"""inventory.py - read-only inventory of ids, offers, string priorities and media names already in use,
and id allocation for a new pack.

Sources: the FM4 base gamedb, every installed FM4 DLC merge, and every staged pack folder passed in.
Nothing is written. Rules follow docs/CAR-PORT-HANDOFF.md:
  * car, engine, drivetrain, wheel and mapping ids are consecutive runs above the highest id in use
    (nothing is reused, so removed or staged packs cannot collide later);
  * drivetrain = engine + 100 for the same car (engine and drivetrain share the owner id space);
  * torque curves are keyed engine x 1000, so an engine owner id must be free in the torque keys too;
  * offer numbers are 2-digit decimal in the ContentOffers id (offer 86 = 4D530910F4FF8600), so they
    must stay <= 99 under that scheme; the highest free number is used;
  * string priorities go above the highest priority in use (25xxx range), in steps of 10.
"""
import hashlib, os, re, sqlite3, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))          # tools/dlc, for forza_zip
import forza_zip

TITLE = '4D530910'
ID_COLUMNS = (('Data_Car', 'Id', 'car'), ('Data_Engine', 'EngineID', 'engine'),
              ('Data_Drivetrain', 'DrivetrainID', 'drivetrain'), ('List_Wheels', 'ID', 'wheel'),
              ('ContentOffersMapping', 'Id', 'mapping'))
KINDS = ('car', 'engine', 'drivetrain', 'wheel', 'mapping', 'torque', 'offer_num', 'offer_id', 'offer_key', 'str_pri', 'media')
OFFER_ID_FMT = '4D530910F4FF%02d00'


def empty_inventory():
    inv = {k: set() for k in KINDS}
    inv['sources'] = []
    return inv


def _ints(rows):
    out = set()
    for (v,) in rows:
        try:
            out.add(int(v))
        except (TypeError, ValueError):
            pass
    return out


def read_db(path, inv, label):
    """Add every id and name from one gamedb / merge SQLite file to the inventory."""
    D = sqlite3.connect('file:%s?mode=ro' % path, uri=True)
    try:
        tabs = [r[0] for r in D.execute("select name from sqlite_master where type='table'")]
        for tbl, col, key in ID_COLUMNS:
            if tbl in tabs and col in {r[1] for r in D.execute('pragma table_info("%s")' % tbl)}:
                inv[key] |= _ints(D.execute('select "%s" from "%s" where "%s" is not null' % (col, tbl, col)))
        for tbl in tabs:
            if 'torque' in tbl.lower():
                first = D.execute('pragma table_info("%s")' % tbl).fetchone()[1]
                inv['torque'] |= _ints(D.execute('select "%s" from "%s"' % (first, tbl)))
        if 'Data_Car' in tabs:
            inv['media'] |= {m.lower() for (m,) in D.execute('select MediaName from Data_Car where MediaName is not null')}
        if 'ContentOffers' in tabs:
            cols = {r[1] for r in D.execute('pragma table_info(ContentOffers)')}
            # ContentOffers.ID is the text id (4D530910F4FF8600); OfferId is the same value as a 64-bit int;
            # PubOfferingID is the offer number that names the .puboffer file.
            if 'ID' in cols:
                inv['offer_id'] |= {str(v).upper() for (v,) in D.execute('select ID from ContentOffers where ID is not null')}
            if 'OfferId' in cols:
                inv['offer_key'] |= _ints(D.execute('select OfferId from ContentOffers where OfferId is not null'))
            if 'PubOfferingID' in cols:
                inv['offer_num'] |= _ints(D.execute('select PubOfferingID from ContentOffers where PubOfferingID is not null'))
    finally:
        D.close()
    inv['sources'].append(label)


def _merges(zip_path, work):
    """Extract every *_merge.slt from a pack zip into work (uncompressed copies, one per member)."""
    tag = hashlib.md5(zip_path.encode()).hexdigest()[:12]
    out = []
    for info in zipfile.ZipFile(zip_path).infolist():
        if info.filename.lower().endswith('_merge.slt'):
            p = os.path.join(work, '%s_%d.slt' % (tag, len(out)))
            with open(zip_path, 'rb') as f:
                data = forza_zip.read_entry(f, info)
            with open(p, 'wb') as g:
                g.write(data)
            out.append(p)
    return out


def scan(fm4_db, fm4_user, staged_dirs, work):
    """Inventory of base game + installed DLC (fm4_user/.../00000002) + staged pack folders."""
    inv = empty_inventory()
    read_db(fm4_db, inv, 'fm4-base')
    os.makedirs(work, exist_ok=True)
    roots = []
    installed = os.path.join(fm4_user, '0000000000000000', TITLE, '00000002')
    if os.path.isdir(installed):
        roots.append(('installed', installed))
    for s in staged_dirs:
        roots.append(('staged:' + os.path.basename(os.path.normpath(s)), s))
    for label, root in roots:
        for dp, _, files in os.walk(root):
            for fn in files:
                low = fn.lower()
                p = os.path.join(dp, fn)
                m = re.match(r'(\d+)\.puboffer$', low)
                if m:
                    inv['offer_num'].add(int(m.group(1)))
                m = re.match(r'stringtables_pri_(\d+)\.zip$', low)
                if m:
                    inv['str_pri'].add(int(m.group(1)))
                if low.endswith('_pri_65.zip'):
                    for sp in _merges(p, work):
                        read_db(sp, inv, '%s:%s' % (label, fn))
    return inv


def free_run(used, n, start):
    """First v >= start such that v .. v+n-1 are all unused."""
    v = start
    while any((v + i) in used for i in range(n)):
        v += 1
    return v


def allocate(inv, k=1):
    """Allocate ids for a pack of k cars. Returns a dict of bases (consecutive from each base)."""
    if k < 1:
        raise ValueError('k must be >= 1')
    car = free_run(inv['car'], k, max(inv['car'] | {0}) + 1)
    mapping = free_run(inv['mapping'], k, max(inv['mapping'] | {0}) + 1)
    wheel = free_run(inv['wheel'], k, max(inv['wheel'] | {0}) + 1)
    used_owner = inv['engine'] | inv['drivetrain'] | {t // 1000 for t in inv['torque']}
    eng = max(used_owner | {0}) + 1
    while True:
        e_run = set(range(eng, eng + k))
        d_run = set(range(eng + 100, eng + 100 + k))
        if not (e_run | d_run) & used_owner:
            break
        eng += 1
    pri = max(inv['str_pri'] | {25000}) + 10
    return dict(car=car, engine=eng, drivetrain=eng + 100, wheel=wheel, mapping=mapping, str_pri=pri,
                count=k)


def pick_offer(inv):
    """Highest free offer number <= 99 whose ContentOffers id is also unused. None if exhausted."""
    for n in sorted(set(range(5, 100)) - inv['offer_num'], reverse=True):
        oid = OFFER_ID_FMT % n
        if oid not in inv['offer_id'] and int(oid, 16) not in inv['offer_key']:
            return n, oid
    return None, None
