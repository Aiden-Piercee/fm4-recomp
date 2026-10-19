"""make_fh_wheels_pack.py - aftermarket-rim DLC pack from Horizon 1/2 stock rims (derived from make_fm2_wheels_pack.py).

Original doc: aftermarket-rim DLC pack from FM2 wheels (Forza-4-Evolution).

Converts FM2 wheel zips (carbin v1, deflate xds) to FM4 wheel folders and adds List_Wheels rows
(IsStock=0 = shop rims, every car can use them).  Data only: one native FM4 "execute" DB patch + the wheel
files + one shop icon per rim.  Template row/carbin come from an FM4 aftermarket rim (default BBS_RE).

  python make_fm2_wheels_pack.py --wheel bbs_rwii --wheel bbs_ch --wheel bbs_vz
      --fm2-media <game-content>/FM2/Media --fm4-media <extracted>/Media --fm4-user <repo>/.local/user
      --out <dir> --work <dir> [--install] [--offer 150 --offer-id 4D530910F4FF9600 --wheel-id 11900]
"""
import argparse, glob, hashlib, io, os, re, shutil, sqlite3, struct, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
import forza_zip, carbin_fm2_wheel, carbin
from port_dlc_car import write_zip, stored, TITLE

ICON_DIR = 'media/UI/Textures/UI4/Upgrades/Tires_Rims/Licensed_Rims/'


def ci_get(zf, name):
    for i in zf.infolist():
        if i.filename.lower() == name.lower(): return zf.read(i.filename)
    return None


def zread_any(path, name):
    f = open(path, 'rb'); z = zipfile.ZipFile(path)
    for i in z.infolist():
        if i.filename.lower() == name.lower():
            try: return z.read(i.filename)
            except NotImplementedError: return forza_zip.read_entry(f, i)
    raise KeyError(name)


# ---- Xbox 360 texture helpers (tiled DXT5 / 32-bit)
def off2d(x, y, pitch, l2):
    pitch = (pitch + 31) & ~31
    macro = ((y >> 5) * (pitch >> 5)) << (l2 + 7)
    micro = ((y & 6) << 2) << l2
    base = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 8) << (3 + l2)) + ((y & 1) << 4)
    macro = (x >> 5) << (l2 + 7)
    micro = (x & 7) << l2
    o = base + macro + ((micro & ~0xF) << 1) + (micro & 0xF)
    return ((o & ~0x1FF) << 3) + ((o & 0x1C0) << 2) + (o & 0x3F) + ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)


def decode_dxt5_xds(d):
    sz = struct.unpack('>I', d[0x24:0x28])[0]; w = (sz & 0x1FFF) + 1; h = ((sz >> 13) & 0x1FFF) + 1
    body = d[0x34:]; sw = bytearray(len(body)); sw[0::2] = body[1::2]; sw[1::2] = body[0::2]
    bw, bh = w // 4, h // 4; out = bytearray(bw * bh * 16)
    for y in range(bh):
        for x in range(bw):
            o = off2d(x, y, bw, 4); out[(y * bw + x) * 16:(y * bw + x + 1) * 16] = sw[o:o + 16]
    hdr = b'DDS ' + struct.pack('<7I', 124, 0x81007, h, w, len(out), 0, 0) + b'\0' * 44 + \
        struct.pack('<2I4s5I', 32, 4, b'DXT5', 0, 0, 0, 0, 0) + struct.pack('<5I', 0x1000, 0, 0, 0, 0)
    return Image.open(io.BytesIO(hdr + bytes(out))).convert('RGBA')


def encode32(img, tmpl):
    sz = struct.unpack('>I', tmpl[0x24:0x28])[0]; W = (sz & 0x1FFF) + 1; H = ((sz >> 13) & 0x1FFF) + 1
    assert img.size == (W, H), (img.size, W, H)
    pw, ph = (W + 31) & ~31, (H + 31) & ~31
    px = np.asarray(img.convert('RGBA'), np.uint8)
    argb = np.stack([px[..., 3], px[..., 0], px[..., 1], px[..., 2]], -1)
    body = bytearray(pw * ph * 4)
    for y in range(H):
        for x in range(W):
            o = off2d(x, y, pw, 2); body[o:o + 4] = argb[y, x].tobytes()
    return tmpl[:0x34] + bytes(body)[:len(tmpl) - 0x34]


