#!/usr/bin/env python3
"""make_fm3_base_pack.py - package BASE-GAME cars of Forza Motorsport 3 as a fake DLC package so
port_dlc_car.py can port them.

  python3 make_fm3_base_pack.py --game <FM3 root dir (contains Media/)> --car CHE_Aveo5_09
        --out <fake package dir> --work <local scratch> --fm4-media <FM4 Media dir>
        [--donor CHE_Spark_11]      # BASELINE MODE: use an FM4 car's models/wheels/textures/audio

Baseline mode (--donor): the DB rows, strings and thumbnail come from FM3; every file in the car's
media zips (models, textures, wheels, audio xml) is taken from the FM4 donor car and renamed to the
FM3 car. The ported car therefore shows the FM3 car's name/stats in menus but the donor's body.
That proves the FM3 DB/strings/pack pipeline independent of the FM3 .carbin vertex converter.
"""
import argparse, os, re, shutil, sqlite3, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip, forza_str
from port_dlc_car import raw_entries, write_zip, stored
import make_fh_base_pack as M
import xml.etree.ElementTree as ET


def fm3_db_with_offers(fm3_db, fm4_db, out):
    """FM3 has no ContentOffers tables: clone their schema and one template row from FM4."""
    shutil.copyfile(fm3_db, out)
    C = sqlite3.connect(out); F = sqlite3.connect(fm4_db)
    for t in ('ContentOffers', 'ContentOffersMapping'):
        sql = F.execute("select sql from sqlite_master where type='table' and name=?", (t,)).fetchone()[0]
        C.execute(sql)
    row = F.execute('select * from ContentOffers limit 1').fetchone()
    C.execute('insert into ContentOffers values (%s)' % ','.join('?' * len(row)), row)
    C.commit(); C.close(); F.close()


def prune_foreign_owners(mini, car_ids, eng, dt):
    """FM3 ids are small, so build_mini_db's 'id//1000 in owners' test also keeps other cars' rows whose
    car id happens to equal this car's engine/drivetrain id (e.g. drivetrain 453 vs car 453). Keep a row only if
    its owner class matches the table: engine tables -> engine ids, drivetrain tables -> drivetrain ids, else car ids."""
    C = sqlite3.connect(mini); removed = []
    for (t,) in list(C.execute("select name from sqlite_master where type='table'")):
        if t in ('ContentOffers', 'ContentOffersMapping', 'Data_Car', 'Data_Engine', 'Data_Drivetrain', 'List_Wheels'): continue
        pk = [r[1] for r in C.execute('pragma table_info("%s")' % t)][0]
        tl = t.lower()
        if tl in ('list_upgradeengine', 'list_upgradedrivetrain'): continue      # keyed by car id
        want = eng if tl.startswith('list_upgradeengine') else dt if tl.startswith('list_upgradedrivetrain') else car_ids
        for rid, v in C.execute('select rowid, "%s" from "%s"' % (pk, t)).fetchall():
            if isinstance(v, int) and v >= 1000 and v // 1000 in (car_ids | eng | dt) and v // 1000 not in want:
                C.execute('delete from "%s" where rowid=?' % t, (rid,)); removed.append((t, v))
    # FM3 torque curves have arbitrary ids + an EngineID column; FM4 has no EngineID and derives the owner
    # engine from id//1000 (curve 1835 would be read as engine 1's curve -> race load hangs). Renumber to engine*1000+k.
    tcm = {}
    cols = [r[1] for r in C.execute('pragma table_info(List_TorqueCurve)')]
    ccols = [r[1] for r in C.execute('pragma table_info(List_UpgradeEngineCamshaft)')]
    if 'EngineID' in cols and 'TorqueCurveZeroThrottleID' in ccols:
        # FM3 stores full- and zero-throttle curves as separate rows (camshaft row -> Full id, Zero id).
        # FM4: one row per camshaft level, zero-throttle scale = ZeroThrottleTorqueScale, id = engine*1000 + (0 if lvl0 else lvl+1)
        zero_ids = set()
        for e, lvl, full, zero in C.execute('select EngineID, Level, TorqueCurveFullThrottleID, TorqueCurveZeroThrottleID from List_UpgradeEngineCamshaft').fetchall():
            zs = C.execute('select TorqueScale from List_TorqueCurve where TorqueCurveID=?', (zero,)).fetchone()
            new = e * 1000 + (0 if lvl == 0 else lvl + 1)
            tcm[full] = new; zero_ids.add(zero)
            C.execute('update List_TorqueCurve set ZeroThrottleTorqueScale=? where TorqueCurveID=?', (zs[0] if zs else 0.0, full)) if 'ZeroThrottleTorqueScale' in cols else None
        if 'ZeroThrottleTorqueScale' not in cols:
            C.execute('alter table List_TorqueCurve add column ZeroThrottleTorqueScale real')
            for e, lvl, full, zero in C.execute('select EngineID, Level, TorqueCurveFullThrottleID, TorqueCurveZeroThrottleID from List_UpgradeEngineCamshaft').fetchall():
                zs = C.execute('select TorqueScale from List_TorqueCurve where TorqueCurveID=?', (zero,)).fetchone()
                C.execute('update List_TorqueCurve set ZeroThrottleTorqueScale=? where TorqueCurveID=?', (zs[0] if zs else 0.0, full))
        for z in zero_ids: C.execute('delete from List_TorqueCurve where TorqueCurveID=?', (z,))
        for old, new in tcm.items():
            C.execute('update List_TorqueCurve set TorqueCurveID=? where TorqueCurveID=?', (-new, old))
        C.execute('update List_TorqueCurve set TorqueCurveID=-TorqueCurveID where TorqueCurveID<0')
        for (t,) in list(C.execute("select name from sqlite_master where type='table'")):
            for c in [r[1] for r in C.execute('pragma table_info("%s")' % t)]:
                if t != 'List_TorqueCurve' and c.lower().startswith('torquecurve') and c.lower().endswith('id') and 'zero' not in c.lower():
                    for old, new in tcm.items():
                        C.execute('update "%s" set "%s"=? where "%s"=?' % (t, c, c), (-new, old))
                    C.execute('update "%s" set "%s"=-"%s" where "%s"<0' % (t, c, c, c))
    C.commit(); C.execute('vacuum'); C.close()
    print('torque curves renumbered:', tcm or '-')
    print('pruned foreign-owner rows:', removed or '-')


