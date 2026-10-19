#!/usr/bin/env python3
"""port_dlc_car.py - rebuild a Forza Horizon (or other Forza) car DLC pack
(one or more cars) as an FM4 DLC pack with non-conflicting IDs, and
optionally install it straight into the recomp's user content folder.

  python3 port_dlc_car.py <extracted FH package dir (contains Media/)>
      --fm4-db  <FM4 Media/db/gamedb.slt>
      --fm4-user <.local/user dir>            (installed FM4 DLC: merges + string tables)
      --out <staging dir>  --work <local scratch dir>  [--install]
      [--car-id 1700 --engine-id 1100 --drivetrain-id 1050 --mapping-id 1900
       --offer 70 --offer-id 4D530910F4FF7000 --str-pri 25100]

  Multi-car packs: the --*-id values are bases; cars/engines/drivetrains/
  mapping rows get consecutive IDs in source-ID order.

What it does
  * main zip  <offer>_pri_65.zip : FH entries copied raw (LZX data untouched),
    except .carbin models, which are transcoded FH -> FM4 by carbin.py and stored,
    renamed to FM4 conventions (lowercase file names, digitalgauge/,
    liverymasks/, physics/maxdata.xml, _lod0), FH-only files dropped
    (stripped_*.carbin, physicsdefinition.bin), versiondata.xml added,
    thumbnails renamed, merge.slt rebuilt. HarmonicTuning xmls that the pack
    ships in a loose HarmonicTuning_pri_N folder and FM4 lacks are added.
  * loose <src offer>_pri_65/ folder (new engine .fsb banks) copied as
    <offer>_pri_65/ (FM4 DLC layout).
  * merge.slt : FM4 schema (template = an installed FM4 DLC merge), FH rows
    mapped by column name, car/engine/drivetrain IDs remapped, new
    ContentOffers + ContentOffersMapping rows, new string hashes.
  * StringTables_pri_<N>.zip : the highest-priority FM4 Data_Car.str and
    ContentOffers.str for every language + the new car/offer strings
    (FM4 string tables are cumulative; highest priority file wins).
  * zipmount.xml, stringtables.xml, <offer>.puboffer, and an XContent
    .header cloned from an installed FM4 DLC header.
"""
import argparse, glob, hashlib, os, re, shutil, sqlite3, struct, sys, zipfile, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip, forza_str, forza_str_write as SW, carbin

TITLE = '4D530910'
XTRA = 0x1123  # Forza zip central-dir extra: u32 absolute data offset

# ---------------------------------------------------------------- zip I/O
def raw_entries(zp):
    zf = zipfile.ZipFile(zp)
    with open(zp, 'rb') as f:
        for i in zf.infolist():
            yield i, forza_zip.raw_entry(f, i)

def write_zip(path, entries):
    """entries: list of (name, method, crc, usize, raw_bytes)"""
    cd = bytearray(); off = 0
    with open(path, 'wb') as f:
        for name, meth, crc, usize, raw in entries:
            n = name.encode('ascii')
            lh = struct.pack('<IHHHHHIIIHH', 0x04034b50, 10, 0, meth, 0, 0x21,
                             crc, len(raw), usize, len(n), 0)
            data_off = off + len(lh) + len(n)
            f.write(lh); f.write(n); f.write(raw)
            ex = struct.pack('<HHI', XTRA, 4, data_off)
            cd += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 10, 10, 0, meth, 0, 0x21,
                              crc, len(raw), usize, len(n), len(ex), 0, 0, 0, 0, off) + n + ex
            off = data_off + len(raw)
        f.write(cd)
        f.write(struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(entries), len(entries),
                            len(cd), off, 0))

def stored(name, data):
    return (name, 0, zlib.crc32(data) & 0xffffffff, len(data), data)


# ------------------------------------------------------------- name rules
def fm4_name(name, cars, drop_gauge=False):
    """cars: {lowercase media name: canonical media name}"""
    p = name.split('/')
    low = [x.lower() for x in p]
    if len(p) >= 3 and low[0] == 'media' and low[1] in ('cars', 'wheels') and low[2] in cars:
        rest = [x.lower() for x in p[3:]]
        if rest and (rest[-1].startswith('stripped_') or rest[-1] == 'physicsdefinition.bin'):
            return None
        if drop_gauge and rest and rest[0] == 'digitalgauge':
            return None
        return '/'.join(['media', low[1], cars[low[2]]] + rest)
    return name

# --------------------------------------------------------------- database
def remap_value(table, col, v, M):
    if not isinstance(v, int) or isinstance(v, bool):
        return v
    c = col.lower()
    for old, new, exact_cols in M:
        if old * 1000 <= v <= old * 1000 + 999:
            return new * 1000 + (v - old * 1000)
        if v == old and c in exact_cols:
            return new
    return v


