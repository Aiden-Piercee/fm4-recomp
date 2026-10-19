#!/usr/bin/env python3
"""make_grid32_pack.py - FM4 DLC-style data pack that extends a track's start grid
(default: Le Mans, StartGridPositions id 100, shared by tracks 100 and 101) from 16 to 32 slots.

  python3 make_grid32_pack.py --fm4-media <FM4 Media dir> --fm4-user <.local/user dir>
        --out <staging dir> --work <scratch dir> [--install] [--offer 131] [--grid-id 100] [--slots 32]

Extra slots continue the stock pattern: rows come in staggered pairs (about 7.75 m apart, alternating
lateral offsets), so slot k+16 = slot k pushed back by 16/2 pairs * pitch. The pack is data only
(one ContentOffers row + one execute.sql). The matching engine hooks (fm4/src/hooks.cpp, FM4_MAX_CARS)
are what actually lets 32 cars race; this file only supplies the grid positions.
"""
import argparse, glob, hashlib, os, re, shutil, sqlite3, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip
from port_dlc_car import write_zip, stored, TITLE


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--offer', type=int, default=131); ap.add_argument('--offer-id', default='4D530910F4FF7901')
    ap.add_argument('--grid-id', type=int, default=100); ap.add_argument('--slots', type=int, default=32)
    ap.add_argument('--pack-name', default='Le Mans 32-car grid')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    pkg = hashlib.sha1(('FM4GRID32:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')

    B = sqlite3.connect('file:%s?mode=ro' % os.path.join(a.fm4_media, 'db', 'gamedb.slt'), uri=True)
    dbs = [B]; merge_paths = []; patch_pri = set()
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
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
    if a.offer_id in used('ContentOffers', 'ID'): raise SystemExit('offer id %s already used' % a.offer_id)

    # ---- does any installed pack already touch this grid id?
    for d in dbs[1:]:
        try:
            n = d.execute('select count(*) from StartGridPositions where id=?', (a.grid_id,)).fetchone()[0]
            if n: raise SystemExit('an installed merge already defines grid %d (%d rows)' % (a.grid_id, n))
        except sqlite3.Error: pass

    rows = B.execute('select StartIndex, MetersBackFromStartLine, MetersRightOfCenterLine, Yaw '
                     'from StartGridPositions where id=? order by StartIndex', (a.grid_id,)).fetchall()
    n0 = len(rows)
    if n0 < 4 or n0 % 2: raise SystemExit('grid %d has %d rows' % (a.grid_id, n0))
    assert [r[0] for r in rows] == list(range(n0))
    back = [r[1] for r in rows]
    pair_pitch = (back[n0 - 2] - back[0]) / ((n0 - 2) / 2)      # metres per staggered pair
    sql = []
    for k in range(n0, a.slots):
        src = rows[k % n0]; cycle = k // n0
        b = src[1] + cycle * (back[n0 - 1] - back[0] + pair_pitch)
        sql.append('INSERT INTO StartGridPositions (id,StartIndex,MetersBackFromStartLine,MetersRightOfCenterLine,Yaw) '
                   'VALUES (%d,%d,%.6f,%.6f,%.6f);' % (a.grid_id, k, b, src[2], src[3]))
    print('grid %d: %d -> %d slots; pair pitch %.3f m; last slot %.1f m behind the line' % (
        a.grid_id, n0, a.slots, pair_pitch, back[0] + (a.slots // 2 - 1) * pair_pitch))

    pri = a.offer * 1000 + 1
    if pri in patch_pri: raise SystemExit('db patch priority %d already used' % pri)

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
    srcrow.update(ID=a.offer_id, OfferId=int(a.offer_id, 16), PubOfferingID=a.offer, DisplayName=ref(off_hi, 0xF4F2),
                  Description=ref(off_hi, 0xF4F3), LicenseLevel=1, LicenseMask=1, IsDLC=1, IsInstalled=0,
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

    hdr_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, 'Headers', '00000002')
    hdrs = sorted(glob.glob(os.path.join(hdr_dir, '*.header')))
    tmplh = next((h for h in hdrs if os.path.basename(h).startswith('E3470A83')), None) or next(h for h in hdrs if len(os.path.basename(h)) > 30)
    h = bytearray(open(tmplh, 'rb').read())
    h[0x08:0x108] = a.pack_name[:127].encode('utf-16-be').ljust(256, b'\0')
    h[0x108:0x132] = pkg.encode('ascii'); h[0x132:0x134] = b'\0\0'; h[0x134:0x13C] = b'\0' * 8
    hdr_out = os.path.join(a.out, pkg + '.header'); open(hdr_out, 'wb').write(h)

    # ---- self-check: apply the SQL to a copy of the base db
    chk = os.path.join(a.work, 'chk.slt'); shutil.copyfile(os.path.join(a.fm4_media, 'db', 'gamedb.slt'), chk)
    C = sqlite3.connect(chk)
    for s in sql: C.execute(s)
    got = C.execute('select count(*), min(StartIndex), max(StartIndex), count(distinct StartIndex) from StartGridPositions where id=?', (a.grid_id,)).fetchone()
    pts = C.execute('select MetersBackFromStartLine, MetersRightOfCenterLine from StartGridPositions where id=?', (a.grid_id,)).fetchall()
    dup = len(pts) - len({(round(x, 1), round(y, 1)) for x, y in pts})
    C.close(); os.remove(chk)
    print('self-check rows/min/max/distinct', got, ' overlapping slots:', dup)
    assert got[0] == a.slots and got[3] == a.slots and dup == 0

    if a.install:
        shutil.copytree(root, os.path.join(pkgs_dir, pkg), dirs_exist_ok=True)
        shutil.copyfile(hdr_out, os.path.join(hdr_dir, pkg + '.header'))
    print('package', pkg, '(installed)' if a.install else '(staged, not installed)')
    print('offer', a.offer, a.offer_id, ' execute priority', pri)


if __name__ == '__main__':
    main()