def merge_carattribs(fm3_xml, donor_xml):
    """FM3 carattribs (Version 15) -> FM4 (Version 16): keep FM3 values, add FM4-only gauges/Animations from the donor."""
    a = ET.fromstring(fm3_xml); b = ET.fromstring(donor_xml)
    a.set('Version', b.get('Version', '16'))
    ga = a.find('Gauges'); gb = b.find('Gauges')
    if ga is None and gb is not None:          # FM3 car has no Gauges section: take the donor's
        ga = ET.SubElement(a, 'Gauges')
    if ga is not None and gb is not None:
        have = {c.tag for c in ga}
        for c in gb:
            if c.tag not in have: ga.append(c)
    for c in b:
        if a.find(c.tag) is None: a.append(c)
    return ET.tostring(a, encoding='utf-8')


def real_model_entries(car, donor, fm3_cars_dir, fm3_wheels_dir, fm4_cars, fm4_wheels, notes):
    import carbin_fm3
    entries = []
    real_brakes = set()
    cn = car.lower(); dn = donor.lower()
    def ren(n): return n.replace(dn, cn).replace(donor, car)
    # --- car zip ---
    fz = M.ci_path(fm3_cars_dir, car + '.zip'); dz = M.ci_path(fm4_cars, donor + '.zip')
    donor_attr = M.zread(dz, 'carattribs.xml')
    for info, raw in raw_entries(fz):
        n = info.filename
        if n.endswith('/'): continue
        ln = n.lower()
        if ln in ('buildnumber.txt', 'versiondata.xml') or ln.endswith('build_report.html'): continue
        dst = 'media/cars/%s/%s' % (car, n)
        bm = re.search(r'_((caliper|rotor)[lr][fr])\.carbin$', ln)
        if bm:                                                                  # FM3 brakes: convert (16-byte vertices), FM4 names them *_lod0
            data = M.zread(fz, n)
            out, st = carbin_fm3.convert3(data)
            entries.append(stored('media/cars/%s/%s_lod0.carbin' % (car, n[:-len('.carbin')]), out))
            notes.append('brake %s %s' % (n, st)); real_brakes.add(bm.group(1)); continue
        if ln == 'carattribs.xml':
            data = M.zread(fz, n)
            entries.append(stored(dst, merge_carattribs(data, donor_attr))); continue
        if ln.endswith('.carbin'):
            data = M.zread(fz, n)
            out, st = carbin_fm3.convert3(data)
            nn = dst
            entries.append(stored(nn, out)); notes.append('%s %s' % (n, st)); continue
        entries.append((dst, info.compress_type, info.CRC, info.file_size, raw))
    # donor files the FM3 zip lacks: brakes, digitalgauge
    have = {e[0].lower() for e in entries}
    for info, raw in raw_entries(dz):
        n = info.filename
        if n.endswith('/'): continue
        ln = n.lower()
        bm = re.search(r'_((caliper|rotor)[lr][fr])_lod0\.carbin$', ln)
        if bm and bm.group(1) in real_brakes: continue                          # converted FM3 brake already present
        if re.search(r'_(caliper|rotor)[lr][fr]_lod0\.carbin$', ln) or ln.startswith('digitalgauge/'):
            entries.append(('media/cars/%s/%s' % (car, ren(n)), info.compress_type, info.CRC, info.file_size, raw))
    # --- wheel zip ---
    fw = M.ci_path(fm3_wheels_dir, car + '.zip')
    for info, raw in raw_entries(fw):
        n = info.filename
        if n.endswith('/'): continue
        ln = n.lower()
        if ln in ('buildnumber.txt', 'versiondata.xml') or ln.endswith('build_report.html'): continue
        dst = 'media/wheels/%s/%s' % (car, n)
        if ln.endswith('.carbin'):
            out, st = carbin_fm3.convert3(M.zread(fw, n))
            entries.append(stored(dst, out)); notes.append('wheel %s %s' % (n, st)); continue
        entries.append((dst, info.compress_type, info.CRC, info.file_size, raw))
    return entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True); ap.add_argument('--car', action='append', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--donor')
    ap.add_argument('--real-model', action='store_true', help='convert the FM3 car/wheel models+textures (donor still supplies brakes, gauges, audio)')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    media = M.ci_path(a.game, 'media')
    fm4_db = M.ci_path(a.fm4_media, 'db', 'gamedb.slt')
    src_db = os.path.join(a.work, 'fm3_plus_offers.slt')
    fm3_db_with_offers(M.ci_path(media, 'db', 'gamedb.slt'), fm4_db, src_db)
    mini = os.path.join(a.work, 'mini.slt')
    ids, car_ids, eng, dt = M.build_mini_db(src_db, mini, a.car)
    prune_foreign_owners(mini, car_ids, eng, dt)
    print('car ids', sorted(car_ids), 'engines', sorted(eng), 'drivetrains', sorted(dt))
    entries = []; notes = []
    entries.append(stored('media/db/patch/9000_merge.slt', open(mini, 'rb').read()))
    ui_tex = M.ci_path(media, 'ui', 'Textures.zip')
    fm4_cars = M.ci_path(a.fm4_media, 'cars'); fm4_wheels = M.ci_path(a.fm4_media, 'wheels')
    ac = M.ci_path(a.fm4_media, 'audio', 'cars')
    if a.real_model:
        import carbin_fm3; carbin_fm3.init_templates(a.fm4_media)
    for car in a.car:
        if not a.donor: raise SystemExit('--donor is required (brakes/gauges/audio come from an FM4 car)')
        dn = a.donor.lower(); cn = car.lower()
        def ren(n): return n.replace(dn, cn).replace(a.donor, car)
        if a.real_model:
            entries += real_model_entries(car, a.donor, M.ci_path(media, 'cars'), M.ci_path(media, 'wheels'), fm4_cars, fm4_wheels, notes)
        for sub, d in (() if a.real_model else (('cars', fm4_cars), ('wheels', fm4_wheels))):
            dz = M.ci_path(d, a.donor + '.zip')
            for info, raw in raw_entries(dz):
                if info.filename.endswith('/'): continue
                if info.filename.lower() in ('buildnumber.txt', 'versiondata.xml') or info.filename.lower().endswith('build_report.html'):
                    continue            # port_dlc_car regenerates these
                entries.append(('media/%s/%s/%s' % (sub, car, ren(info.filename)), info.compress_type, info.CRC, info.file_size, raw))
        tn = 'Thumbnails/Thumbnail_%d.xds' % ids[car]
        th = M.zread(ui_tex, tn)
        if th is None: notes.append('no thumbnail ' + tn)
        else: entries.append(stored('media/UI/Textures/Thumbnails/Thumbnail_%d.xds' % ids[car], th))
        cmt = M.zread(M.ci_path(ac, 'CarModelTuning.zip'), a.donor + '_CMT.xml')
        et = M.zread(M.ci_path(ac, 'Engines', 'EngineTuning.zip'), a.donor + '_ET.xml')
        if cmt: entries.append(stored('media/audio/cars/CarModelTuning/%s_CMT.xml' % car, cmt))
        else: notes.append('no donor CMT')
        if et: entries.append(stored('media/audio/cars/Engines/EngineTuning/%s_ET.xml' % car, et))
        else: notes.append('no donor ET')
    dz = os.path.join(a.out, 'Media', 'DLCZips'); os.makedirs(dz, exist_ok=True)
    write_zip(os.path.join(dz, '9000_pri_65.zip'), entries)
    sz = M.ci_path(media, 'stringtables', 'EN.zip')
    dc = M.zread(sz, 'Data_Car.str')
    write_zip(os.path.join(dz, 'StringTables_pri_1.zip'), [stored('EN/Data_Car.str', dc)])
    print('entries', len(entries)); print('notes', notes or '-')
    for car in a.car: print(car, 'id', ids[car])


if __name__ == '__main__':
    main()