def apply_db_donor(out_slt, fm4_db, donor_car, P, only=None):
    """TEST helper: replace the ported car's car/engine/drivetrain-owned rows by a stock FM4 car's, remapped to the new ids."""
    O = sqlite3.connect(out_slt); F = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
    (oc, nc), = P['car'].items(); (oe, ne), = P['engine'].items(); (od, nd), = P['drivetrain'].items()
    de = F.execute('select EngineID from List_UpgradeEngine where Ordinal=? and IsStock=1', (donor_car,)).fetchone()[0]
    dd = F.execute('select DrivetrainID from List_UpgradeDrivetrain where Ordinal=? and IsStock=1', (donor_car,)).fetchone()[0]
    M = [(donor_car, nc, {'id', 'carid', 'ordinal', 'modelid', 'contentid'}), (de, ne, {'engineid'}), (dd, nd, {'drivetrainid'})]
    keep = ('Id', 'DisplayName', 'ModelShort', 'MediaName', 'Thumbnail', 'StockWheelID', 'ModelId', 'ContentId', 'Year', 'MakeID', 'MakeName', 'MakeIcon')
    for (t,) in list(O.execute("select name from sqlite_master where type='table'")):
        if t in ('ContentOffers', 'ContentOffersMapping', 'List_Wheels'): continue
        if only and not any(t.startswith(p) if not p.startswith('!') else not t.startswith(p[1:]) for p in only): continue
        cols = [r[1] for r in O.execute('pragma table_info("%s")' % t)]
        if not O.execute('select count(*) from "%s"' % t).fetchone()[0] and t != 'Data_Car': 
            pass
        lc = [c.lower() for c in cols]
        def owned(r, car, eng, dt):
            d = dict(zip(lc, r)); first = r[0]
            if t == 'Data_Car': return d['id'] == car
            if t == 'Data_Engine': return d['engineid'] == eng
            if t == 'Data_Drivetrain': return d['drivetrainid'] == dt
            if any(k in d for k in ('ordinal', 'carid', 'modelid')): return any(d.get(k) == car for k in ('ordinal', 'carid', 'modelid') if k in d)
            if 'engineid' in d: return d['engineid'] == eng
            if 'drivetrainid' in d: return d['drivetrainid'] == dt
            return isinstance(first, int) and first >= 1000 and first // 1000 in (car, eng, dt)
        try: fcols = [r[1] for r in F.execute('pragma table_info("%s")' % t)]
        except sqlite3.Error: continue
        if fcols != cols: continue
        old_rows = [r for r in O.execute('select * from "%s"' % t) if owned(r, nc, ne, nd)]
        new_rows = [r for r in F.execute('select * from "%s"' % t) if owned(r, donor_car, de, dd)]
        if t == 'Data_Car':
            base = dict(zip(cols, old_rows[0]))
            nr = dict(zip(cols, new_rows[0])); nr = {c: remap_value(t, c, v, M) for c, v in nr.items()}
            for k in keep:
                if k in base: nr[k] = base[k]
            new_rows = [tuple(nr[c] for c in cols)]
        else:
            new_rows = [tuple(remap_value(t, c, v, M) for c, v in zip(cols, r)) for r in new_rows]
        if not old_rows and not new_rows: continue
        for r in old_rows:
            O.execute('delete from "%s" where rowid in (select rowid from "%s" where %s)' % (t, t, ' and '.join('"%s" is ?' % c for c in cols)), r)
        for r in new_rows:
            O.execute('insert into "%s" values (%s)' % (t, ','.join('?' * len(cols))), r)
    O.commit(); O.execute('vacuum'); O.close()

def build_merge(src_slt, template_slt, out_slt, a, P):
    """P: plan dict with car/engine/drivetrain maps and per-car strings."""
    shutil.copyfile(template_slt, out_slt)
    O = sqlite3.connect(out_slt); S = sqlite3.connect(src_slt)
    T = sqlite3.connect(a.fm4_db)
    def fm4_has(tab, key, v):
        return T.execute('select count(*) from "%s" where "%s"=?' % (tab, key), (v,)).fetchone()[0] > 0
    src_tables = [r[0] for r in S.execute("select name from sqlite_master where type='table'")]
    out_tables = [r[0] for r in O.execute("select name from sqlite_master where type='table'")]
    missing = [t for t in src_tables if t not in out_tables]
    missing += [t for t in src_tables if t.startswith(DROP_TABLE_PREFIX) and t in out_tables]
    src_tables = [t for t in src_tables if t in out_tables and not t.startswith(DROP_TABLE_PREFIX)]   # FH-only tables (no FM4 equivalent) are dropped
    for t in out_tables:
        if t in src_tables: O.execute('delete from "%s"' % t)
        else: O.execute('drop table "%s"' % t)
    M = ([(o, n, {'id', 'carid', 'ordinal', 'modelid', 'contentid'}) for o, n in P['car'].items()] +
         [(o, n, {'engineid'}) for o, n in P['engine'].items()] +
         [(o, n, {'drivetrainid'}) for o, n in P['drivetrain'].items()])
    report = {'dropped_cols': {}, 'dropped_tables': missing}
    offer_int = int(a.offer_id, 16)
    mapping_rows = 0
    for t in src_tables:
        scols = [r[1] for r in S.execute('pragma table_info("%s")' % t)]
        ocols = [r[1] for r in O.execute('pragma table_info("%s")' % t)]
        report['dropped_cols'][t] = [c for c in scols if c not in ocols]
        lost = [c for c in ocols if c not in scols]
        fill = {}
        if lost:
            # source game predates these FM4 columns (e.g. FM3): fill from an FM4 donor row / column default / zero
            tinfo = {r[1]: (r[2], r[4]) for r in O.execute('pragma table_info("%s")' % t)}
            don = None
            if t == 'Data_Car' and getattr(a, 'fill_car', None):
                don = T.execute('select * from Data_Car where Id=?', (a.fill_car,)).fetchone()
                dcols = [r[1] for r in T.execute('pragma table_info(Data_Car)')]
                don = dict(zip(dcols, don)) if don else None
            for c in lost:
                typ, dv = tinfo[c]
                if don and c in don: fill[c] = don[c]
                elif (t, c) == ('List_UpgradeDrivetrainTransmission', 'CVTShiftTime'): fill[c] = -1.0     # FM4: -1 = not a CVT (0 makes it a 1-gear CVT)
                elif dv is not None: fill[c] = eval(dv) if dv.lstrip('-').replace('.', '', 1).isdigit() else dv.strip("'")
                else: fill[c] = '' if 'CHAR' in (typ or '').upper() or 'TEXT' in (typ or '').upper() else 0
            report.setdefault('filled_cols', {})[t] = fill
        order = ' order by Id' if t == 'ContentOffersMapping' else ''
        for row in S.execute('select * from "%s"%s' % (t, order)):
            d = dict(zip(scols, row)); d.update(fill)
            if t == 'List_SpringDamperPhysics' and d.get('MaxStretchDeltaFromRideHeight') == 0:   # FM3 left this unset; every FM4 row has 0.5
                d['MaxStretchDeltaFromRideHeight'] = 0.5
            if t == 'List_UpgradeCarBodyChassisStiffness' and 'FrontFrictionScale' in d:   # FM3 single scale -> FM4 lat+long pairs
                d['FrontLatFrictionScale'] = d['FrontLongFrictionScale'] = d['FrontFrictionScale']
                d['RearLatFrictionScale'] = d['RearLongFrictionScale'] = d['RearFrictionScale']
            if t == 'ContentOffers':
                d.update(ID=a.offer_id, OfferId=offer_int, PubOfferingID=a.offer,
                         DisplayName='_&%d' % P['offer_name'], Description='_&%d' % P['offer_desc'],
                         LicenseLevel=1, LicenseMask=1, IsDLC=1, IsInstalled=0, IsPurchased=0,
                         IsVIP=0, Hidden=0, IsALaCarte=0, SeasonPassOffer=0)
            elif t == 'ContentOffersMapping':
                d.update(Id=a.mapping_id + mapping_rows, OfferId=offer_int); mapping_rows += 1
                if d['ContentType'] == 1 and d['ContentId'] in P['car']: d['ContentId'] = P['car'][d['ContentId']]
            else:
                old_id = d.get('Id') if t == 'Data_Car' else None
                d = {c: remap_value(t, c, v, M) for c, v in d.items()}
                if t == 'List_Wheels' and d.get('ID') in P['wheel']: d['ID'] = P['wheel'][d['ID']]
                if t == 'Data_Car' and d.get('StockWheelID') in P['wheel']: d['StockWheelID'] = P['wheel'][d['StockWheelID']]
                if t == 'Data_Car':
                    s = P['car_strings'][old_id]
                    d['DisplayName'] = '_&%d' % s['name']
                    d['ModelShort'] = '_&%d' % s['short']
                    d['Thumbnail'] = 'game:\\Media\\UI\\Textures\\Thumbnails\\Thumbnail_%d.xds' % P['car'][old_id]
                    # FH2/FH ids for family lookups may not exist in FM4 (e.g. FamilyModelID 47) -> clamp to a valid FM4 row
                    for col, tab, key, dflt in (('FamilyModelID', 'List_FamilyModel', 'Id', 1), ('FamilyBodyID', 'List_FamilyBody', 'Id', 11)):
                        try:
                            if col in d and not fm4_has(tab, key, d[col]):
                                report.setdefault('clamped', []).append('Data_Car.%s %s not in FM4 %s -> %s' % (col, d[col], tab, dflt)); d[col] = dflt
                        except sqlite3.Error: pass
                    # PI above every FM4 car (FH2 hypercars) leaves the AI grid builder with no opponents -> race load stalls; cap at FM4's max
                    try:
                        mx = T.execute('select max(PerformanceIndex) from Data_Car').fetchone()[0]
                        if d.get('PerformanceIndex') is not None and d['PerformanceIndex'] > mx:
                            report.setdefault('clamped', []).append('Data_Car.PerformanceIndex %.4f > FM4 max %.4f -> capped' % (d['PerformanceIndex'], mx))
                            d['PerformanceIndex'] = mx
                            d['ClassID'] = T.execute('select Id from CarClasses where MaxPerformanceIndex>=? order by Id limit 1', (mx,)).fetchone()[0]
                    except sqlite3.Error: pass
            if t in ('CarPartPositions', 'Combo_Colors') and not d.get('ID'):   # FM3 tables have no ID column (FM4: car*1000+part/sequence)
                d['ID'] = d['Ordinal'] * 1000 + (d['PartId'] if t == 'CarPartPositions' else d['Sequence'])
            O.execute('insert into "%s" (%s) values (%s)' % (t, ','.join('[%s]' % c for c in ocols),
                      ','.join('?' * len(ocols))), [d[c] for c in ocols])
    O.commit()
    return report

