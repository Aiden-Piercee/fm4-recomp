#!/usr/bin/env python3
"""make_dealer_thumbs_pack.py - FM4 add-on pack that clears the big 3/4 picture (x 0-607) of every car
thumbnail (Thumbnail_<id>.xds, 768x288) so the 3D stage car behind the Buy Cars list shows through.
The rotated side view (x 608-767, used by the film-strip tiles) is left untouched.

  python3 make_dealer_thumbs_pack.py --fm4-media <Media dir> --fm4-user <.local/user dir> --out <staging dir>
        --work <scratch dir> [--ids 1463,1726] [--install] [--offer 160]

Without --ids every thumbnail is processed (base ui/Textures.zip plus thumbnails inside installed packs;
installed-pack versions win). Needs numpy, libfzlzx.so, and thumbx.py/xds.py (--thumbx-dir).
"""
import argparse, glob, hashlib, os, re, shutil, sqlite3, struct, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import forza_zip
from port_dlc_car import write_zip, stored, TITLE

MAIN_W = 608          # columns 0..607 = main 3/4 view
PRI = 27000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--thumbx-dir', default=os.path.expanduser('~/th'))
    ap.add_argument('--ids', default=''); ap.add_argument('--install', action='store_true')
    ap.add_argument('--offer', type=int, default=160); ap.add_argument('--offer-id', default='4D530910F4FF7A00')
    ap.add_argument('--pack-name', default='Dealership 3D stage thumbnails')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    os.chdir(a.thumbx_dir); sys.path.insert(0, a.thumbx_dir)
    import thumbx
    import numpy as np
    want = {int(x) for x in a.ids.split(',') if x.strip()}
    pkg = hashlib.sha1(('FM4DEALERTHUMBS:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')

    # ---- gather source thumbnails: base first, installed packs override
    src = {}
    def collect(zpath, pat):
        with open(zpath, 'rb') as f:
            for i in zipfile.ZipFile(zpath).infolist():
                m = re.search(r'thumbnails/thumbnail_(\d+)\.xds$', i.filename.replace('\\', '/').lower())
                if not m: continue
                n = int(m.group(1))
                if want and n not in want: continue
                src[n] = (zpath, i.filename)
    base = os.path.join(a.fm4_media, 'UI', 'Textures.zip')
    if not os.path.exists(base): base = os.path.join(a.fm4_media, 'ui', 'Textures.zip')
    collect(base, None)
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
        collect(z, None)
    print('thumbnails to process:', len(src))

    entries = []; bad = 0
    handles = {}
    for n in sorted(src):
        zpath, name = src[n]
        if zpath not in handles: handles[zpath] = (open(zpath, 'rb'), zipfile.ZipFile(zpath))
        f, zf = handles[zpath]
        d = forza_zip.read_entry(f, zf.getinfo(name))
        tmp = os.path.join(a.work, 't.xds'); open(tmp, 'wb').write(d)
        try:
            img, hdr = thumbx.read(tmp)
        except AssertionError:
            bad += 1; print('skip (format)', n); continue
        if img.shape[1] != 768: bad += 1; print('skip (size)', n, img.shape); continue
        img[:, :MAIN_W, :] = 0
        thumbx.write(tmp, img, hdr)
        entries.append(stored('Thumbnail_%d.xds' % n, open(tmp, 'rb').read()))
    print('written', len(entries), 'skipped', bad)

    # ---- ContentOffers row (clone of an installed offer), as in make_grid32_pack.py
    dbs = []; merge_paths = []
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
        with open(z, 'rb') as f:
            for i in zipfile.ZipFile(z).infolist():
                if i.filename.endswith('_merge.slt'):
                    p = os.path.join(a.work, 'm_%s_%s' % (z.split(os.sep)[-4][:8], os.path.basename(i.filename)))
                    open(p, 'wb').write(forza_zip.read_entry(f, i))
                    dbs.append(sqlite3.connect(p)); merge_paths.append((dbs[-1], p))
    for d in dbs:
        if a.offer_id in {r[0] for r in d.execute('select ID from ContentOffers')}: raise SystemExit('offer id used')
    tmpl_path = next((p for d, p in merge_paths if d.execute("select 1 from sqlite_master where name='ContentOffers'").fetchone()), None)
    out_slt = os.path.join(a.work, '%d000_merge.slt' % a.offer); shutil.copyfile(tmpl_path, out_slt)
    O = sqlite3.connect(out_slt)
    for (t,) in O.execute("select name from sqlite_master where type='table'").fetchall(): O.execute('delete from "%s"' % t)
    oc = [r[1] for r in O.execute('pragma table_info(ContentOffers)')]
    srcrow = None
    for d in dbs:
        rr = d.execute('select * from ContentOffers limit 1').fetchone()
        if rr: srcrow = dict(zip([c[1] for c in d.execute('pragma table_info(ContentOffers)')], rr)); break
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
    open(os.path.join(pm, '%d.puboffer' % a.offer), 'wb').write(b' \r\n')
    write_zip(os.path.join(pdz, '%d_pri_65.zip' % a.offer),
              [stored('media/db/patch/%d000_merge.slt' % a.offer, open(out_slt, 'rb').read())])
    write_zip(os.path.join(pdz, 'Thumbnails_pri_%d.zip' % PRI), entries)
    # ---- the flat hangar picture behind the thumbnail (UI4/BuyCar/THUMBNAIL_BACKGROUND.xds): make it transparent
    # (+ Car_Select_CAR: the grey gradient the picture box shows while a thumbnail is still loading)
    bg_entries = []
    with open(base, 'rb') as f:
        bz = zipfile.ZipFile(base)
        for nm in ('THUMBNAIL_BACKGROUND', 'Car_Select_CAR'):
            bg = forza_zip.read_entry(f, bz.getinfo('UI4/BuyCar/%s.xds' % nm))
            tmp = os.path.join(a.work, 'bg.xds'); open(tmp, 'wb').write(bg)
            bimg, bhdr = thumbx.read(tmp); bimg[:] = 0; thumbx.write(tmp, bimg, bhdr)
            bg_entries.append(stored('%s.xds' % nm, open(tmp, 'rb').read()))
    write_zip(os.path.join(pdz, 'BuyCarUI_pri_%d.zip' % PRI), bg_entries)
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='').write(
        '<?xml version="1.0" ?> \r\n<zipmount> \r\n'
        '   <zip Name="Thumbnails_pri_%d.zip" Mount="game:\\Media\\ui\\Textures\\Thumbnails\\" '
        'AltRootPath="game:\\Media\\ui\\Textures\\Thumbnails\\" ShouldCache="0" /> \r\n'
        '   <zip Name="BuyCarUI_pri_%d.zip" Mount="game:\\Media\\ui\\Textures\\UI4\\BuyCar\\" '
        'AltRootPath="game:\\Media\\ui\\Textures\\UI4\\BuyCar\\" ShouldCache="0" /> \r\n</zipmount> \r\n' % (PRI, PRI))

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
    print('package', pkg, '(installed)' if a.install else '(staged, not installed)')


if __name__ == '__main__':
    main()
