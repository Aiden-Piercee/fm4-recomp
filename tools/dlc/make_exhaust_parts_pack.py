"""Exhaust parts pack: per-exhaust-level show/hide of car sections (Forza-4-Evolution).

Research: claude/exhaust-upgrade-visuals-research-2026-10-12.md
Needs the ExhaustPartsFlag hook (fm4/src/brake_preview.cpp), which makes the game
resolve List_UpgradeEngineExhaust.PartsStringID like it does for body parts.

The pack is data only: a native FM4 "execute" DB patch (SQL lines, as in the Porsche
Expansion's 100001_execute.sql and our AI drivers pack) that
  * inserts List_PartsStrings rows (comma list of section names, "-name" hides one;
    names match sections case-insensitively, e.g. the stock "SkirtLa,SkirtRa"), and
  * points List_UpgradeEngineExhaust rows (Id = EngineID*1000 + n) at them.

Spec (JSON):
  {"strings": {"9001": "-exhaustRa,exhaustRc", "9002": "-exhaustRa"},
   "exhaust": {"179002": 9001, "179003": 9002}}

Usage:
  python make_exhaust_parts_pack.py --spec spec.json --fm4-media <extracted>/Media
      --fm4-user <repo>/.local/user --out <dir> --work <dir> [--install]
Package layout: <offer>_pri_65.zip with media/db/patch/<offer>000_merge.slt (one
ContentOffers row) + <offer>001_execute.sql; <offer>.puboffer; empty zipmount.xml and
stringtables.xml; cloned XContent .header. Uninstall: move the package folder and its
.header out (game closed).
"""
import argparse, glob, hashlib, json, os, re, shutil, sqlite3, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip
from port_dlc_car import write_zip, stored, TITLE


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--spec', required=True)
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--offer', type=int, default=140); ap.add_argument('--offer-id', default='4D530910F4FF8C00')
    ap.add_argument('--pack-name', default='Exhaust Parts Test')
    a = ap.parse_args()
    spec = json.load(open(a.spec))
    strings = {int(k): v for k, v in spec.get('strings', {}).items()}
    exhaust = {int(k): int(v) for k, v in spec.get('exhaust', {}).items()}

    os.makedirs(a.work, exist_ok=True); os.makedirs(a.out, exist_ok=True)
    pkg = hashlib.sha1(('FM4EXHAUSTPARTS:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')

    # ---- base db + every installed merge (except ours): clash checks
    B = sqlite3.connect('file:%s?mode=ro' % os.path.join(a.fm4_media, 'db', 'gamedb.slt'), uri=True)
    dbs = [B]; merge_paths = []; patch_pri = set(); offers = set()
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
        m = re.match(r'(\d+)_pri_65\.zip$', os.path.basename(z))
        if m: offers.add(int(m.group(1)))
        for i in zipfile.ZipFile(z).infolist():
            fn = i.filename.replace('\\', '/').lower()
            if '/db/patch/' in fn:
                m = re.match(r'(\d+)_', os.path.basename(fn))
                if m: patch_pri.add(int(m.group(1)))
            if fn.endswith('_merge.slt'):
                p = os.path.join(a.work, 'm_%s_%s' % (z.split(os.sep)[-4][:8], os.path.basename(i.filename)))
                with open(z, 'rb') as f: open(p, 'wb').write(forza_zip.read_entry(f, i))
                dbs.append(sqlite3.connect(p)); merge_paths.append((dbs[-1], p))
    def used(table, col):
        s = set()
        for d in dbs:
            try: s |= {r[0] for r in d.execute('select "%s" from "%s"' % (col, table))}
            except sqlite3.Error: pass
        return s
    if a.offer in offers: raise SystemExit('offer %d already installed by another package' % a.offer)
    if a.offer_id in used('ContentOffers', 'ID'): raise SystemExit('offer id %s already used' % a.offer_id)
    clash = set(strings) & used('List_PartsStrings', 'Id')
    if clash: raise SystemExit('List_PartsStrings ids already used: %s' % sorted(clash))
    exh_rows = {r[0]: r for r in B.execute('select Id, EngineID, Level, IsStock, PartsStringID from List_UpgradeEngineExhaust')}
    for pid, sid in exhaust.items():
        if pid not in exh_rows: raise SystemExit('no List_UpgradeEngineExhaust row %d in the base DB' % pid)
        if sid not in strings and sid not in used('List_PartsStrings', 'Id'):
            raise SystemExit('exhaust %d points at unknown parts string %d' % (pid, sid))
    pri = a.offer * 1000 + 1
    if pri in patch_pri: raise SystemExit('db patch priority %d already used' % pri)

    def q(s): return "'" + s.replace("'", "''") + "'"
    sql = ['INSERT INTO List_PartsStrings (Id, PartsString) VALUES (%d, %s);' % (k, q(v)) for k, v in sorted(strings.items())]
    sql += ['UPDATE List_UpgradeEngineExhaust SET PartsStringID = %d WHERE Id = %d;' % (sid, pid) for pid, sid in sorted(exhaust.items())]

    # dry run on a copy of the base DB
    chk = os.path.join(a.work, 'check.slt'); shutil.copyfile(os.path.join(a.fm4_media, 'db', 'gamedb.slt'), chk)
    C = sqlite3.connect(chk)
    for s in sql: C.execute(s)
    for pid, sid in exhaust.items():
        got = C.execute('select e.PartsStringID, s.PartsString from List_UpgradeEngineExhaust e join List_PartsStrings s '
                        'on s.Id = e.PartsStringID where e.Id = ?', (pid,)).fetchone()
        assert got and got[0] == sid, (pid, got)
    C.close()

    # ---- merge.slt with the ContentOffers row (template = an installed FM4 DLC merge)
    tmpl_path = next((p for d, p in merge_paths if d.execute("select 1 from sqlite_master where name='ContentOffers'").fetchone()), None)
    if not tmpl_path: raise SystemExit('no installed FM4 DLC merge to use as template')
    out_slt = os.path.join(a.work, '%d000_merge.slt' % a.offer); shutil.copyfile(tmpl_path, out_slt)
    O = sqlite3.connect(out_slt)
    for (t,) in O.execute("select name from sqlite_master where type='table'").fetchall(): O.execute('delete from "%s"' % t)
    oc = [r[1] for r in O.execute('pragma table_info(ContentOffers)')]
    srcrow = None
    for d in dbs[1:]:
        try:
            rr = d.execute('select * from ContentOffers where ImagePath like ? limit 1', ('%Porsche%',)).fetchone() or d.execute('select * from ContentOffers limit 1').fetchone()
            if rr: srcrow = dict(zip([c[1] for c in d.execute('pragma table_info(ContentOffers)')], rr)); break
        except sqlite3.Error: pass
    ref = lambda hi, lo: '_&%d' % ((hi << 16) | lo)
    off_hi = int(srcrow['DisplayName'][2:]) >> 16
    srcrow.update(ID=a.offer_id, OfferId=int(a.offer_id, 16), PubOfferingID=a.offer, DisplayName=ref(off_hi, 0xF4F0),
                  Description=ref(off_hi, 0xF4F1), LicenseLevel=1, LicenseMask=1, IsDLC=1, IsInstalled=0,
                  IsPurchased=0, IsVIP=0, Hidden=0, IsALaCarte=0, SeasonPassOffer=0)
    O.execute('insert into ContentOffers (%s) values (%s)' % (','.join('"%s"' % c for c in oc), ','.join('?' * len(oc))), [srcrow.get(c) for c in oc])
    O.commit(); O.execute('vacuum'); O.close()

    root = os.path.join(a.out, pkg); pm = os.path.join(root, 'Media'); pdz = os.path.join(pm, 'DLCZips')
    if os.path.isdir(root): shutil.rmtree(root)
    os.makedirs(pdz)
    entries = [stored('media/db/patch/%d000_merge.slt' % a.offer, open(out_slt, 'rb').read()),
               stored('media/db/patch/%d_execute.sql' % pri, '\r\n'.join(sql).encode('ascii'))]
    open(os.path.join(pm, '%d.puboffer' % a.offer), 'wb').write(b' \r\n')
    write_zip(os.path.join(pdz, '%d_pri_65.zip' % a.offer), entries)
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='\r\n').write('<?xml version="1.0" ?> \n<zipmount> \n</zipmount> \n')
    open(os.path.join(pdz, 'stringtables.xml'), 'w', newline='\r\n').write('<?xml version="1.0" ?> \n<stringtables> \n</stringtables> \n')

    # ---- XContent header (clone the Porsche Expansion header if present)
    hdr_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, 'Headers', '00000002')
    hdrs = sorted(glob.glob(os.path.join(hdr_dir, '*.header')))
    tmplh = next((h for h in hdrs if os.path.basename(h).startswith('E3470A83')), None) or next(h for h in hdrs if len(os.path.basename(h)) > 30)
    h = bytearray(open(tmplh, 'rb').read())
    h[0x08:0x108] = a.pack_name[:127].encode('utf-16-be').ljust(256, b'\0')
    h[0x108:0x132] = pkg.encode('ascii'); h[0x132:0x134] = b'\0\0'; h[0x134:0x13C] = b'\0' * 8
    hdr_out = os.path.join(a.out, pkg + '.header'); open(hdr_out, 'wb').write(h)

    if a.install:
        shutil.copytree(root, os.path.join(pkgs_dir, pkg), dirs_exist_ok=True)
        shutil.copyfile(hdr_out, os.path.join(hdr_dir, pkg + '.header'))
    print('package', pkg, '(installed)' if a.install else '')
    print('offer', a.offer, a.offer_id, ' patch', pri)
    for s in sql: print('  ' + s)


if __name__ == '__main__':
    main()
