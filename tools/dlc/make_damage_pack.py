#!/usr/bin/env python3
"""make_damage_pack.py - FM4 add-on pack "Damage Plus": stronger visual damage without touching base files.

Overrides two kinds of file through high-priority zips (same mechanism as make_dealer_thumbs_pack.py):
  * media/cars/Shared/GlobalCarAttribs.xml, <DamageAttribs>: the GLOBAL visual damage scales for every car
      MorphDamageScale (stock 6.0)                         dent depth, chase/replay views
      HoodViewAndCockpitViewMorphDamageScale (stock 6.0)   dent depth, bonnet/cockpit views
      TextureDamageScale (stock 1.0)                       scratch/scuff strength
      HoodViewAndCockpitViewTextureDamageScale (stock 0.03) scratch strength seen from bonnet/cockpit
  * media/cars/<CAR>/carattribs.xml, <PartDropValues>: which parts can fall off and how easily (per car)

  python3 make_damage_pack.py --fm4-media <Media> --fm4-user <.local/user> --out <staging> --work <scratch>
        [--morph 10] [--morph-cockpit 10] [--tex 1.6] [--tex-cockpit 0.3]
        [--drop-cars ALF_8C_08] [--drops "bumperF=0.15,bumperR=0.12,..."] [--install]

Needs libfzlzx.so ($FZLZX). Remove = move the package folder and its .header out of the user dir.
"""
import argparse, glob, hashlib, os, re, shutil, sqlite3, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import forza_zip
from port_dlc_car import write_zip, stored, TITLE

PRI = 27100
DEFAULT_DROPS = ('bumperF=0.15,bumperR=0.12,wing=0.15,wingrace=0.15,exhaust=0.15,exhaustL=0.15,'
                 'exhaustR=0.15,mirrorL=0.05,mirrorR=0.05')


def read_member(zpath, name):
    with open(zpath, 'rb') as f:
        z = zipfile.ZipFile(zpath)
        info = next(i for i in z.infolist() if i.filename.lower() == name.lower())
        return forza_zip.read_entry(f, info).decode('utf-8')


def set_attr(xml, tag, attr, value):
    pat = re.compile(r'(<%s\b[^>]*\b%s=")[^"]*(")' % (tag, attr))
    new, n = pat.subn(lambda m: m.group(1) + ('%.6f' % value) + m.group(2), xml)
    if n != 1: raise SystemExit('%s.%s found %d times' % (tag, attr, n))
    return new


SCALAR_MULT = (1.0, 1.0)   # (radius, damage) multipliers for every <DamageScalars>, set from --radius-mult/--damage-mult


STD_SCALARS = ('<DamageScalars speed="5.000000" radius="0.300000" damage="0.500000" />\n    '
               '<DamageScalars speed="35.000000" radius="1.300000" damage="1.500000" />\n    '
               '<DamageScalars speed="98.000000" radius="3.200000" damage="2.500000" />')


def scale_scalars(xml):
    # some cars ship all-zero DamageScalars (speed 0, radius 0, damage 0) = no dents at all: give them the standard curve
    sc = re.findall(r'<DamageScalars [^>]*/>', xml)
    if sc and all(re.search(r'radius="0(\.0*)?"', t) and re.search(r'damage="-?0(\.0*)?"', t) for t in sc):
        xml = re.sub(r'<DamageScalars [^>]*/>(\s*<DamageScalars [^>]*/>)*', lambda _: STD_SCALARS, xml, count=1)
    rm, dm = SCALAR_MULT
    if rm == 1.0 and dm == 1.0: return xml
    def f(mm):
        t = mm.group(0)
        t = re.sub(r'(radius=")([-0-9.]+)(")', lambda q: q.group(1) + '%.6f' % (float(q.group(2)) * rm) + q.group(3), t)
        t = re.sub(r'(damage=")([-0-9.]+)(")', lambda q: q.group(1) + '%.6f' % (float(q.group(2)) * dm) + q.group(3), t)
        return t
    return re.sub(r'<DamageScalars [^>]*/>', f, xml)


STD_SCRAPE = ('<DamageScrape ScrapeMinSpeed="0.010000" ScrapeMaxSpeed="0.300000" ScrapeRadius="1.300000" '
              'ScrapeDamage="0.080000" DamageBias="0.750000" ScrapeBias="0.700000" GlassDamageCausingBreak="0.005000" />')
