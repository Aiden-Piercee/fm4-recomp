#!/usr/bin/env python3
"""dlc_inspect.py - one-shot "can this Forza DLC car run in FM4?" report.

Works on FM2 / FM3 / FM4 marketplace packages (STFS LIVE/PIRS) or on an
already-extracted package folder (the one containing Media/).

  python3 dlc_inspect.py <package-or-folder> --fm4-db <FM4 gamedb.slt>
         [--fm4-str <dir with EN/Data_Car.str (latest cumulative)>]
         [--work <scratch dir>] [--out report.md]

What it checks, per package:
  1. container: magic, title id (4D5307EA FM2 / 4D53084D FM3 / 4D530910 FM4)
  2. DLCZips layout: car zip(s), zipmount.xml, stringtables.xml, LicenseMasks
  3. merge.slt: every table vs FM4 schema (same / column diff / missing),
     ID collisions with FM4, which Data_Car MediaNames FM4 already has
  4. per car: file set vs FM4 convention, .carbin header versions
     (FM4 body=2 wheels=3; FM3 body=1 wheels=2), texture header match
  5. strings: car DisplayName resolved from the pack's Data_Car.str, and
     whether FM4's latest string table already holds that hash
"""
import argparse, os, re, sqlite3, struct, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip, forza_str, stfs_extract

TITLES = {0x4D5307EA: 'FM2', 0x4D53084D: 'FM3', 0x4D530910: 'FM4'}
FM4_CARBIN = {'body': 2, 'wheel': 3}

def unpack_package(pkg, work):
    if os.path.isdir(pkg):
        return pkg, None
    s = stfs_extract.Stfs(pkg)
    dest = os.path.join(work, os.path.basename(pkg))
    for e in s.entries:
        if e['dir']: continue
        p = os.path.join(dest, e['path'])
        if os.path.exists(p) and os.path.getsize(p) == e['size']: continue
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(s.read(e))
    return dest, s

def zip_read(zp, name):
    zf = zipfile.ZipFile(zp)
    with open(zp, 'rb') as f:
        return forza_zip.read_entry(f, zf.getinfo(name))

