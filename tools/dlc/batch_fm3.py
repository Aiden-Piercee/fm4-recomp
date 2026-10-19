#!/usr/bin/env python3
"""batch_fm3.py - port a list of FM3 DISC 1 base-game cars to FM4 as one DLC pack.

Steps:
  1. Scan FM4 base gamedb + every installed FM4 DLC merge for the highest ids in use
     (Data_Car, Data_Engine, Data_Drivetrain, List_Wheels, ContentOffersMapping), the highest
     installed offer number and the highest installed string-table priority.
  2. Allocate fresh, non-overlapping bases for all of them (ids are consecutive from each base).
  3. make_fm3_base_pack.py --real-model (FM3 body/wheel models converted; brakes, audio and
     gauge donor fallbacks as before).
  4. port_dlc_car.py on that pack with the allocated bases (+ --fill-car from the donor).
  5. Optionally install (--install). Installing refuses to overwrite an existing offer.

  python3 batch_fm3.py --game <FM3U1 dir> --fm4-media <FM4 Media dir> --fm4-user <repo>/.local/user \
      --work <local scratch> --out <staging dir> --cars POR_BoxsterS_03,POR_CaymanS_06 [--plan] [--install]

--plan prints the car list and the allocated ids without building anything.
"""
import argparse, glob, json, os, re, shutil, sqlite3, subprocess, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import forza_zip

TITLE = '4D530910'

def offer_id(n):
    return '4D530910F4FF%02d00' % n          # offer 83 -> 4D530910F4FF8300 (as in the earlier ports)

def installed_merges(fm4_user):
    out = []
    for z in glob.glob(os.path.join(fm4_user, '0000000000000000', TITLE, '00000002', '*', 'Media', 'DLCZips', '*_pri_65.zip')):
        out.append(z)
    return out

def installed_offers(fm4_user):
    nums = []
    for p in glob.glob(os.path.join(fm4_user, '0000000000000000', TITLE, '00000002', '*', 'Media', '*.puboffer')):
        m = re.match(r'(\d+)\.puboffer$', os.path.basename(p))
        if m: nums.append(int(m.group(1)))
    return nums

def installed_str_pri(fm4_user):
    pri = []
    for z in glob.glob(os.path.join(fm4_user, '0000000000000000', TITLE, '00000002', '*', 'Media', 'DLCZips', 'StringTables_pri_*.zip')):
        m = re.search(r'_pri_(\d+)', z)
        if m: pri.append(int(m.group(1)))
    return pri

def max_ids(db_path, work, tag):
    """max of each id column over the tables that matter, for one sqlite file."""
    res = {}
    D = sqlite3.connect('file:%s?mode=ro' % db_path, uri=True)
    tabs = {r[0] for r in D.execute("select name from sqlite_master where type='table'")}
    for tbl, col, key in (('Data_Car', 'Id', 'car'), ('Data_Engine', 'EngineID', 'engine'),
                          ('Data_Drivetrain', 'DrivetrainID', 'drivetrain'), ('List_Wheels', 'ID', 'wheel'),
                          ('ContentOffersMapping', 'Id', 'mapping')):
        if tbl in tabs:
            v = D.execute('select max("%s") from "%s"' % (col, tbl)).fetchone()[0]
            if v is not None: res[key] = max(res.get(key, 0), int(v))
    D.close()
    return res

def scan(fm4_db, fm4_user, work):
    best = max_ids(fm4_db, work, 'base')
    os.makedirs(work, exist_ok=True)
    for i, z in enumerate(installed_merges(fm4_user)):
        for info in zipfile.ZipFile(z).infolist():
            if info.filename.lower().endswith('_merge.slt'):
                p = os.path.join(work, 'scan_%d.slt' % i)
                if not os.path.exists(p):          # cache: merges are extracted once per work dir
                    with open(z, 'rb') as f: open(p, 'wb').write(forza_zip.read_entry(f, info))
                for k, v in max_ids(p, work, 'x').items(): best[k] = max(best.get(k, 0), v)
    return best

