#!/usr/bin/env python3
"""port_fm2_car.py - plan and verify FM2 -> FM4 car ports. READ-ONLY: writes nothing into the game or repo
except the optional --report JSON.

  # one car: donor ranking, id allocation, checks, PASS / WARN / FAIL report
  python3 port_fm2_car.py --car LOT_Elise135R_03 [--donor <FM4 MediaName>] [--report out.json]

  # whole FM2 roster: status of every FM2 car (already in FM4, already in a pack, make missing, ready)
  python3 port_fm2_car.py --roster

Paths default to the repo layout; override with --fm2-game, --fm4-media, --fm4-user, --staged, --work.
The build step (make_fm2_base_pack.py) is NOT run here. It still keeps donor ids, so id injection is the
next framework step (see README.md).
"""
import argparse, json, os, sqlite3, sys, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import inventory, donor

DLC = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', 'Forza-4-compiling-stuff'))
DEF = dict(
    fm2_game=os.path.join(DLC, 'game-content', 'FM2', 'Media'),
    fm4_media=os.path.join(DLC, 'extracted', 'Media'),
    fm4_user=os.path.join(HERE, '..', '..', '..', '.local', 'user'),
    staged=[os.path.join(DLC, 'game-content', 'DLC-FH', '_FM4-ports', 'fm2_batch1')],
    work=os.path.join(os.path.expanduser('~'), 'fm2work', 'port_fw'),
)


def ci_find(folder, name):
    """Case-insensitive file lookup (the game folders are mixed case)."""
    if not folder or not os.path.isdir(folder):
        return None
    for f in os.listdir(folder):
        if f.lower() == name.lower():
            return os.path.join(folder, f)
    return None


def zip_has(zpath, member):
    try:
        return member.lower() in {n.lower() for n in zipfile.ZipFile(zpath).namelist()}
    except Exception:
        return False


def fm2_row(fm2_db, car):
    G = sqlite3.connect('file:%s?mode=ro' % fm2_db, uri=True)
    try:
        cols = [r[1] for r in G.execute('pragma table_info(Data_Car)')]
        r = G.execute('select * from Data_Car where MediaName=? collate nocase', (car,)).fetchone()
        if not r:
            return None
        d = dict(zip(cols, r))
        mk = G.execute('select ManufacturerCode from List_CarMake where ID=?', (d['MakeID'],)).fetchone()
        return d, (mk[0] if mk else None)
    finally:
        G.close()


def fm2_all(fm2_db):
    G = sqlite3.connect('file:%s?mode=ro' % fm2_db, uri=True)
    try:
        return [r[0] for r in G.execute('select MediaName from Data_Car where MediaName is not null order by Id')]
    finally:
        G.close()


def src_for(fm2_db, fm4_db, car):
    """FM2 car summary in FM4 terms (class mapped by performance index, as make_fm2_base_pack does)."""
    got = fm2_row(fm2_db, car)
    if not got:
        return None
    d, code = got
    F = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
    try:
        cls = donor.fm4_class(F, d['PerformanceIndex'])
        mk4 = F.execute('select ID from List_CarMake where ManufacturerCode=?', (code,)).fetchone()
    finally:
        F.close()
    return dict(MediaName=d['MediaName'], MakeCode=code, Year=d['Year'], PI=d['PerformanceIndex'],
                Class=cls, Asp=d.get('AspirationTypeId'), Id=d['Id'], fm4_make=mk4[0] if mk4 else None)


def fm4_has(fm4_db, media):
    F = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
    try:
        return F.execute('select count(*) from Data_Car where MediaName=? collate nocase', (media,)).fetchone()[0] > 0
    finally:
        F.close()