def check_collisions(out_slt, fm4_dbs):
    O = sqlite3.connect(out_slt); hits = []
    for (t,) in O.execute("select name from sqlite_master where type='table'"):
        pk = [r[1] for r in O.execute('pragma table_info("%s")' % t)][0]
        mine = {r[0] for r in O.execute('select "%s" from "%s"' % (pk, t))}
        for db in fm4_dbs:
            if t in {r[0] for r in db.execute("select name from sqlite_master where type='table'")}:
                if pk in [r[1] for r in db.execute('pragma table_info("%s")' % t)]:
                    used = {r[0] for r in db.execute('select "%s" from "%s"' % (pk, t))}
                    if mine & used: hits.append((t, sorted(mine & used)[:5]))
    return hits

# ---------------------------------------------------------- string tables
def latest_tables(user, names, skip=None):
    """{(lang, table): (pri, zip, member)} highest priority installed FM4 copy."""
    best = {}
    for z in glob.glob(os.path.join(user, '0000000000000000', TITLE, '00000002', '*', 'Media', 'DLCZips', 'StringTables_pri_*.zip')):
        if skip and skip in z: continue  # our own previous install
        pri = int(re.search(r'_pri_(\d+)', z).group(1))
        for n in zipfile.ZipFile(z).namelist():
            parts = n.replace('\\', '/').split('/')
            if len(parts) == 2 and parts[1][:-4] in names and parts[1].endswith('.str'):
                k = (parts[0], parts[1][:-4])
                if k not in best or pri > best[k][0]: best[k] = (pri, z, n)
    return best

def read_member(z, n):
    zf = zipfile.ZipFile(z)
    with open(z, 'rb') as f:
        return forza_zip.read_entry(f, zf.getinfo(n))

def free_hashes(tables, count, start=0xF4A0):
    used = set()
    for t in tables: used |= SW.hashes(t)
    out = []; h = start
    while len(out) < count:
        if h not in used and h != 0xFFFF: out.append(h)
        h = (h + 1) & 0xFFFF
    return out


# ------------------------------------------------- skip cars FM4 already has
CAR_COLS = {'ordinal', 'carid', 'modelid'}
CAR_ID_TABLES = {'CarTrackOffsets'}           # first column is the car Id
DROP_TABLE_PREFIX = ('Rewards', 'Livery', 'Event', 'Environments', 'Tracks', 'SplitPoints', 'PittingProfiles')   # career/track data, not cars
CORE_TABLES = {'Data_Car', 'Data_Engine', 'Data_Drivetrain', 'ContentOffers', 'ContentOffersMapping', 'List_Wheels', 'Data_CarBody', 'CarPartPositions', 'Combo_Colors', 'List_TorqueCurve'}               # FH career rewards: not car data, would collide with FM4 rows
def fm4_inventory(fm4_db, merge_files):
    """MediaName -> where it comes from; make IDs; engine IDs; drivetrain IDs known to FM4."""
    inv = {'cars': {}, 'makes': set(), 'engines': set(), 'drivetrains': set()}
    dbs = [('FM4 base game', sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True))]
    for p in merge_files:
        D = sqlite3.connect(p)
        try: offer = D.execute('select ID from ContentOffers').fetchone()
        except sqlite3.Error: offer = None
        label = 'earlier FH port' if offer and str(offer[0]).upper().startswith('4D530910F4FF') else 'FM4 DLC'
        dbs.append((label, D))
    for label, D in dbs:
        tabs = {r[0] for r in D.execute("select name from sqlite_master where type='table'")}
        if 'Data_Car' in tabs:
            for (m,) in D.execute('select MediaName from Data_Car'):
                if m: inv['cars'].setdefault(m.lower(), label)
        if 'List_CarMake' in tabs: inv['makes'] |= {r[0] for r in D.execute('select ID from List_CarMake')}
        if 'Data_Engine' in tabs: inv['engines'] |= {r[0] for r in D.execute('select EngineID from Data_Engine')}
        if 'Data_Drivetrain' in tabs: inv['drivetrains'] |= {r[0] for r in D.execute('select DrivetrainID from Data_Drivetrain')}
    return inv