def cols(c, t): return [r[1] for r in c.execute('pragma table_info("%s")' % t)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package'); ap.add_argument('--fm4-db', required=True)
    ap.add_argument('--fm4-str'); ap.add_argument('--work', default=os.path.expanduser('~/dlc_work'))
    ap.add_argument('--out')
    ap.add_argument('--fm4-dlc-merges', help='folder of FM4 DLC *_merge.slt files (cars added by FM4 DLC count as present)')
    ap.add_argument('--src-str', help="source game's base EN/Data_Car.str (names for disc-2 style packs)")
    ap.add_argument('--src-db', help='source game gamedb.slt, for packs whose cars live in the base DB (disc-2 style, no merge.slt)')
    a = ap.parse_args()
    out = []; P = out.append
    root, s = unpack_package(a.package, a.work)
    if s:
        P('# %s  (%s, %s, title %08X)\n' % (s.display_name, s.magic,
          TITLES.get(s.title_id, '?'), s.title_id))
    else:
        P('# %s\n' % root)
    media = next((os.path.join(root, d) for d in os.listdir(root) if d.lower() == 'media'), root)
    dz = next((os.path.join(media, d) for d in os.listdir(media) if d.lower() == 'dlczips'), None)
    if not dz:
        P('No Media/DLCZips - not a car/track content pack.'); print('\n'.join(out)); return
    P('## Package layout')
    for f in sorted(os.listdir(media)):
        P('- Media/%s' % f)
    for f in sorted(os.listdir(dz)):
        p = os.path.join(dz, f)
        P('- DLCZips/%s%s' % (f, '' if os.path.isdir(p) else '  (%d bytes)' % os.path.getsize(p)))
    for x in ('zipmount.xml', 'stringtables.xml'):
        p = os.path.join(dz, x)
        if os.path.exists(p):
            P('\n`%s`:\n```\n%s\n```' % (x, open(p, errors='replace').read().strip()))
    lm = os.path.join(media, 'LicenseMasks')
    if os.path.isdir(lm):
        for f in sorted(os.listdir(lm)):
            P('\nLicenseMasks/%s: `%s`' % (f, open(os.path.join(lm, f), errors='replace').read().strip()[:300]))

    zips = [os.path.join(dz, f) for f in sorted(os.listdir(dz)) if f.lower().endswith('.zip')]
    content = [z for z in zips if re.match(r'^[0-9A-Fa-f]+_pri_|^DLC\d+_pri_', os.path.basename(z))]
    strz = [z for z in zips if os.path.basename(z).lower().startswith(('stringtables', 'dlc3overrides')) or
            any(n.lower().endswith('.str') for n in zipfile.ZipFile(z).namelist()[:5])]

    B = sqlite3.connect('file:%s?mode=ro' % a.fm4_db, uri=True)
    fm4_tables = {r[0] for r in B.execute("select name from sqlite_master where type='table'")}
    fm4_media = {r[0].lower(): r[1] for r in B.execute('select MediaName, Id from Data_Car')}
    fm4_ids = {r[0] for r in B.execute('select Id from Data_Car')}
    fm4_where = {k: 'base' for k in fm4_media}
    if a.fm4_dlc_merges:
        import glob
        for f in sorted(glob.glob(os.path.join(a.fm4_dlc_merges, '*_merge.slt'))):
            try:
                for mn, i in sqlite3.connect(f).execute('select MediaName, Id from Data_Car'):
                    fm4_media.setdefault(mn.lower(), i); fm4_ids.add(i)
                    fm4_where.setdefault(mn.lower(), 'FM4 DLC ' + os.path.basename(f).split('_')[0])
            except sqlite3.Error:
                pass

    # strings
    pack_str = {}
    for z in strz:
        zf = zipfile.ZipFile(z)
        n = next((n for n in zf.namelist() if re.match(r'(?i)^en/data_car\.str$', n)), None)
        if n:
            tmp = os.path.join(a.work, '_str.tmp'); os.makedirs(a.work, exist_ok=True)
            open(tmp, 'wb').write(zip_read(z, n))
            pack_str = {h: t for k, h, t in forza_str.read(tmp)}
    src_str = {}
    if a.src_str and os.path.exists(a.src_str):
        src_str = {h: t for k, h, t in forza_str.read(a.src_str)}
    fm4_str = {}
    if a.fm4_str and os.path.exists(os.path.join(a.fm4_str, 'EN', 'Data_Car.str')):
        fm4_str = {h: t for k, h, t in forza_str.read(os.path.join(a.fm4_str, 'EN', 'Data_Car.str'))}

    for z in content:
        zf = zipfile.ZipFile(z)
        names = zf.namelist()
        P('\n## Content zip %s  (%d entries, methods %s)' % (os.path.basename(z), len(names),
          sorted({i.compress_type for i in zf.infolist()})))
        dirs = {}
        for n in names:
            m = re.match(r'(?i)media/(cars|wheels|digitalgauges)/([^/]+)/', n)
            if m: dirs.setdefault(m.group(2).lower(), set()).add(m.group(1).lower())
        other = sorted({'/'.join(n.split('/')[:3]) for n in names
                        if not re.match(r'(?i)media/(cars|wheels|digitalgauges)/', n)})
        P('Other content folders: ' + ', '.join(other))
        merges = [n for n in names if re.search(r'(?i)db/patch/.*\.(slt|sql)$', n)]
        cars = []
        for n in merges:
            if not n.lower().endswith('.slt'):
                P('\n`%s` (SQL script, %d bytes) - replay after merge' % (n, zf.getinfo(n).file_size)); continue
            tmp = os.path.join(a.work, os.path.basename(n)); open(tmp, 'wb').write(zip_read(z, n))
            M = sqlite3.connect(tmp)
            P('\n### %s - table compatibility with FM4' % n)
            P('| table | rows | FM4 schema | id collisions |\n|---|---|---|---|')
            for (t,) in M.execute("select name from sqlite_master where type='table'"):
                rows = M.execute('select count(*) from "%s"' % t).fetchone()[0]
                if t not in fm4_tables:
                    P('| %s | %d | **missing in FM4** | - |' % (t, rows)); continue
                pc, bc = cols(M, t), cols(B, t)
                if pc == bc: sch = 'same'
                else:
                    sch = 'drop %s / add %s' % (sorted(set(pc) - set(bc)) or '-', sorted(set(bc) - set(pc)) or '-')
                k = pc[0]
                ids = [r[0] for r in M.execute('select "%s" from "%s"' % (k, t))]
                col = 0
                if k in bc and ids:
                    for i in range(0, len(ids), 500):
                        ch = ids[i:i + 500]
                        col += B.execute('select count(*) from "%s" where "%s" in (%s)' % (t, k, ','.join('?' * len(ch))), ch).fetchone()[0]
                P('| %s | %d | %s | %d/%d on %s |' % (t, rows, sch, col, len(ids), k))
            if 'Data_Car' in [r[0] for r in M.execute("select name from sqlite_master")]:
                for cid, mn, dn in M.execute('select Id, MediaName, DisplayName from Data_Car'):
                    cars.append((cid, mn, dn))
        known = {c[1].lower() for c in cars}
        car_dirs = sorted({re.match(r'(?i)media/cars/([^/]+)/', n).group(1) for n in names
                           if re.match(r'(?i)media/cars/[^/]+/.*\.carbin$', n)})
        if a.src_db:
            S = sqlite3.connect('file:%s?mode=ro' % a.src_db, uri=True)
            for d in car_dirs:
                if d.lower() in known: continue
                r = S.execute('select Id, MediaName, DisplayName from Data_Car where lower(MediaName)=?', (d.lower(),)).fetchone()
                cars.append(r if r else (None, d, None))
        else:
            cars += [(None, d, None) for d in car_dirs if d.lower() not in known]
        P('\n### Cars')
        P('| Id | MediaName | name (pack str) | in FM4? | Id free in FM4? | FM4 str has hash? | body carbin v | wheel carbin v | layout notes |')
        P('|---|---|---|---|---|---|---|---|---|')
        for cid, mn, dn in cars:
            h = int(dn[2:]) & 0xFFFF if dn and dn.startswith('_&') else None
            name = (pack_str.get(h) or src_str.get(h, '?')) if h is not None else (dn or '?')
            in4 = 'yes (Id %s, %s)' % (fm4_media[mn.lower()], fm4_where[mn.lower()]) if mn.lower() in fm4_media else '**no**'
            free = '?' if cid is None else 'yes' if cid not in fm4_ids else ('same car' if fm4_media.get(mn.lower()) == cid else '**taken**')
            has = ('yes' if h in fm4_str else 'no') if fm4_str else 'n/a'
            body = next((n for n in names if re.match(r'(?i)media/cars/%s/%s\.carbin$' % (re.escape(mn), re.escape(mn)), n)), None)
            wheel = next((n for n in names if re.match(r'(?i)media/wheels/%s/%s\.carbin$' % (re.escape(mn), re.escape(mn)), n)), None)
            bv = struct.unpack('>I', zip_read(z, body)[:4])[0] if body else '-'
            wv = struct.unpack('>I', zip_read(z, wheel)[:4])[0] if wheel else '-'
            files = [n.lower() for n in names if n.lower().startswith('media/cars/%s/' % mn.lower())]
            notes = []
            if not any(f.endswith('_lod0.carbin') and 'caliper' not in f and 'rotor' not in f for f in files): notes.append('no lod0 body')
            if not any(f.endswith('_cockpit.carbin') for f in files): notes.append('no cockpit')
            if any(re.search(r'(caliper|rotor)..\.carbin$', f) for f in files): notes.append('caliper/rotor need _lod0 rename')
            if any(n.lower().startswith('media/digitalgauges/dash_%s/' % mn.lower()) for n in names): notes.append('gauges in media/DigitalGauges (FM4: cars/<car>/digitalgauge)')
            if bv != FM4_CARBIN['body']: notes.append('body carbin v%s != FM4 v2' % bv)
            if wv != FM4_CARBIN['wheel']: notes.append('wheel carbin v%s != FM4 v3' % wv)
            P('| %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (cid, mn, name, in4, free, has, bv, wv, '; '.join(notes) or 'FM4-ready'))
    txt = '\n'.join(out)
    if a.out:
        open(a.out, 'w').write(txt)
    print(txt)

if __name__ == '__main__':
    main()
