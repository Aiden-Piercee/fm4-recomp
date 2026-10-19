#!/usr/bin/env python3
"""make_fh_base_pack.py - package BASE-GAME cars of Forza Horizon / Horizon 2 as a fake
'FH DLC package' so port_dlc_car.py can port them unchanged.

  python3 make_fh_base_pack.py --game <FH2 root dir (contains Media/ or media/)>
        --car FER_LaFerrari_13 [--car ...] --out <fake package dir> --work <local scratch>
        --fm4-media <FM4 Media dir>

Output: <out>/Media/DLCZips/<id>_pri_65.zip   (media/cars, media/wheels, thumbnails, CMT/ET xml,
                                                media/db/patch/<id>_merge.slt)
        <out>/Media/DLCZips/StringTables_pri_1.zip  (EN/Data_Car.str copied from the game)
Then:  python3 port_dlc_car.py <out> --fm4-db ... --fm4-user ... --out ... --work ... --install
       [--display-suffix ' (FH2)' --pack-name '...']
Audio: the car's CMT and ET xml are copied. Harmonic-tuning xmls (HT) and soundbanks they use are
reported if FM4 lacks them (bank files must then be copied by hand; see the report).
"""
import argparse, os, re, shutil, sqlite3, sys, zipfile, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip, forza_str
from port_dlc_car import raw_entries, write_zip, stored

def ci_path(root, *parts):
    cur = root
    for p in parts:
        m = {n.lower(): n for n in os.listdir(cur)}
        if p.lower() not in m: return None
        cur = os.path.join(cur, m[p.lower()])
    return cur

def zread(zp, name):
    """read a member of a Forza zip by case-insensitive exact (or trailing) name"""
    zf = zipfile.ZipFile(zp)
    low = name.lower().replace('\\', '/')
    for i in zf.infolist():
        n = i.filename.lower().replace('\\', '/')
        if n == low or n.endswith('/' + low):
            with open(zp, 'rb') as f: return forza_zip.read_entry(f, i)
    return None

def zhas(zp, name):
    low = name.lower().replace('\\', '/')
    return any(i.filename.lower().replace('\\', '/').endswith(low) for i in zipfile.ZipFile(zp).infolist())