STD_DAMAGE = ('<Damage Version="3" value="1.000000">\n    ' + STD_SCALARS + '\n    <PartDropValues>\n    </PartDropValues>\n'
              '    <PartDropGlobalScalar value="1.000000" />\n    ' + STD_SCRAPE + '\n  </Damage>')


def _nums(tag):
    out = []
    for v in re.findall(r'="(-?[0-9.]+(?:e[-+]?[0-9]+)?)"', tag):
        try: out.append(float(v))
        except ValueError: out.append(float('nan'))
    return out


def normalise_damage(xml):
    """Give every car a complete, sane <Damage> block: missing or self-closed -> standard block;
    garbage DamageScalars (all-zero, zero damage, huge radius) -> standard curve; garbage DamageScrape -> standard."""
    if re.search(r'<Damage\b[^>]*/>', xml):
        xml = re.sub(r'<Damage\b[^>]*/>', lambda _: STD_DAMAGE, xml, count=1)
    elif not re.search(r'<Damage\b', xml):
        if '</CarAttributes' not in xml: return None
        xml = xml.replace('</CarAttributes', '  ' + STD_DAMAGE + '\n</CarAttributes', 1)
    sc = re.findall(r'<DamageScalars [^>]*/>', xml)
    def bad_sc(t):
        sp, r, d = (_nums(t) + [0, 0, 0])[:3]
        return not (0 < r < 50) or not (0 < d < 50)
    if len(sc) != 3 or any(bad_sc(t) for t in sc):
        if sc:
            xml = re.sub(r'<DamageScalars [^>]*/>(\s*<DamageScalars [^>]*/>)*', lambda _: STD_SCALARS, xml, count=1)
        else:
            xml = re.sub(r'(<Damage\b[^>]*>)', lambda m: m.group(1) + '\n    ' + STD_SCALARS, xml, count=1)
    m = re.search(r'<DamageScrape [^>]*/>', xml)
    if m:
        v = _nums(m.group(0))
        if any(x != x or abs(x) > 1000 for x in v) or (len(v) > 2 and v[2] == 0):
            xml = xml.replace(m.group(0), STD_SCRAPE, 1)
    elif '</Damage>' in xml:
        xml = xml.replace('</Damage>', '  ' + STD_SCRAPE + '\n  </Damage>', 1)
    return xml


def car_attribs(xml, drops, merge=False):
    """Set the part-drop list. merge=True keeps the car's other entries (glass, lights...) and only
    sets/adds the given parts. Also forces PartDropGlobalScalar 1 and repairs a garbage Damage value."""
    want = dict(drops)
    xml = normalise_damage(xml)
    if xml is None:
        return None
    m = re.search(r'<PartDropValues>(.*?)</PartDropValues>', xml, re.S)
    old = re.findall(r'partName="([^"]+)" dropValue="([^"]+)"', m.group(1)) if m else []
    items = [(n, float(v)) for n, v in old if not (merge and n in want)] if merge else []
    items += list(want.items()) if merge else list(drops)
    body = '\n'.join('      <partDrop partName="%s" dropValue="%.6f" />' % (p, v) for p, v in items)
    block = '<PartDropValues>\n%s\n    </PartDropValues>' % body
    if m or re.search(r'<PartDropValues\s*/>', xml):
        xml = re.sub(r'<PartDropValues>.*?</PartDropValues>|<PartDropValues\s*/>', lambda _: block, xml, count=1, flags=re.S)
    else:
        xml = xml.replace('</Damage>', '    ' + block + '\n  </Damage>', 1)
    if re.search(r'<PartDropGlobalScalar value="[^"]*"', xml):
        xml = re.sub(r'(<PartDropGlobalScalar value=")[^"]*(")', r'\g<1>1.000000\2', xml, count=1)
    else:
        xml = xml.replace('</PartDropValues>', '</PartDropValues>\n    <PartDropGlobalScalar value="1.000000" />', 1)
    def fixval(mm):
        try: ok = 0 < float(mm.group(2)) < 100
        except ValueError: ok = False
        return mm.group(0) if ok else mm.group(1) + '1.000000' + mm.group(3)
    xml = re.sub(r'(<Damage\b[^>]*\bvalue=")([^"]*)(")', fixval, xml, count=1)
    return scale_scalars(xml)