def filter_source(slt, inv, skip_existing=True):
    """Remove cars FM4 already has (or whose make FM4 lacks) from the source merge, plus their
    stock engines/drivetrains and every row owned by them. Returns (skipped [(media, reason)], notes)."""
    S = sqlite3.connect(slt); notes = []
    cars = S.execute('select Id, MediaName, MakeID from Data_Car').fetchall()
    skip = {}
    for cid, media, make in cars:
        if skip_existing and media.lower() in inv['cars']:
            skip[cid] = (media, 'already in FM4 (%s)' % inv['cars'][media.lower()])
        elif make not in inv['makes']:
            skip[cid] = (media, 'manufacturer (MakeID %d) not in FM4' % make)
    keep = [c[0] for c in cars if c[0] not in skip]
    def owned(table, col, ids):
        try: return {r[0] for r in S.execute('select "%s" from "%s" where Ordinal in (%s) and IsStock=1' % (col, table, ','.join(map(str, ids or [-1]))))}
        except sqlite3.Error: return set()
    skip_eng = owned('List_UpgradeEngine', 'EngineID', list(skip)) - owned('List_UpgradeEngine', 'EngineID', keep)
    skip_dt = owned('List_UpgradeDrivetrain', 'DrivetrainID', list(skip)) - owned('List_UpgradeDrivetrain', 'DrivetrainID', keep)
    skip_media = {v[0].lower() for v in skip.values()}
    tabs = [r[0] for r in S.execute("select name from sqlite_master where type='table'")]
    dropped = 0
    for t in tabs:
        cols = [r[1] for r in S.execute('pragma table_info("%s")' % t)]
        rows = S.execute('select rowid, * from "%s"' % t).fetchall()
        kill = []
        for row in rows:
            d = dict(zip(cols, row[1:]))
            lc = {c.lower(): v for c, v in d.items()}
            first = row[1]
            bad = False
            if t == 'Data_Car': bad = d['Id'] in skip
            elif t == 'ContentOffersMapping': bad = d.get('ContentType') == 1 and d.get('ContentId') in skip
            elif t == 'Data_Engine': bad = d['EngineID'] in skip_eng
            elif t == 'Data_Drivetrain': bad = d['DrivetrainID'] in skip_dt
            elif t == 'List_Wheels': bad = str(d.get('MediaName', '')).lower() in skip_media
            elif 'drivetrain' in t.lower() and 'ordinal' not in lc:
                bad = lc.get('drivetrainid') in skip_dt or \
                      (isinstance(first, int) and first >= 1000 and first // 1000 in skip_dt)
            else:
                bad = any(lc.get(c) in skip for c in CAR_COLS if c in lc) or \
                      ('engineid' in lc and 'ordinal' not in lc and lc['engineid'] in skip_eng) or \
                      (isinstance(first, int) and first >= 1000 and (first // 1000 in skip or first // 1000 in skip_eng)) or \
                      (t in CAR_ID_TABLES and first in skip)
            if bad: kill.append(row[0])
        for r in kill: S.execute('delete from "%s" where rowid=?' % t, (r,))
        dropped += len(kill)
    # range-keyed rows (Id = owner*1000+n) whose owner is not a car/engine/drivetrain of this pack:
    # FH tuning overrides for cars FM4 already ships. Dropped so they can't replace FM4 rows.
    owners = {r[0] for r in S.execute('select Id from Data_Car')} | {r[0] for r in S.execute('select EngineID from Data_Engine')} | \
             {r[0] for r in S.execute('select DrivetrainID from Data_Drivetrain')} | set(skip) | skip_eng | skip_dt
    foreign = {}
    for t in tabs:
        if t in ('Data_Car', 'ContentOffers', 'ContentOffersMapping', 'List_Wheels', 'List_CarMake') or t.startswith(('Livery', 'Rewards')): continue
        cols = [r[1].lower() for r in S.execute('pragma table_info("%s")' % t)]
        if any(c in cols for c in ('ordinal', 'engineid', 'drivetrainid', 'carid')): continue
        for rid, first in S.execute('select rowid, "%s" from "%s"' % (cols[0], t)).fetchall():
            if isinstance(first, int) and first >= 1000 and first // 1000 not in owners:
                S.execute('delete from "%s" where rowid=?' % t, (rid,)); foreign[t] = foreign.get(t, 0) + 1
    if foreign: notes.append('dropped FH overrides for cars/engines outside this pack: %s' % foreign)
    # engine swaps to engines neither this pack nor FM4 has
    pack_eng = {r[0] for r in S.execute('select EngineID from Data_Engine')}
    try:
        bad_swaps = S.execute('select Id, Ordinal, EngineID from List_UpgradeEngine').fetchall()
        bad_swaps = [r for r in bad_swaps if r[2] not in pack_eng and r[2] not in inv['engines']]
        for r in bad_swaps: S.execute('delete from List_UpgradeEngine where Id=?', (r[0],))
        if bad_swaps: notes.append('dropped %d engine-swap upgrades to engines FM4 lacks: %s' % (len(bad_swaps), sorted({r[2] for r in bad_swaps})))
    except sqlite3.Error: pass
    S.commit()
    if skip: notes.append('removed %d DB rows for skipped cars (engines %s, drivetrains %s)' % (dropped, sorted(skip_eng), sorted(skip_dt)))
    return [(cid, m, why) for cid, (m, why) in sorted(skip.items())], notes

class FM4Files:
    """Case-insensitive 'does FM4 already ship this Media/... path' (loose files or inside <dir>.zip)."""
    def __init__(s, media): s.media = media; s.dirs = {}; s.zips = {}
    def _listing(s, parts):
        key = '/'.join(parts).lower()
        if key not in s.dirs:
            cur = s.media; ok = True
            for p in parts:
                try: m = {n.lower(): n for n in os.listdir(cur)}
                except OSError: ok = False; break
                if p.lower() not in m: ok = False; break
                cur = os.path.join(cur, m[p.lower()])
            s.dirs[key] = cur if ok else None
        return s.dirs[key]
    def __call__(s, rel):
        parts = rel.replace('\\', '/').split('/')
        if parts and parts[0].lower() == 'media': parts = parts[1:]
        if s._listing(parts): return True
        for k in range(len(parts) - 1, 0, -1):          # inside an ancestor <dir>.zip ?
            parent = s._listing(parts[:k - 1]) if k > 1 else s.media
            if not parent: continue
            zname = {n.lower(): n for n in os.listdir(parent)}.get(parts[k - 1].lower() + '.zip')
            if zname:
                zp = os.path.join(parent, zname)
                if zp not in s.zips: s.zips[zp] = {n.lower().replace('\\', '/') for n in zipfile.ZipFile(zp).namelist()}
                inner = '/'.join(parts[k:]).lower()
                if inner in s.zips[zp] or any(n.endswith('/' + inner) for n in s.zips[zp]): return True
        return False

KEEP_PREFIX = ('media/cars/', 'media/wheels/', 'media/audio/', 'media/ui/', 'media/db/')

# -------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('--fm4-db', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--install', action='store_true')
    ap.add_argument('--car-id', type=int, default=1700); ap.add_argument('--engine-id', type=int, default=1100)
    ap.add_argument('--drivetrain-id', type=int, default=1050); ap.add_argument('--mapping-id', type=int, default=1900)
    ap.add_argument('--offer', type=int, default=70); ap.add_argument('--offer-id', default='4D530910F4FF7000')
    ap.add_argument('--str-pri', type=int, default=25100)
    ap.add_argument('--display-suffix', default=' (FH)')
    ap.add_argument('--keep-gauge', action='store_true', help='keep a digital gauge even if its .fbf is not FM4 version 0x3EF')
    ap.add_argument('--no-convert', action='store_true', help='copy .carbin models raw (default: transcode FH carbin -> FM4 with carbin.py)')
    ap.add_argument('--brake-donor', help='FM4 car zip whose caliper/rotor .carbin replace the source brakes (single-car packs only)')
    ap.add_argument('--db-donor-car', type=int, help='TEST: FM4 Data_Car Id whose car/engine/drivetrain DB rows replace the ported ones (single-car packs only; keeps identity columns)')
    ap.add_argument('--db-donor-tables', help='comma list of table-name prefixes the DB donor applies to (prefix with ! to exclude); default all')
    ap.add_argument('--fill-car', type=int, help='FM4 Data_Car Id whose values fill Data_Car columns missing in an older source game (FM3)')
    ap.add_argument('--model-donor', help='TEST: FM4 car zip whose .carbin models replace the source car models (single-car packs only)')
    ap.add_argument('--work', help='scratch dir (default: <out>/_work). Use a local dir: SQLite cannot journal on some mounted folders')
    ap.add_argument('--wheel-id', type=int, default=0, help='base for new List_Wheels IDs (0 = keep the source IDs)')
    ap.add_argument('--pack-name', help='offer/pack display name (default: the source ContentOffers string)')
    ap.add_argument('--include-existing', action='store_true', help='also port cars whose MediaName FM4 already has')
    ap.add_argument('--report', help='append a JSON line describing the result to this file')
    ap.add_argument('--header-template', help='installed FM4 .header to clone (default: first DLC header found)')
    a = ap.parse_args()
    work = a.work or os.path.join(a.out, '_work'); os.makedirs(work, exist_ok=True)

    media = os.path.join(a.src, 'Media'); dz = os.path.join(media, 'DLCZips')
    main_zip = next(os.path.join(dz, f) for f in os.listdir(dz) if re.match(r'^[0-9A-Fa-f]+_pri_65\.zip$', f))
    src_str_zip = next((os.path.join(dz, f) for f in os.listdir(dz) if f.lower().startswith('stringtables') and f.lower().endswith('.zip')), None)
    infos = list(zipfile.ZipFile(main_zip).infolist())
    slt_info = next(i for i in infos if i.filename.lower().endswith('_merge.slt'))
    with open(main_zip, 'rb') as f:
        open(os.path.join(work, 'src_merge.slt'), 'wb').write(forza_zip.read_entry(f, slt_info))
    S0 = sqlite3.connect(os.path.join(work, 'src_merge.slt'))
    first_car = S0.execute('select MediaName from Data_Car order by Id').fetchone()[0]; S0.close()
    pkg = hashlib.sha1(('FM4PORT:%s:%s' % (first_car, a.offer_id)).encode()).hexdigest().upper() + '4D'
    merges = sorted(glob.glob(os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002', '*', 'Media', 'DLCZips', '*_pri_65.zip')))
    merges = [z for z in merges if pkg not in z]
    fm4_merge_files = []
    for z in merges:
        for i in zipfile.ZipFile(z).infolist():
            if i.filename.lower().endswith('_merge.slt'):
                p = os.path.join(work, 'fm4_%s_%s' % (os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(z))))[:8], os.path.basename(i.filename)))
                if not os.path.exists(p):
                    with open(z, 'rb') as f: open(p, 'wb').write(forza_zip.read_entry(f, i))
                fm4_merge_files.append(p)
    import time; T0 = time.time()
    def prog(msg): print('[%6.1fs] %s' % (time.time() - T0, msg), file=sys.stderr, flush=True)
    prog('merges extracted (%d)' % len(fm4_merge_files))
    inv = fm4_inventory(a.fm4_db, fm4_merge_files)
    prog('inventory')
    skipped, filt_notes = filter_source(os.path.join(work, 'src_merge.slt'), inv, not a.include_existing)
    skipped_media = {m.lower() for _, m, _ in skipped}; skipped_ids = {c for c, _, _ in skipped}
    for c, m, why in skipped: print('SKIP car     ', m, '-', why)
    for n in filt_notes: print('note         ', n)
    S = sqlite3.connect(os.path.join(work, 'src_merge.slt'))
    if not S.execute('select count(*) from Data_Car').fetchone()[0]:
        if a.report:
            import json
            open(a.report, 'a').write(json.dumps(dict(src=a.src, offer=a.offer, pkg=None, ported=[], skipped=skipped, notes=filt_notes + ['nothing left to port'])) + '\n')
        print('NOTHING TO PORT'); return
    cars = S.execute('select Id, MediaName, DisplayName, ModelShort, Year from Data_Car order by Id').fetchall()
    engines = [r[0] for r in S.execute('select EngineID from Data_Engine order by EngineID')]
    drivetrains = [r[0] for r in S.execute('select DrivetrainID from Data_Drivetrain order by DrivetrainID')]
    src_offer = S.execute('select ID, DisplayName, Description from ContentOffers').fetchone()
    car_names = {c[1].lower(): c[1] for c in cars}
    if len(cars) > 1 and (a.model_donor or a.brake_donor):
        raise SystemExit('--model-donor/--brake-donor only support single-car packs')

    P = {'car': {c[0]: a.car_id + k for k, c in enumerate(cars)},
         'engine': {e: a.engine_id + k for k, e in enumerate(engines)},
         'drivetrain': {d: a.drivetrain_id + k for k, d in enumerate(drivetrains)}}
    wheels = [r[0] for r in S.execute('select ID from List_Wheels order by ID')] if S.execute("select 1 from sqlite_master where name='List_Wheels'").fetchone() else []
    P['wheel'] = {w: a.wheel_id + k for k, w in enumerate(wheels)} if a.wheel_id else {}

    # --- strings: source texts
    car_refs = [r for c in cars for r in c[2:4]]
    def src_texts(lang):
        out = {}
        if not src_str_zip: return out
        zf = zipfile.ZipFile(src_str_zip); names = {n.lower(): n for n in zf.namelist()}
        for tbl, refs in (('Data_Car', car_refs), ('ContentOffers', src_offer[1:])):
            n = names.get(('%s/%s.str' % (lang, tbl)).lower()) or names.get(('EN/%s.str' % tbl).lower())
            if not n: continue
            open(os.path.join(work, 'tmp.str'), 'wb').write(read_member(src_str_zip, n))
            by_hash = {h: txt for _, h, txt in forza_str.read(os.path.join(work, 'tmp.str'))}
            for r in refs: out[r] = by_hash.get(int(r[2:]) & 0xFFFF, '')
        return out
    en = src_texts('EN')

    # --- FM4 cumulative tables
    best = latest_tables(a.fm4_user, {'Data_Car', 'ContentOffers'}, skip=pkg)
    parsed = {k: SW.parse(read_member(z, n)) for k, (pri, z, n) in best.items()}
    if a.str_pri <= max(p for p, _, _ in best.values()):
        raise SystemExit('--str-pri must exceed %d' % max(p for p, _, _ in best.values()))
    hcar = free_hashes([t for (l, tb), t in parsed.items() if tb == 'Data_Car'], 2 * len(cars))
    ho, hd = free_hashes([t for (l, tb), t in parsed.items() if tb == 'ContentOffers'], 2)
    OFF_HI = int(src_offer[1][2:]) >> 16
    P['car_strings'] = {}
    for k, c in enumerate(cars):
        hi = int(c[2][2:]) >> 16
        P['car_strings'][c[0]] = dict(name=(hi << 16) | hcar[2 * k], short=(hi << 16) | hcar[2 * k + 1])
    P['offer_name'] = (OFF_HI << 16) | ho; P['offer_desc'] = (OFF_HI << 16) | hd

    # --- merge.slt
    template = max(fm4_merge_files, key=lambda p: len(sqlite3.connect(p).execute("select name from sqlite_master").fetchall()))
    slt_name = 'media/db/patch/%d000_merge.slt' % a.offer
    out_slt = os.path.join(work, os.path.basename(slt_name))
    if os.path.exists(out_slt): os.remove(out_slt)
    prog('filter + strings done')
    report = build_merge(os.path.join(work, 'src_merge.slt'), template, out_slt, a, P)
    prog('merge built')
    if getattr(a, 'db_donor_car', None):
        if len(cars) > 1: raise SystemExit('--db-donor-car only supports single-car packs')
        apply_db_donor(out_slt, a.fm4_db, a.db_donor_car, P, a.db_donor_tables.split(',') if a.db_donor_tables else None); prog('db donor applied')
    fm4_dbs = [sqlite3.connect('file:%s?mode=ro' % a.fm4_db, uri=True)] + [sqlite3.connect(p) for p in fm4_merge_files]
    hits = check_collisions(out_slt, fm4_dbs)
    soft = [(t, ids) for t, ids in hits if not (t in CORE_TABLES or t.startswith('List_Upgrade'))]
    hard = [(t, ids) for t, ids in hits if (t, ids) not in soft]
    if hard: raise SystemExit('ID collisions remain: %s' % hard)
    if soft:   # non-core rows that already exist in FM4 (makes, decals, colour exceptions...): keep FM4's
        O = sqlite3.connect(out_slt); n_soft = {}
        for t, _ in soft:
            pk = [r[1] for r in O.execute('pragma table_info("%s")' % t)][0]
            used = set()
            for db in fm4_dbs:
                try: used |= {r[0] for r in db.execute('select "%s" from "%s"' % (pk, t))}
                except sqlite3.Error: pass
            for (v,) in O.execute('select "%s" from "%s"' % (pk, t)).fetchall():
                if v in used: O.execute('delete from "%s" where "%s"=?' % (t, pk), (v,)); n_soft[t] = n_soft.get(t, 0) + 1
        O.commit(); O.execute('vacuum'); O.close()
        filt_notes.append('kept FM4 rows instead of colliding pack rows: %s' % n_soft)
        if check_collisions(out_slt, fm4_dbs): raise SystemExit('collisions after soft drop')
    if any(f.endswith('_%d000_merge.slt' % a.offer) for f in fm4_merge_files):
        raise SystemExit('offer %d already used' % a.offer)

    # --- pack folders
    root = os.path.join(a.out, pkg); pm = os.path.join(root, 'Media'); pdz = os.path.join(pm, 'DLCZips')
    os.makedirs(pdz, exist_ok=True)  # files are overwritten in place (deletes may be blocked on mounted folders)
    open(os.path.join(pm, '%d.puboffer' % a.offer), 'wb').write(b' \r\n')

    # --- digital gauge: FM4's Anark .fbf is version 0x3EF; newer (FH 0x3F0) crashes FM4's gauge loader
    drop_gauge = False
    with open(main_zip, 'rb') as f:
        for i in infos:
            if i.filename.lower().endswith('.fbf') and '/digitalgauge/' in i.filename.lower():
                ver = struct.unpack('>I', forza_zip.read_entry(f, i)[:4])[0]
                if ver != 0x3EF: drop_gauge = True
    if a.keep_gauge: drop_gauge = False

    def load_donor(zp):
        out = {}
        if zp:
            dcar = os.path.basename(zp)[:-4].lower()
            for di, draw in raw_entries(zp):
                n = di.filename.lower()
                if n.endswith('.carbin') and '/' not in n: out[n.replace(dcar, '')] = (di, draw)
        return out
    donor = load_donor(a.model_donor)
    brake_donor = load_donor(a.brake_donor)

    # --- main zip
    entries = []; build_no = {}; seen = set(); conv_report = []; dropped_other = []
    for info, raw in raw_entries(main_zip):
        if info is slt_info or info.filename == slt_info.filename:
            data = open(out_slt, 'rb').read(); entries.append(stored(slt_name, data)); continue
        if len(entries) % 50 == 0: prog('zip entries %d' % len(entries))
        lowsrc = info.filename.lower().replace('\\', '/')
        if not lowsrc.startswith(KEEP_PREFIX):
            dropped_other.append(info.filename); continue
        parts = lowsrc.split('/')
        if (parts[1] in ('cars', 'wheels') and len(parts) > 2 and parts[2] in skipped_media) or \
           any(parts[-1].startswith(m + '_') for m in skipped_media) or \
           any(parts[-1] == 'thumbnail_%d.xds' % c for c in skipped_ids):
            continue
        name = fm4_name(info.filename, car_names, drop_gauge)
        if name is None: continue
        m = re.search(r'Thumbnail_(\d+)\.xds$', name)
        if m and int(m.group(1)) in P['car']:
            name = name[:m.start(1)] + str(P['car'][int(m.group(1))]) + '.xds'
        if name.lower() in seen: raise SystemExit('duplicate ' + name)
        seen.add(name.lower())
        low = name.lower()
        is_brake = re.search(r'_(caliper|rotor)(lf|lr|rf|rr)_lod0\.carbin$', low)
        use = donor if donor else (brake_donor if is_brake else None)
        if use and low.endswith('.carbin') and low.startswith('media/cars/'):
            car = cars[0][1]
            base = name.rsplit('/', 1)[1].lower().replace(car.lower(), '')
            if base not in use: raise SystemExit('donor lacks model ' + base)
            di, draw = use[base]
            entries.append((name, di.compress_type, di.CRC, di.file_size, draw)); continue
        if not a.no_convert and low.endswith('.carbin') and low.startswith(('media/cars/', 'media/wheels/')):
            with open(main_zip, 'rb') as f: src_model = forza_zip.read_entry(f, info)
            fm4_model, st = carbin.convert(src_model)
            conv_report.append((name.split('/', 2)[2], len(src_model), len(fm4_model), st))
            entries.append(stored(name, fm4_model)); continue
        entries.append((name, info.compress_type, info.CRC, info.file_size, raw))
        mm = re.match(r'media/cars/([^/]+)/buildnumber\.txt$', low)
        if mm:
            with open(main_zip, 'rb') as f: build_no[mm.group(1)] = forza_zip.read_entry(f, info).decode('ascii', 'replace').strip()

    # Loose folders FH mounts via zipmount.xml (HarmonicTuning/EngineTuning/CarModelTuning xmls, LOD1/Burbles banks).
    # FM4 DLC has no folder mounts: xmls go into the main zip at the mount path, .fsb banks into the
    # loose <offer>_pri_65/ folder. Files FM4 already has, or that belong to skipped cars, are left out.
    fm4_media = os.path.dirname(os.path.dirname(os.path.abspath(a.fm4_db)))
    fm4_has = FM4Files(fm4_media)
    ht_added = []; loose_mount = []; mount_dropped = []
    zm = os.path.join(dz, 'zipmount.xml')
    mounts = re.findall(r'Name="([^"]+)"\s+Mount="([^"]+)"', open(zm, encoding='utf-8', errors='replace').read()) if os.path.exists(zm) else []
    for mname, mpath in mounts:
        d = os.path.join(dz, mname)
        if not os.path.isdir(d): continue
        rel = mpath.replace('\\', '/').split(':', 1)[-1].strip('/')          # Media/audio/...
        if not rel.lower().startswith('media/audio/'):
            mount_dropped.append(mname); continue
        for r, _, fs in os.walk(d):
            for fn in sorted(fs):
                if fn.endswith(('.ok', '.done')): continue
                sub = os.path.relpath(os.path.join(r, fn), d).replace('\\', '/')
                target = rel + '/' + sub
                if any(fn.lower().startswith(m + '_') for m in skipped_media): continue
                if fm4_has(target): continue
                if fn.lower().endswith('.fsb'):
                    if not fm4_has(os.path.dirname(target)):
                        mount_dropped.append('%s/%s (FM4 has no %s folder)' % (mname, fn, os.path.dirname(target))); continue
                    dst = os.path.join(pdz, '%d_pri_65' % a.offer, *target.split('/'))
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copyfile(os.path.join(r, fn), dst); loose_mount.append(target)
                else:
                    n = 'media/' + target.split('/', 1)[1]
                    if n.lower() in seen: continue
                    entries.append(stored(n, open(os.path.join(r, fn), 'rb').read())); seen.add(n.lower()); ht_added.append(target)

    for c in cars:
        car = c[1]
        vd = ('<?xml version="1.0" encoding="utf-8"?>\r\n<VersionData>\r\n  <BuildType value="BuildSystem" />\r\n'
              '  <Sync value="LKG" />\r\n  <ContentBuildNumber value="%s" />\r\n'
              '  <ToolsLabel value="FM4-recomp port_dlc_car.py (source: Forza Horizon DLC)" />\r\n</VersionData>\r\n'
              % (build_no.get(car.lower()) or car)).encode()
        for kind in ('cars', 'wheels'):
            n = 'media/%s/%s/versiondata.xml' % (kind, car)
            if n.lower() not in seen:
                idx = max(i for i, e in enumerate(entries) if e[0].lower().startswith('media/%s/%s/' % (kind, car.lower())))
                entries.insert(idx + 1, stored(n, vd)); seen.add(n.lower())
    prog('entries ready, writing zip')
    main_name = '%d_pri_65.zip' % a.offer
    write_zip(os.path.join(pdz, main_name), entries)

    # loose files next to the main zip (new engine sound banks): <src>_pri_65/ -> <offer>_pri_65/
    loose = []
    src_loose = main_zip[:-4]
    if os.path.isdir(src_loose):
        for r, _, fs in os.walk(src_loose):
            for fn in fs:
                if fn.endswith(('.ok', '.done')): continue
                rel = os.path.relpath(os.path.join(r, fn), src_loose)
                if not rel.replace('\\', '/').lower().startswith('media/audio/'):
                    dropped_other.append('loose:' + rel.replace('\\', '/')); continue
                dst = os.path.join(pdz, '%d_pri_65' % a.offer, rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(os.path.join(r, fn), dst); loose.append(rel.replace('\\', '/'))

    # --- string tables
    texts_cache = {}
    st_entries = []
    for (lang, tbl) in sorted(parsed):
        if lang not in texts_cache: texts_cache[lang] = src_texts(lang)
        tx = texts_cache[lang]
        pick = lambda r: tx.get(r) or en.get(r) or ''
        if tbl == 'Data_Car':
            new = []
            for c in cars:
                s = P['car_strings'][c[0]]; nid = P['car'][c[0]]
                new += [('IDS_DisplayName_%d' % nid, s['name'] & 0xFFFF, pick(c[2]) + a.display_suffix),
                        ('IDS_ModelShort_%d' % nid, s['short'] & 0xFFFF, pick(c[3]) or c[1])]
        else:
            new = [('IDS_DisplayName_%s' % a.offer_id, ho, (a.pack_name or pick(src_offer[1])) + a.display_suffix),
                   ('IDS_Description_%s' % a.offer_id, hd, (pick(src_offer[2]) + ' ').lstrip() + '[Ported from Forza Horizon]')]
        data = SW.rebuild(parsed[(lang, tbl)], new)
        st_entries.append(stored('%s/%s.str' % (lang, tbl), data))
    st_name = 'StringTables_pri_%d.zip' % a.str_pri
    write_zip(os.path.join(pdz, st_name), st_entries)
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='\r\n').write(
        '<?xml version="1.0" ?> \n<zipmount> \n   <zip Name="%s" Mount="game:\\Media\\StringTables\\"  /> \n</zipmount> \n' % st_name)
    open(os.path.join(pdz, 'stringtables.xml'), 'w', newline='\r\n').write(
        '<?xml version="1.0" ?> \n<stringtables> \n   <table Name="ContentOffers" /> \n   <table Name="Data_Car" /> \n</stringtables> \n')

    # --- XContent header
    hdr_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, 'Headers', '00000002')
    tmpl = a.header_template or next(p for p in sorted(glob.glob(os.path.join(hdr_dir, '*.header'))) if len(os.path.basename(p)) > 30)
    h = bytearray(open(tmpl, 'rb').read())
    dn = (a.pack_name or en.get(src_offer[1]) or cars[0][1]) + a.display_suffix
    h[0x08:0x108] = (dn[:127].encode('utf-16-be')).ljust(256, b'\0')
    h[0x108:0x132] = pkg.encode('ascii'); h[0x132:0x134] = b'\0\0'
    h[0x134:0x13C] = b'\0' * 8
    hdr_out = os.path.join(a.out, pkg + '.header'); open(hdr_out, 'wb').write(h)

    if a.install:
        dest = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002', pkg)
        shutil.copytree(root, dest, dirs_exist_ok=True)
        shutil.copyfile(hdr_out, os.path.join(hdr_dir, pkg + '.header'))

    print('package      ', pkg, '(installed)' if a.install else '')
    print('display name ', dn)
    for c in cars:
        print('car          ', c[1], '%d -> %d' % (c[0], P['car'][c[0]]), ' name:', en.get(c[2]) or '?')
    print('engines      ', P['engine'], ' drivetrains', P['drivetrain'], ' wheels', P['wheel'] or 'kept')
    print('offer        ', a.offer, a.offer_id, ' mapping ids from', a.mapping_id)
    print('str base pri ', max(p for p, _, _ in best.values()), '-> new', a.str_pri)
    print('main zip     ', main_name, len(entries), 'entries; template', os.path.basename(template))
    print('model donor  ', a.model_donor or '-', ' brake donor', a.brake_donor or '-')
    for n, a0, b0, st in conv_report: print('converted    ', n, a0, '->', b0, st)
    print('mount xmls   ', ht_added or '-')
    print('loose files  ', (loose + loose_mount) or '-')
    print('mount dropped', mount_dropped or '-')
    print('digital gauge', 'DROPPED (fbf not FM4 v0x3EF)' if drop_gauge else 'kept')
    print('dropped cols ', {k: v for k, v in report['dropped_cols'].items() if v})
    print('dropped tabs ', report['dropped_tables'] or '-')
    print('dropped files', len(dropped_other), sorted({'/'.join(x.split('/')[:3]) for x in dropped_other})[:20])
    if a.report:
        import json
        open(a.report, 'a').write(json.dumps(dict(
            src=a.src, offer=a.offer, pkg=pkg, display=dn,
            ported=[dict(media=c[1], name=en.get(c[2]) or '', year=c[4], old=c[0], new=P['car'][c[0]]) for c in cars],
            skipped=skipped, notes=filt_notes, dropped_tables=report['dropped_tables'],
            dropped_files=sorted({'/'.join(x.split('/')[:3]) for x in dropped_other}),
            gauge_dropped=drop_gauge, mount_files=ht_added, loose=loose + loose_mount, mount_dropped=mount_dropped)) + '\n')

if __name__ == '__main__':
    main()