def build_mini_db(game_db, out, cars):  # game_db: FH/FH2 gamedb.slt
    shutil.copyfile(game_db, out)
    C = sqlite3.connect(out)
    ids = {}
    for car in cars:
        r = C.execute('select Id, StockWheelID from Data_Car where MediaName=? collate nocase', (car,)).fetchone()
        if not r: raise SystemExit('car not in Data_Car: ' + car)
        ids[car] = r[0]; ids[car + ':wheel'] = r[1]
    car_ids = {ids[c] for c in cars}; wheel_ids = {ids[c + ':wheel'] for c in cars}
    eng = set(); dt = set()
    for i in car_ids:
        eng |= {r[0] for r in C.execute('select EngineID from List_UpgradeEngine where Ordinal=? and IsStock=1', (i,))}
        dt |= {r[0] for r in C.execute('select DrivetrainID from List_UpgradeDrivetrain where Ordinal=? and IsStock=1', (i,))}
    owners = car_ids | eng | dt
    tabs = [r[0] for r in C.execute("select name from sqlite_master where type='table'")]
    tmpl_offer = C.execute('select * from ContentOffers limit 1').fetchone()
    offer_cols = [r[1] for r in C.execute('pragma table_info(ContentOffers)')]
    for t in tabs:
        cols = [r[1] for r in C.execute('pragma table_info("%s")' % t)]
        lc = [c.lower() for c in cols]
        if t in ('ContentOffers', 'ContentOffersMapping'):
            C.execute('delete from "%s"' % t); continue
        keep = []
        for row in C.execute('select rowid, * from "%s"' % t).fetchall():
            d = dict(zip(lc, row[1:])); first = row[1]
            if t == 'Data_Car': ok = d['id'] in car_ids
            elif t == 'Data_Engine': ok = d['engineid'] in eng
            elif t == 'Data_Drivetrain': ok = d['drivetrainid'] in dt
            elif t == 'List_Wheels': ok = d['id'] in wheel_ids
            elif any(k in d for k in ('ordinal', 'carid', 'modelid')):
                ok = any(d.get(k) in car_ids for k in ('ordinal', 'carid', 'modelid') if k in d)
            elif 'engineid' in d: ok = d['engineid'] in eng
            elif 'drivetrainid' in d: ok = d['drivetrainid'] in dt
            elif isinstance(first, int) and first >= 1000: ok = first // 1000 in owners
            else: ok = False
            if ok: keep.append(row[0])
        if keep:
            C.execute('delete from "%s" where rowid not in (%s)' % (t, ','.join(map(str, keep))))
        else:
            C.execute('delete from "%s"' % t)
    # FH/FH2 rows may reference SHARED physics rows owned by other cars (e.g. a front bumper's AeroPhysicsID
    # pointing into another car's id range). The pack would then carry a dangling reference, which hangs the
    # FM4 race load. Copy each foreign row into this car's own id range and repoint the reference.
    G = sqlite3.connect(game_db)
    REF = {'aerophysicsid': 'List_AeroPhysics', 'antiswayphysicsid': 'List_AntiSwayPhysics',
           'springdamperphysicsid': 'List_SpringDamperPhysics', 'torquecurveid': 'List_TorqueCurve'}
    copied = []
    for t in tabs:
        cols = [r[1] for r in C.execute('pragma table_info("%s")' % t)]
        for c in cols:
            tgt = next((v for k, v in REF.items() if c.lower().endswith(k)), None)
            if not tgt or t == tgt: continue
            tcols = [r[1] for r in C.execute('pragma table_info("%s")' % tgt)]
            pk = tcols[0]
            for rowid, v, first in C.execute('select rowid, "%s", "%s" from "%s"' % (c, cols[0], t)).fetchall():
                if v is None or v < 0: continue
                if C.execute('select count(*) from "%s" where "%s"=?' % (tgt, pk), (v,)).fetchone()[0]: continue
                src = G.execute('select * from "%s" where "%s"=?' % (tgt, pk), (v,)).fetchone()
                if src is None: continue
                owner = first // 1000 if isinstance(first, int) and first >= 1000 else None
                if owner not in owners: continue
                nid = owner * 1000 + v % 1000
                while C.execute('select count(*) from "%s" where "%s"=?' % (tgt, pk), (nid,)).fetchone()[0]: nid += 1
                row = list(src); row[0] = nid
                C.execute('insert into "%s" values (%s)' % (tgt, ','.join('?' * len(row))), row)
                C.execute('update "%s" set "%s"=? where rowid=?' % (t, c), (nid, rowid))
                copied.append('%s.%s %d -> %s %d' % (t, c, v, tgt, nid))
    print('foreign refs copied:', copied or '-')
    # one synthetic offer + mapping rows (port_dlc_car.py rewrites ids/strings)
    d = dict(zip(offer_cols, tmpl_offer))
    C.execute('insert into ContentOffers (%s) values (%s)' % (','.join('[%s]' % c for c in offer_cols), ','.join('?' * len(offer_cols))),
              [d[c] for c in offer_cols])
    for k, cid in enumerate(sorted(car_ids)):
        C.execute('insert into ContentOffersMapping (Id, OfferId, ContentId, ContentType) values (?,?,?,1)', (k + 1, d['OfferId'], cid))
    C.commit(); C.execute('vacuum'); C.close()
    return ids, car_ids, eng, dt

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True); ap.add_argument('--car', action='append', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--fm4-media', required=True)
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    media = ci_path(a.game, 'media')
    cars_dir = ci_path(media, 'cars'); wheels_dir = ci_path(media, 'wheels')
    mini = os.path.join(a.work, 'mini.slt')
    ids, car_ids, eng, dt = build_mini_db(ci_path(media, 'db', 'gamedb.slt'), mini, a.car)
    print('car ids', sorted(car_ids), 'engines', sorted(eng), 'drivetrains', sorted(dt))
    entries = []; notes = []
    entries.append(stored('media/db/patch/9000_merge.slt', open(mini, 'rb').read()))
    ui_tex = ci_path(media, 'ui', 'Textures.zip')
    for car in a.car:
        cz = ci_path(cars_dir, car + '.zip')
        for info, raw in raw_entries(cz):
            if info.filename.endswith('/'): continue
            entries.append(('media/cars/%s/%s' % (car, info.filename), info.compress_type, info.CRC, info.file_size, raw))
        wz = ci_path(wheels_dir, car + '.zip')
        if not wz: raise SystemExit('no wheel zip for ' + car)
        for info, raw in raw_entries(wz):
            if info.filename.endswith('/'): continue
            entries.append(('media/wheels/%s/%s' % (car, info.filename), info.compress_type, info.CRC, info.file_size, raw))
        # thumbnail
        tn = 'Thumbnails/Thumbnail_%d.xds' % ids[car]
        th = zread(ui_tex, tn)
        if th is None: notes.append('no thumbnail ' + tn)
        else: entries.append(stored('media/UI/Textures/Thumbnails/Thumbnail_%d.xds' % ids[car], th))
        # audio
        ac = ci_path(media, 'audio', 'cars')
        cmt = zread(ci_path(ac, 'CarModelTuning.zip'), car + '_CMT.xml')
        et = zread(ci_path(ac, 'Engines', 'EngineTuning.zip'), car + '_ET.xml')
        if cmt: entries.append(stored('media/audio/cars/CarModelTuning/%s_CMT.xml' % car, cmt))
        else: notes.append('no CMT for ' + car)
        if et: entries.append(stored('media/audio/cars/Engines/EngineTuning/%s_ET.xml' % car, et))
        else: notes.append('no ET for ' + car)
        if et:
            fm4_ht = ci_path(a.fm4_media, 'audio', 'cars', 'Engines', 'HarmonicTuning.zip')
            fm4_banks = ci_path(a.fm4_media, 'audio', 'cars', 'Engines', 'Soundbanks')
            for ht in sorted(set(re.findall(r'[A-Za-z0-9_]+_HT\b', et.decode('utf-8', 'replace')))):
                if zhas(fm4_ht, ht + '.xml'):
                    hx = zread(fm4_ht, ht + '.xml')
                else:
                    hx = zread(ci_path(ac, 'Engines', 'HarmonicTuning.zip'), ht + '.xml')
                    if hx is None: notes.append('HT missing everywhere: ' + ht); continue
                    notes.append('HT added from source game: ' + ht)
                    entries.append(stored('media/audio/cars/Engines/HarmonicTuning/%s.xml' % ht, hx))
                for bank in sorted(set(re.findall(r'<Soundbank name="([^"]+)"', hx.decode('utf-8', 'replace')))):
                    have = False
                    if fm4_banks:
                        have = any(n.lower() == bank.lower() for _, _, fs in os.walk(fm4_banks) for n in fs)
                        if not have:
                            bz = [os.path.join(r, n) for r, _, fs in os.walk(os.path.dirname(fm4_banks)) for n in fs if n.lower().endswith('.zip')]
                            have = any(zhas(z, bank) for z in bz if os.path.getsize(z) < 400e6)
                    if not have: notes.append('SOUNDBANK not in FM4: %s (used by %s)' % (bank, ht))
    dz = os.path.join(a.out, 'Media', 'DLCZips'); os.makedirs(dz, exist_ok=True)
    write_zip(os.path.join(dz, '9000_pri_65.zip'), entries)
    # strings: Data_Car.str (EN) from the game
    sz = ci_path(media, 'stringtables', 'EN.zip')
    dc = zread(sz, 'Data_Car.str')
    write_zip(os.path.join(dz, 'StringTables_pri_1.zip'), [stored('EN/Data_Car.str', dc)])
    print('entries', len(entries)); print('notes', notes or '-')
    for car in a.car: print(car, 'id', ids[car])

if __name__ == '__main__':
    main()