def morph_fill_mounts(a, cars_dir, pkgs_dir, pkg, parent_entries):
    import json, time
    import fill_morph as FM
    t0 = time.time()
    # car folder -> (car, zip, {lower member name: real name}, prefix); later sources win (packs over base)
    src = {}
    for zp in sorted(glob.glob(os.path.join(cars_dir, '*.zip'))):
        try: names = zipfile.ZipFile(zp).namelist()
        except zipfile.BadZipFile: continue
        car = os.path.basename(zp)[:-4]
        if any(n.lower() == car.lower() + '.carbin' for n in names):
            src[car.lower()] = (car, zp, {n.lower(): n for n in names}, '', 'base')
    for zp in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in zp: continue
        try: names = zipfile.ZipFile(zp).namelist()
        except zipfile.BadZipFile: continue
        per = {}
        for n in names:
            mm = re.match(r'media/cars/([^/]+)/(.+)$', n, re.I)
            if mm: per.setdefault(mm.group(1), {})[mm.group(2).lower()] = n
        for car, mem in per.items():
            if car.lower() + '.carbin' in mem:
                src[car.lower()] = (car, zp, mem, '', 'pack')
    def read(car_l, rel):
        car, zp, mem, _, _ = src[car_l]
        n = mem.get(rel.lower())
        if n is None: return None
        with open(zp, 'rb') as f: return forza_zip.read_entry(f, zipfile.ZipFile(zp).getinfo(n))
    cache = {}
    if a.morph_cache and os.path.exists(a.morph_cache): cache = json.load(open(a.morph_cache))
    def needs(car_l):
        car, zp, mem, _, _ = src[car_l]
        k = '%s|%s|%d' % (car_l, os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(zp)))), os.path.getsize(zp))
        if k not in cache:
            try: cache[k] = FM.needs_fill(read(car_l, car + '.carbin'))
            except Exception as e: cache[k] = 'ERR %s' % e
        return cache[k]
    targets = [c for c, v in src.items() if v[4] == 'pack' and needs(c) is True]
    if a.morph_cache: json.dump(cache, open(a.morph_cache, 'w'))
    print('morph scan: %d pack cars without damage-morph data (%.0fs): %s' % (len(targets), time.time() - t0, sorted(src[c][0] for c in targets)))
    if a.morph_scan_only: return []
    dims = {}
    for c, v in src.items():
        if c in targets: continue
        mx = read(c, 'physics/maxdata.xml')
        dims[v[0]] = FM.bbox_dims(mx.decode('utf-8', 'ignore')) if mx else None
    over = dict(x.split('=') for x in a.morph_donors.split(',') if '=' in x)
    out = []; report = {}
    for c in sorted(targets):
        car = src[c][0]
        mx = read(c, 'physics/maxdata.xml'); td = FM.bbox_dims(mx.decode('utf-8', 'ignore')) if mx else None
        entries = []; used = None
        for donor in FM.pick_donor(car, td, dims, over)[:4]:
            dl = donor.lower()
            if dl not in src or needs(dl) is not False: continue
            entries = []
            for suf in FM.CAR_FILES:
                tb = read(c, car + suf + '.carbin'); db = read(dl, src[dl][0] + suf + '.carbin')
                if tb is None or db is None: continue
                ob, rep = FM.fill(tb, db)
                if ob: entries.append(stored(src[c][2][(car + suf + '.carbin').lower()].split('/')[-1], ob))
            if entries: used = src[dl][0]; break
        if not entries:
            report[car] = 'no usable donor'; continue
        ca = next((e for e in parent_entries if e[0].lower() == c + '/carattribs.xml'), None)
        if ca: entries.append(stored('carattribs.xml', ca[4]))
        report[car] = used
        out.append(('DmgM%s_pri_%d.zip' % (re.sub(r'[^A-Za-z0-9]', '', car), PRI), 'game:\\Media\\Cars\\%s\\' % car, entries))
    if a.morph_cache: json.dump(cache, open(a.morph_cache, 'w'))
    print('morph fill (%.0fs):' % (time.time() - t0), report)
    open(os.path.join(a.work, 'morph_fill_report.json'), 'w').write(json.dumps(report, indent=1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--morph', type=float, default=10.0); ap.add_argument('--morph-cockpit', type=float, default=10.0)
    ap.add_argument('--tex', type=float, default=1.6); ap.add_argument('--tex-cockpit', type=float, default=0.3)
    ap.add_argument('--drop-cars', default='ALF_8C_08'); ap.add_argument('--drops', default=DEFAULT_DROPS)
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--morph-fill', action='store_true',
                    help='cars in installed packs whose model has no damage-morph data get a baseline copied from a similar car')
    ap.add_argument('--morph-donors', default='', help='CAR=DONOR,... overrides the automatic donor choice')
    ap.add_argument('--morph-cache', default='', help='json cache of the morph scan (speeds up reruns)')
    ap.add_argument('--morph-scan-only', action='store_true', help='only fill the morph cache, then stop')
    ap.add_argument('--db-sql', default='',
                    help='SQL run on a copy of the base gamedb.slt, shipped as an override at game:\\Media\\db\\ '
                         '(DLC merges are INSERT OR IGNORE, so they cannot change existing base rows)')
    ap.add_argument('--radius-mult', type=float, default=1.0, help='multiply every car\'s DamageScalars radius (dent size)')
    ap.add_argument('--damage-mult', type=float, default=1.0, help='multiply every car\'s DamageScalars damage (dent strength)')
    ap.add_argument('--all-cars', action='store_true',
                    help='every car (base + installed packs) gets --all-drops merged into its list, in one zip at game:\\Media\\Cars\\; '
                         'also overrides the fallback media\\cars\\CarAttribs.xml')
    ap.add_argument('--all-drops', default='bumperF=0.25,bumperR=0.2,exhaust=0.25,exhaustL=0.25,exhaustR=0.25,'
                    'wing=0.2,wingrace=0.2,mirrorL=0.08,mirrorR=0.08')
    ap.add_argument('--canary-body-scale', type=float, default=0.0,
                    help='test only: set the driver BodyScale in each drop car, to see in game that the car override loads')
    ap.add_argument('--parent-mount', action='store_true',
                    help='also mount the car files from one zip at game:\\Media\\Cars\\ (CAR/carattribs.xml)')
    ap.add_argument('--offer', type=int, default=170); ap.add_argument('--offer-id', default='4D530910F4FFAA00')
    ap.add_argument('--pack-name', default='Damage Plus')
    a = ap.parse_args()
    global SCALAR_MULT; SCALAR_MULT = (a.radius_mult, a.damage_mult)
    os.makedirs(a.work, exist_ok=True)
    pkg = hashlib.sha1(('FM4DAMAGEPLUS:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')
    cars_dir = os.path.join(a.fm4_media, 'cars')

    # ---- global scales
    g = read_member(os.path.join(cars_dir, 'Shared.zip'), 'GlobalCarAttribs.xml')
    for attr, v in (('MorphDamageScale', a.morph), ('HoodViewAndCockpitViewMorphDamageScale', a.morph_cockpit),
                    ('TextureDamageScale', a.tex), ('HoodViewAndCockpitViewTextureDamageScale', a.tex_cockpit)):
        g = set_attr(g, 'DamageAttribs', attr, v)
    mounts = [('DmgShared_pri_%d.zip' % PRI, 'game:\\Media\\Cars\\Shared\\',
               [stored('GlobalCarAttribs.xml', g.encode('utf-8'))])]
    open(os.path.join(a.work, 'GlobalCarAttribs.xml'), 'w').write(g)

    # ---- per-car part drops
    parent_entries = []
    drops = [(p.split('=')[0].strip(), float(p.split('=')[1])) for p in a.drops.split(',') if p.strip()]
    for car in [c.strip() for c in a.drop_cars.split(',') if c.strip()]:
        zp = next(p for p in glob.glob(os.path.join(cars_dir, '*.zip')) if os.path.basename(p).lower() == car.lower() + '.zip')
        car = os.path.basename(zp)[:-4]
        x = car_attribs(read_member(zp, 'carattribs.xml'), drops)
        if a.canary_body_scale:
            x, n = re.subn(r'(<Driver\b[^>]*\bBodyScale=")[^"]*(")', lambda m: m.group(1) + str(a.canary_body_scale) + m.group(2), x)
            if n != 1: raise SystemExit('Driver BodyScale found %d times' % n)
        parent_entries.append(stored('%s/carattribs.xml' % car, x.encode('utf-8')))
        open(os.path.join(a.work, car + '_carattribs.xml'), 'w').write(x)
        mounts.append(('Dmg%s_pri_%d.zip' % (re.sub(r'[^A-Za-z0-9]', '', car), PRI), 'game:\\Media\\Cars\\%s\\' % car,
                       [stored('carattribs.xml', x.encode('utf-8'))]))

    if a.all_cars:
        alld = [(p.split('=')[0].strip(), float(p.split('=')[1])) for p in a.all_drops.split(',') if p.strip()]
        done = {e[0].split('/')[0].lower() for e in parent_entries}
        sources = {}   # car folder (lower) -> (canonical name, zip path, member)
        for zp in sorted(glob.glob(os.path.join(cars_dir, '*.zip'))):
            try: names = zipfile.ZipFile(zp).namelist()
            except zipfile.BadZipFile: continue
            mem = next((n for n in names if n.lower() == 'carattribs.xml'), None)
            if mem: sources[os.path.basename(zp)[:-4].lower()] = (os.path.basename(zp)[:-4], zp, mem)
        for zp in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
            if pkg in zp: continue
            for n in zipfile.ZipFile(zp).namelist():
                mm = re.match(r'media/cars/([^/]+)/carattribs\.xml$', n, re.I)
                if mm: sources[mm.group(1).lower()] = (mm.group(1), zp, n)
        stats = dict(cars=0, nodamage=0, fixed=0)
        for key, (car, zp, mem) in sorted(sources.items()):
            if key in done: continue
            with open(zp, 'rb') as f:
                x0 = forza_zip.read_entry(f, zipfile.ZipFile(zp).getinfo(mem)).decode('utf-8', 'ignore')
            x = car_attribs(x0, alld, merge=True)
            if x is None: stats['nodamage'] += 1; continue
            if 'PartDropGlobalScalar value="0.0' in x0: stats['fixed'] += 1
            parent_entries.append(stored('%s/carattribs.xml' % car, x.encode('utf-8'))); stats['cars'] += 1
        fb = open(os.path.join(cars_dir, 'CarAttribs.xml'), encoding='utf-8-sig').read()
        fb = car_attribs(fb, alld, merge=True)
        fb = re.sub(r'<DamageScalars[^>]*/>\s*<DamageScalars[^>]*/>\s*<DamageScalars[^>]*/>',
                    '<DamageScalars speed="5.000000" radius="0.300000" damage="0.500000"/>\n    '
                    '<DamageScalars speed="35.000000" radius="1.300000" damage="1.500000"/>\n    '
                    '<DamageScalars speed="98.000000" radius="3.200000" damage="2.500000"/>', fb, count=1)
        fb = re.sub(r'<DamageScrape [^>]*/>', '<DamageScrape ScrapeMinSpeed="0.010000" ScrapeMaxSpeed="0.300000" '
                    'ScrapeRadius="1.300000" ScrapeDamage="0.080000" DamageBias="0.750000" ScrapeBias="0.700000" '
                    'GlassDamageCausingBreak="0.005000"/>', fb, count=1)
        parent_entries.append(stored('CarAttribs.xml', fb.encode('utf-8')))
        open(os.path.join(a.work, 'fallback_CarAttribs.xml'), 'w').write(fb)
        print('all-cars:', stats, 'entries', len(parent_entries))
        a.parent_mount = True
    if a.parent_mount and parent_entries:
        mounts.append(('DmgCars_pri_%d.zip' % PRI, 'game:\\Media\\Cars\\', parent_entries))

    # guard: another installed pack overriding the same DB folder would clash with ours (project rule)
    for zm in glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', 'zipmount.xml')):
        if pkg in zm: continue
        if re.search(r'Mount="game:\\Media\\db\\?"', open(zm, errors='ignore').read(), re.I):
            print('WARNING: %s also overrides game:\\Media\\db\\ - merge its changes into --db-sql instead' % zm)
    if a.db_sql:
        dbp = os.path.join(a.work, 'gamedb.slt')
        shutil.copyfile(os.path.join(a.fm4_media, 'db', 'gamedb.slt'), dbp)
        D = sqlite3.connect(dbp); D.executescript(a.db_sql); D.commit()
        print('db patch:', D.total_changes, 'row changes')
        D.close()
        mounts.append(('DmgDB_pri_%d.zip' % PRI, 'game:\\Media\\db\\', [stored('gamedb.slt', open(dbp, 'rb').read())]))

    if a.morph_fill or a.morph_scan_only:
        mounts += morph_fill_mounts(a, cars_dir, pkgs_dir, pkg, parent_entries)
        if a.morph_scan_only: return

    # ---- ContentOffers row (clone of an installed offer), as in make_dealer_thumbs_pack.py
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
        if a.offer in {r[0] for r in d.execute('select PubOfferingID from ContentOffers')}: raise SystemExit('offer number used')
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
    lines = []
    for name, mount, entries in mounts:
        write_zip(os.path.join(pdz, name), entries)
        lines.append('   <zip Name="%s" Mount="%s" AltRootPath="%s" ShouldCache="0" /> \r\n' % (name, mount, mount))
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='').write(
        '<?xml version="1.0" ?> \r\n<zipmount> \r\n' + ''.join(lines) + '</zipmount> \r\n')

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