def roster(a):
    fm2_db = os.path.join(a.fm2_game, 'db', 'gamedb.slt')
    fm4_db = os.path.join(a.fm4_media, 'db', 'gamedb.slt')
    inv = inventory.scan(fm4_db, a.fm4_user, a.staged, a.work)
    rows = []
    for car in fm2_all(fm2_db):
        s = src_for(fm2_db, fm4_db, car)
        if fm4_has(fm4_db, car):
            status, best = 'in-fm4-base', None
        elif car.lower() in inv['media']:
            status, best = 'in-pack-installed-or-staged', None
        elif s['fm4_make'] is None:
            status, best = 'make-missing-in-fm4', None
        else:
            r = donor.rank(s, fm4_db, top=1)
            best = r[0] if r else None
            status = 'ready' if best and best['tier'] in ('model', 'make') else 'ready-weak-donor'
        rows.append(dict(car=car, status=status, donor=best and best['MediaName'], tier=best and best['tier']))
    for r in rows:
        print('%-28s %-28s %-12s %s' % (r['car'], r['status'], r['tier'] or '-', r['donor'] or '-'))
    from collections import Counter
    print('summary:', dict(Counter(r['status'] for r in rows)))
    return rows


def plan(a):
    fm2_db = os.path.join(a.fm2_game, 'db', 'gamedb.slt')
    fm4_db = os.path.join(a.fm4_media, 'db', 'gamedb.slt')
    checks = []

    def chk(name, status, detail):
        checks.append(dict(check=name, status=status, detail=detail))

    s = src_for(fm2_db, fm4_db, a.car)
    if not s:
        chk('FM2 car exists', 'FAIL', '%s not in FM2 Data_Car' % a.car)
        return finish(checks, a)
    chk('FM2 car exists', 'PASS', '%s (FM2 id %d, %s)' % (s['MediaName'], s['Id'], s['Year']))
    chk('not already in FM4 base', 'FAIL' if fm4_has(fm4_db, s['MediaName']) else 'PASS',
        'FM4 base already has it' if fm4_has(fm4_db, s['MediaName']) else 'no base car with this MediaName')

    inv = inventory.scan(fm4_db, a.fm4_user, a.staged, a.work)
    taken = s['MediaName'].lower() in inv['media']
    chk('not in installed or staged pack', 'FAIL' if taken else 'PASS',
        'name already in an installed or staged pack' if taken else 'no installed or staged pack has this name')
    chk('make maps to FM4', 'PASS' if s['fm4_make'] else 'FAIL',
        'FM4 make id %s' % s['fm4_make'] if s['fm4_make'] else 'no FM4 make with ManufacturerCode %s' % s['MakeCode'])

    fz = ci_find(os.path.join(a.fm2_game, 'cars'), s['MediaName'] + '.zip')
    member = s['MediaName'].lower() + '.carbin'
    chk('FM2 body carbin present', 'PASS' if fz and zip_has(fz, member) else 'FAIL',
        '%s in %s' % (member, os.path.basename(fz)) if fz and zip_has(fz, member) else 'no %s' % member)

    ranked = donor.rank(s, fm4_db, top=5)
    if a.donor:
        cand = [r for r in ranked if r['MediaName'].lower() == a.donor.lower()]
        if not cand:
            F = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
            row = F.execute('select Id, MediaName, Year, PerformanceIndex, AspirationTypeId from Data_Car where MediaName=? collate nocase',
                            (a.donor,)).fetchone()
            F.close()
            if not row:
                chk('donor exists in FM4', 'FAIL', '%s not in FM4 Data_Car' % a.donor)
                return finish(checks, a)
            cand = [dict(Id=row[0], MediaName=row[1], Year=row[2], PI=row[3], Asp=row[4], tier='manual', score=None, reasons=[])]
        donor_row = cand[0]
    else:
        if not ranked:
            chk('donor found', 'FAIL', 'no FM4 candidates')
            return finish(checks, a)
        donor_row = ranked[0]
    tier = donor_row.get('tier', 'manual')
    tier_status = 'PASS' if tier in ('model', 'manual') else ('WARN' if tier in ('make', 'class') else 'FAIL')
    chk('donor tier', tier_status, '%s (%s), score %s; %s' % (donor_row['MediaName'], tier, donor_row.get('score'),
                                                          ', '.join(donor_row.get('reasons', [])) or 'manual choice'))
    if donor_row.get('Asp') is not None and s.get('Asp') is not None and donor_row['Asp'] != s['Asp']:
        chk('aspiration matches donor', 'WARN', 'FM2 aspiration id %s, donor %s (enum not verified); builder checks this too'
            % (s['Asp'], donor_row['Asp']))
    else:
        chk('aspiration matches donor', 'PASS', 'same aspiration id or none recorded')
    cars_dir = os.path.join(a.fm4_media, 'cars')
    wheels_dir = os.path.join(a.fm4_media, 'wheels')
    dz = ci_find(cars_dir, donor_row['MediaName'] + '.zip')
    wz = ci_find(wheels_dir, donor_row['MediaName'] + '.zip')
    chk('donor car and wheel zips', 'PASS' if dz and wz else 'WARN',
        'in base media' if dz and wz else 'not in base media: pass --donor-pack (disc 2 package) to the builder')

    ids = inventory.allocate(inv, 1)
    for k in ('car', 'engine', 'drivetrain', 'wheel', 'mapping'):
        clash = ids[k] in inv[{'car': 'car', 'engine': 'engine', 'drivetrain': 'drivetrain', 'wheel': 'wheel', 'mapping': 'mapping'}[k]]
        if clash:
            chk('id %s free' % k, 'FAIL', '%d already used' % ids[k])
    if ids['engine'] * 1000 in inv['torque']:
        chk('torque keys free', 'FAIL', 'engine %d torque key in use' % ids['engine'])
    else:
        chk('ids free (allocated)', 'PASS', 'car %d, engine %d, drivetrain %d, wheel %d, mapping %d, string priority %d'
            % (ids['car'], ids['engine'], ids['drivetrain'], ids['wheel'], ids['mapping'], ids['str_pri']))
    n, oid = inventory.pick_offer(inv)
    chk('offer free', 'PASS' if n else 'FAIL', 'offer %s, id %s' % (n, oid) if n else 'no free 2-digit offer')
    chk('string priority free', 'PASS' if ids['str_pri'] not in inv['str_pri'] else 'FAIL', str(ids['str_pri']))
    chk('build step', 'WARN', 'not automated yet: make_fm2_base_pack.py keeps donor ids (framework step 2)')

    plan_out = dict(car=s['MediaName'], donor=donor_row['MediaName'], donor_id=donor_row['Id'],
                    ids=ids, offer=n, offer_id=oid, sources=inv['sources'],
                    ranked=[dict(MediaName=r['MediaName'], tier=r['tier'], score=r['score']) for r in ranked])
    return finish(checks, a, plan_out)


def finish(checks, a, plan_out=None):
    overall = 'FAIL' if any(c['status'] == 'FAIL' for c in checks) else (
        'WARN' if any(c['status'] == 'WARN' for c in checks) else 'PASS')
    for c in checks:
        print('[%s] %-28s %s' % (c['status'], c['check'], c['detail']))
    print('overall: %s' % overall)
    rep = dict(overall=overall, checks=checks, plan=plan_out)
    if a.report:
        with open(a.report, 'w') as f:
            json.dump(rep, f, indent=1, default=str)
    return rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--car'); ap.add_argument('--donor'); ap.add_argument('--roster', action='store_true')
    ap.add_argument('--report')
    for k, v in DEF.items():
        ap.add_argument('--' + k.replace('_', '-'), default=v, type=str if k != 'staged' else None,
                        action='append' if k == 'staged' else None)
    a = ap.parse_args()
    if a.staged and a.staged == DEF['staged'] and isinstance(DEF['staged'], list):
        a.staged = DEF['staged']
    if a.roster:
        roster(a)
        return
    if not a.car:
        ap.error('give --car or --roster')
    rep = plan(a)
    sys.exit(1 if rep['overall'] == 'FAIL' else 0)


if __name__ == '__main__':
    main()