LOGO_IMG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fh_logo.png')
def make_logo(size, big):
    if os.path.exists(LOGO_IMG):
        w, h = size
        c = Image.open(LOGO_IMG).convert('RGB')
        sc = min(w * 0.94 / c.width, h * 0.90 / c.height)
        r = c.resize((max(1, round(c.width * sc)), max(1, round(c.height * sc))), Image.LANCZOS)
        k = Image.new('RGBA', (w, h), (255, 255, 255, 255)); k.paste(r, ((w - r.width) // 2, (h - r.height) // 2))
        return k
    return make_logo_text(size, big)
def make_logo_text(size, big):
    """Brand tile: white card, black lettering (same look as the other maker logos)."""
    from PIL import ImageDraw, ImageFont
    W, H = size; img = Image.new('RGBA', (W, H), (255, 255, 255, 255)); d = ImageDraw.Draw(img)
    fp = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
    f1 = ImageFont.truetype(fp, int(H * 0.52)); f2 = ImageFont.truetype(fp, max(8, int(H * 0.15)))
    b = d.textbbox((0, 0), 'FM2', font=f1); d.text(((W - (b[2] - b[0])) / 2 - b[0], H * 0.12 - b[1]), 'FM2', fill=(0, 0, 0, 255), font=f1)
    t = 'FORZA MOTORSPORT 2'
    fs_ = max(8, int(H * 0.15))
    while fs_ > 6:
        f2 = ImageFont.truetype(fp, fs_); b = d.textbbox((0, 0), t, font=f2)
        if b[2] - b[0] <= W * 0.92: break
        fs_ -= 1
    b = d.textbbox((0, 0), t, font=f2)
    d.text(((W - (b[2] - b[0])) / 2 - b[0], H * 0.80 - b[1]), t, fill=(0, 0, 0, 255), font=f2)
    return img


def make_icon_xds(face, tmpl):
    """220x124 32-bit tiled icon (UI template header), rim face fitted on a transparent background."""
    sz = struct.unpack('>I', tmpl[0x24:0x28])[0]; W = (sz & 0x1FFF) + 1; H = ((sz >> 13) & 0x1FFF) + 1
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    s = H - 4; f = face.resize((s, s), Image.LANCZOS)
    from PIL import ImageDraw
    mk = Image.new('L', (s * 4, s * 4), 0); ImageDraw.Draw(mk).ellipse((0, 0, s * 4 - 1, s * 4 - 1), fill=255)
    mk = mk.resize((s, s), Image.LANCZOS); f.putalpha(Image.composite(f.split()[3], Image.new('L', (s, s), 0), mk))
    img.paste(f, ((W - s) // 2, 2), f)
    pw, ph = (W + 31) & ~31, (H + 31) & ~31
    px = np.asarray(img, np.uint8)                       # RGBA
    argb = np.stack([px[..., 3], px[..., 0], px[..., 1], px[..., 2]], -1)   # bytes A R G B (big-endian ARGB)
    body = bytearray(pw * ph * 4)
    for y in range(H):
        for x in range(W):
            o = off2d(x, y, pw, 2); body[o:o + 4] = argb[y, x].tobytes()
    return tmpl[:0x34] + bytes(body)[:len(tmpl) - 0x34]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--wheel', action='append', default=[], help='FM2 wheel media name (lower case), e.g. bbs_rwii')
    ap.add_argument('--fm2-media', required=True); ap.add_argument('--fm4-media', required=True)
    ap.add_argument('--fm4-user', required=True); ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--template', default='BBS_RE'); ap.add_argument('--suffix', default='')
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--offer', type=int, default=151); ap.add_argument('--offer-id', default='4D530910F4FF9700')
    ap.add_argument('--wheel-id', type=int, default=12000); ap.add_argument('--pack-name', default='Horizon Wheels Pack')
    ap.add_argument('--maker-id', type=int, default=1701); ap.add_argument('--brand', default='Forza Horizon')
    ap.add_argument('--fh-root', required=True); ap.add_argument('--list', required=True); ap.add_argument('--limit', type=int, default=0)
    ap.add_argument('--brand-logo', default='fh')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True); os.makedirs(a.out, exist_ok=True)
    pkg = hashlib.sha1(('FM4FHWHEELS:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')
    wdir4 = os.path.join(a.fm4_media, 'wheels'); wdir2 = os.path.join(a.fm2_media, 'wheels')

    # ---- clash checks against base DB and every installed merge (except ours)
    B = sqlite3.connect('file:%s?mode=ro' % os.path.join(a.fm4_media, 'db', 'gamedb.slt'), uri=True)
    dbs = [B]; merge_paths = []; offers = set(); patch_pri = set(); exec_sql = []
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
        m = re.match(r'(\d+)_pri_65\.zip$', os.path.basename(z))
        if m: offers.add(int(m.group(1)))
        for i in zipfile.ZipFile(z).infolist():
            fn = i.filename.replace('\\', '/').lower()
            if '/db/patch/' in fn:
                mm = re.match(r'(\d+)_', os.path.basename(fn))
                if mm: patch_pri.add(int(mm.group(1)))
                if fn.endswith('.sql'):
                    with open(z, 'rb') as f: exec_sql.append(forza_zip.read_entry(f, i).decode('latin-1'))
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
    used_ids = used('List_Wheels', 'ID')
    for t in exec_sql:
        used_ids |= {int(x) for x in re.findall(r"INSERT INTO List_Wheels \([^)]*\) VALUES \((\d+),", t)}
    used_media = {str(x).lower() for x in used('List_Wheels', 'MediaName')}
    pri = a.offer * 1000 + 1
    if pri in patch_pri: raise SystemExit('db patch priority %d already used' % pri)

    cols = [r[1] for r in B.execute('pragma table_info(List_Wheels)')]
    trow = dict(zip(cols, B.execute('select * from List_Wheels where MediaName=?', (a.template,)).fetchone()))
    tcar = zread_any(os.path.join(wdir4, a.template + '.zip'), a.template.lower() + '.carbin')
    ui = zipfile.ZipFile(os.path.join(a.fm4_media, 'UI', 'Textures.zip'))
    ui_f = open(os.path.join(a.fm4_media, 'UI', 'Textures.zip'), 'rb')
    icon_tmpl = forza_zip.read_entry(ui_f, ui.getinfo('UI4/Upgrades/Tires_Rims/Licensed_Rims/%s.xds' % a.template))

    def q(v):
        if v is None: return 'NULL'
        if isinstance(v, (int, float)): return repr(v)
        return "'" + str(v).replace("'", "''") + "'"
    entries = []; sql = []; manuf = {}
    wid = a.wheel_id
    D2 = sqlite3.connect('file:%s?mode=ro' % os.path.join(a.fm2_media, 'DB', 'gamedb.slt'), uri=True)
    D2.text_factory = lambda b: b.decode('latin-1')
    mk2 = {r[0]: r[1].strip() for r in D2.execute('select Id, PartManufacturer from List_PartManufacturer')}
    # FM4 maker names (EN string table) and the maker ids FM4's own aftermarket rims use per media-name prefix
    import forza_str
    en = os.path.join(a.fm4_media, 'StringTables', 'EN.zip'); ef = open(en, 'rb'); ez = zipfile.ZipFile(en)
    tmp = os.path.join(a.work, 'pm.str')
    open(tmp, 'wb').write(forza_zip.read_entry(ef, ez.getinfo('List_PartManufacturer.str')))
    mk4 = {int(k.rsplit('_', 1)[1]): t for k, h, t in forza_str.read(tmp)}
    norm = lambda t: re.sub(r'[^a-z0-9]', '', t.lower())
    by_prefix = {}
    for m_, p_ in B.execute('select MediaName, PartManufacturerID from List_Wheels where IsStock=0'):
        by_prefix.setdefault(m_.split('_')[0].lower(), {}).setdefault(p_, 0); by_prefix[m_.split('_')[0].lower()][p_] += 1
    new_maker = {}; used_makers = used('List_PartManufacturer', 'Id')
    brand_id = a.maker_id
    while brand_id in used_makers: brand_id += 1
    def resolve_maker(media, mk_name):
        cands = by_prefix.get(media.split('_')[0].lower(), {})
        hit = [i for i in cands if norm(mk4.get(i, '')) == norm(mk_name)]
        if hit: return hit[0]
        hit = [i for i, t in mk4.items() if norm(t) == norm(mk_name)]
        if hit: return hit[0]
        if len(cands) == 1: return next(iter(cands))
        nonlocal_state = new_maker
        if norm(mk_name) not in nonlocal_state:
            nid = a.maker_id + len(nonlocal_state)
            while nid in used_makers: nid += 1
            nonlocal_state[norm(mk_name)] = (nid, mk_name)
        return nonlocal_state[norm(mk_name)][0]
    import json, collections, unicodedata
    FHD = {}; STR = {}
    for g in ('FH2', 'FH'):
        dbp = os.path.join(a.fh_root, g, 'media', 'db', 'gamedb.slt')
        c_ = sqlite3.connect('file:%s?mode=ro' % dbp, uri=True); c_.text_factory = lambda b_: b_.decode('latin-1')
        zp = os.path.join(a.fh_root, g, 'media', 'stringtables', 'EN.zip'); zf_ = open(zp, 'rb'); zz_ = zipfile.ZipFile(zp)
        tp_ = os.path.join(a.work, 'dc_%s.str' % g); open(tp_, 'wb').write(forza_zip.read_entry(zf_, zz_.getinfo('Data_Car.str')))
        STR[g] = {h_: t_ for k_, h_, t_ in forza_str.read(tp_) if k_.startswith('IDS_ModelShort_')}
        for mn, yr, ms, mass in c_.execute('select c.MediaName, c.Year, c.ModelShort, w.Mass from Data_Car c left join List_Wheels w on lower(w.MediaName)=lower(c.MediaName)'):
            FHD.setdefault(mn.lower(), (yr, ms, g, mass))
    lst = json.load(open(a.list)); grp = collections.OrderedDict()
    for k, v in sorted(lst.items(), key=lambda kv: kv[0].split(':')[1]):
        if 'err' in v or v.get('inFM4hash') or not v.get('db'): continue
        grp.setdefault(v['hash'], []).append((k, v))
    reps = [vs[0] for vs in grp.values()]
    if a.limit: reps = reps[:a.limit]
    FHW = {}
    for g in ('FH', 'FH2'):
        c_ = sqlite3.connect('file:%s?mode=ro' % os.path.join(a.fh_root, g, 'media', 'db', 'gamedb.slt'), uri=True)
        for mn, mass in c_.execute('select MediaName, Mass from List_Wheels'): FHW.setdefault(mn.lower(), (mn, mass))
    for key, v in reps:
        g, w = key.split(':'); media, mass = FHW[w]
        if media.lower() in used_media: print('skip (already installed):', media); continue
        while wid in used_ids: wid += 1
        zpath = v['path']; zf_ = open(zpath, 'rb'); zz_ = zipfile.ZipFile(zf_)
        out_dir = 'media/wheels/%s/' % media
        have = {}
        for i in zz_.infolist():
            fn = i.filename.lower()
            if fn.endswith('.xds') or fn.endswith('.carbin'): have[fn] = forza_zip.read_entry(zf_, i)
        if media.lower() + '.carbin' not in have: raise SystemExit('%s: no carbin in %s (%s)' % (media, zpath, sorted(have)))
        rs = lst.get(g + ':' + w + '_rightside') or lst.get('FH2:' + w + '_rightside') or lst.get('FH:' + w + '_rightside')
        if rs and 'path' in rs:
            rzf = open(rs['path'], 'rb'); rz = zipfile.ZipFile(rzf)
            for i in rz.infolist():
                if i.filename.lower().endswith('_rightside.carbin'): have[media.lower() + '_rightside.carbin'] = forza_zip.read_entry(rzf, i)
        for fn, d_ in sorted(have.items()):
            if fn.endswith('.carbin'):
                d_, st_ = carbin.convert(d_)          # FH carbin -> FM4 layout (same converter as port_dlc_car.py)
            entries.append(stored(out_dir + fn, d_))
        face = decode_dxt5_xds(have['wheel0.xds'])
        entries.append(stored(ICON_DIR + media + '.xds', make_icon_xds(face, icon_tmpl)))
        info = FHD.get(w); disp = media.replace('_', ' ')
        if info and info[1] and info[1].startswith('_&') is False:
            disp = info[1]
        if info and info[1] is not None:
            ms = STR[info[2]].get(int(str(info[1])[2:]) & 0xFFFF) if str(info[1]).startswith('_&') else info[1]
            if ms:
                yr = int(info[0]) if info[0] else 0
                disp = ms + (" '%02d" % (yr % 100) if yr and not re.search(r"'\d\d$", ms) else '')
        disp = unicodedata.normalize('NFKD', disp).encode('ascii', 'ignore').decode().replace('  ', ' ').strip()
        row = dict(trow); row.update(ID=wid, MediaName=media, DisplayName=disp + a.suffix, IconPath=media, IsStock=0,
                                     PartManufacturerID=brand_id, Mass=round(mass, 2), Price=3600, BrakeDrawOffset=0.0)
        sql.append('INSERT INTO List_Wheels (%s) VALUES (%s);' % (','.join(cols), ','.join(q(row[c]) for c in cols)))
        print('wheel', wid, media, '|', row['DisplayName'], '|', g, '+rs' if rs and 'path' in rs else '', len(have), 'files')
        used_ids.add(wid); used_media.add(media.lower()); wid += 1
    sql.insert(0, 'INSERT INTO List_PartManufacturer (Id, PartManufacturer, LogoPath) VALUES (%d, %s, %s);' % (brand_id, q(a.brand), q(a.brand_logo)))
    print('brand maker', brand_id, a.brand, 'logo', a.brand_logo)
    lt = forza_zip.read_entry(ui_f, ui.getinfo('UI4/Logos/Car_Parts_Logos/decal052.xds'))
    ls = forza_zip.read_entry(ui_f, ui.getinfo('UI4/Logos/Car_Parts_Logos_SM/decal052.xds'))
    def dims(t):
        z_ = struct.unpack('>I', t[0x24:0x28])[0]; return ((z_ & 0x1FFF) + 1, ((z_ >> 13) & 0x1FFF) + 1)
    entries.append(stored('media/UI/Textures/UI4/Logos/Car_Parts_Logos/%s.xds' % a.brand_logo, encode32(make_logo(dims(lt), True), lt)))
    entries.append(stored('media/UI/Textures/UI4/Logos/Car_Parts_Logos_SM/%s.xds' % a.brand_logo, encode32(make_logo(dims(ls), False), ls)))
    make_logo(dims(lt), True).save(os.path.join(a.work, 'brand_logo.png'))

    # dry run on a copy of the base DB
    chk = os.path.join(a.work, 'check.slt'); shutil.copyfile(os.path.join(a.fm4_media, 'db', 'gamedb.slt'), chk)
    C = sqlite3.connect(chk)
    for s in sql: C.execute(s)
    print('dry run ok:', C.execute('select count(*) from List_Wheels').fetchone()[0], 'wheels'); C.close()

    # ---- merge.slt with the ContentOffers row
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
    os.makedirs(pdz, exist_ok=True)
    entries = [stored('media/db/patch/%d000_merge.slt' % a.offer, open(out_slt, 'rb').read()),
               stored('media/db/patch/%d_execute.sql' % pri, '\r\n'.join(sql).encode('ascii'))] + entries
    open(os.path.join(pm, '%d.puboffer' % a.offer), 'wb').write(b' \r\n')
    write_zip(os.path.join(pdz, '%d_pri_65.zip' % a.offer), entries)
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='\r\n').write('<?xml version="1.0" ?> \n<zipmount> \n</zipmount> \n')
    open(os.path.join(pdz, 'stringtables.xml'), 'w', newline='\r\n').write('<?xml version="1.0" ?> \n<stringtables> \n</stringtables> \n')

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
    print('offer', a.offer, a.offer_id, ' patch', pri, ' zip entries', len(entries))


if __name__ == '__main__':
    main()