def find_donor_id(fm4_db, media):
    D = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
    r = D.execute('select Id from Data_Car where lower(MediaName)=lower(?)', (media,)).fetchone()
    return r[0] if r else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True); ap.add_argument('--fm4-media', required=True)
    ap.add_argument('--fm4-user', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--cars', required=True,
                    help='comma list of FM3 disc 1 MediaNames, or a file with one per line')
    ap.add_argument('--donor', default='CHE_Spark_11'); ap.add_argument('--offer', type=int)
    ap.add_argument('--pack-name', default='FM3 batch'); ap.add_argument('--display-suffix', default=' (FM3)')
    ap.add_argument('--plan', action='store_true'); ap.add_argument('--install', action='store_true')
    ap.add_argument('--report', default=None)
    a = ap.parse_args()

    cars = [c.strip() for c in (open(a.cars).read().split() if os.path.isfile(a.cars) else a.cars.split(',')) if c.strip()]
    # pre-check: every car needs its body carbin and carattribs.xml in the FM3 zip
    media = os.path.join(a.game, 'Media')
    ok, skipped = [], []
    for c in cars:
        zp = os.path.join(media, 'Cars', c + '.zip')
        if not os.path.isfile(zp): skipped.append((c, 'no FM3 zip')); continue
        names = {n.lower() for n in zipfile.ZipFile(zp).namelist()}
        if c.lower() + '.carbin' not in {n.split('/')[-1] for n in names}: skipped.append((c, 'no body carbin')); continue
        if 'carattribs.xml' not in {n.split('/')[-1] for n in names}: skipped.append((c, 'no carattribs.xml')); continue
        ok.append(c)
    for c, why in skipped: print('SKIP', c, '-', why)
    cars = ok
    if not cars: raise SystemExit('no cars left to port')
    fm4_db = os.path.join(a.fm4_media, 'db', 'gamedb.slt')
    best = scan(fm4_db, a.fm4_user, a.work)
    offers = installed_offers(a.fm4_user)
    offer = a.offer or (max(offers + [82]) + 1)
    pris = installed_str_pri(a.fm4_user)
    str_pri = (max(pris) + 10) if pris else 25100
    donor_id = find_donor_id(fm4_db, a.donor)
    if donor_id is None: raise SystemExit('donor %s not in FM4 Data_Car' % a.donor)
    # engine and drivetrain ids must not overlap: torque curves are keyed engine*1000+level and
    # share the owner id space with drivetrains (see fm3-db-port-findings). Keep 100 ids per block.
    engine_base = best.get('engine', 1100) + 1
    drivetrain_base = max(best.get('drivetrain', 1050) + 1, engine_base + 100)
    alloc = dict(car_id=best.get('car', 1700) + 1, engine_id=engine_base,
                 drivetrain_id=drivetrain_base, wheel_id=best.get('wheel', 11600) + 1,
                 mapping_id=best.get('mapping', 6000) + 1)
    plan = dict(cars=cars, donor=a.donor, donor_id=donor_id, offer=offer, offer_id=offer_id(offer),
                str_pri=str_pri, **alloc)
    print(json.dumps(plan, indent=1))
    if a.plan: return
    if os.path.exists(os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002', '%d.puboffer' % offer)) or \
       glob.glob(os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002', '*', 'Media', '%d.puboffer' % offer)):
        raise SystemExit('offer %d already installed' % offer)

    pack = os.path.join(a.work, 'pack'); shutil.rmtree(pack, ignore_errors=True)
    subprocess.run([sys.executable, '-I', os.path.join(HERE, 'make_fm3_base_pack.py'), '--game', a.game,
                    *sum([['--car', c] for c in cars], []), '--donor', a.donor, '--real-model',
                    '--out', pack, '--work', os.path.join(a.work, 'mkpack'), '--fm4-media', a.fm4_media], check=True)
    cmd = [sys.executable, '-I', os.path.join(HERE, 'port_dlc_car.py'), pack, '--fm4-db', fm4_db,
           '--fm4-user', a.fm4_user, '--out', a.out, '--work', os.path.join(a.work, 'port'),
           '--car-id', str(alloc['car_id']), '--engine-id', str(alloc['engine_id']),
           '--drivetrain-id', str(alloc['drivetrain_id']), '--mapping-id', str(alloc['mapping_id']),
           '--wheel-id', str(alloc['wheel_id']), '--offer', str(offer), '--offer-id', offer_id(offer),
           '--str-pri', str(str_pri), '--fill-car', str(donor_id), '--display-suffix', a.display_suffix,
           '--pack-name', a.pack_name]
    if a.install: cmd.append('--install')
    if a.report: cmd += ['--report', a.report]
    subprocess.run(cmd, check=True)
    print('done. offer %d, cars %s' % (offer, ', '.join(cars)))

if __name__ == '__main__':
    main()
