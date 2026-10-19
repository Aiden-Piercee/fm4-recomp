#!/usr/bin/env python3
"""make_fm2_base_pack.py - package BASE-GAME cars of Forza Motorsport 2 as a fake DLC package so
port_dlc_car.py can port them (FM2 counterpart of make_fm3_base_pack.py).

  python3 make_fm2_base_pack.py --game <FM2 root (contains Media/)> --car LOT_Elise135R_03
        --out <fake package dir> --work <local scratch> --fm4-media <FM4 Media dir>
        --donor LOT_Elise111S_05 [--real-model] [--lod0 fm2|donor] [--converter-dir <dir of carbin_fm2.py>]
        [--physics donor|fm2] [--springs donor|fm2] [--donor-pack <FM4 content package _pri_65.zip>]

FM2's gamedb is far older than FM4's (no Data_Drivetrain, no List_UpgradeDrivetrain*, different upgrade
tables), so the DB rows cannot be mapped column by column. Instead:
  * DB: every car/engine/drivetrain-owned row of the FM4 DONOR car (FM4 schema, consistent physics) is
    taken, then the Data_Car identity is replaced with the FM2 car's: MediaName, Year, MakeID,
    DisplayName/ModelShort (FM2 string hashes; the FM2 EN Data_Car.str is shipped so they resolve),
    PerformanceIndex (FM2 value) and ClassID (the FM4 class whose PI band holds that PI; FM2 class ids
    use a different scale). The wheel row is renamed to the FM2 car.  port_dlc_car.py then gives new ids.
  * Media: every file of the donor's FM4 car and wheel zips, renamed to the FM2 car (textures, cockpit,
    brakes, gauges, carattribs, physics/maxdata, liverymasks, shadersettings). With --real-model the
    main body carbin is replaced by the FM2 model converted with carbin_fm2.convert (donor main carbin as
    template), and with --lod0 fm2 (default) the _lod0 carbin is synthesised from the converted model's
    LOD1 meshes (FM2 has no LOD0), so the showroom does not show the donor's body. By default the _lod0
    holds ALL converted parts (--lod0-parts all): the showroom / pre-race preview draws only the _lod0
    model, so a part left out of it (hood, mirrors, interior, seats, side glass...) is missing there even
    though the race model has it. --lod0-parts donor keeps only the parts the donor's _lod0 has (old
    behaviour). --lod0 donor keeps the donor's LOD0 model instead.
  * --physics fm2 (default donor = all of the above unchanged): apply_fm2_physics() writes the FM2 car's own
    values into the donor's rows (same ids): torque curves (v0..vN, TorqueScale, zero-throttle scale), camshaft
    rpm limits, transmission/clutch/differential/driveline, brakes, anti-roll bars, body weight, tyre widths,
    Data_Car (weight, tyres, Sim*, ratings, steering, assists, body aero) and Data_CarBody geometry.
    Kept from the donor: tyre compounds/friction, aero parts, engine-upgrade parts, flywheel, engine mass/inertia,
    CMHeight, prices. --springs fm2 also copies FM2 spring/damper rows and ride heights (default: donor's).
    Stock forced induction (apply_fm2_aspiration): same aspiration -> FM2 boost scales in the donor's stock row;
    FM2 car naturally aspirated but donor not (e.g. Exige 05 on the supercharged Exige 240) -> the donor's stock
    DSC/turbo/intercooler rows become level-1 upgrades and AspirationID_Stock = 1; other mismatches are refused.
  * --donor-pack: donors whose car/wheel zips are only in an FM4 content package (disc-2 cars such as
    TVR_TuscanS_01, MIT_LancerEvoVIIIMR_04 in 4d53091000000002/Media/DLCZips/0002_pri_65.zip).
  * Thumbnail and audio (CMT/ET): the donor's (FM2 thumbnails/audio are not FM4 formats - unverified).
FM2 wheels (carbin v1) and FM2 textures are NOT converted.
"""
import argparse, os, re, sqlite3, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from port_dlc_car import raw_entries, write_zip, stored
import make_fh_base_pack as M

# FM2-only part names that stand in for an FM4 LOD0 part name
LOD0_ALIASES = {'glassltl': 'taillightl', 'glassrtl': 'taillightr', 'exhausta': ('exhaustl', 'exhaustr')}


def lod0_indices(x):
    """FM4 _lod0 meshes use 32-bit indices with strip restart 0x00FFFFFF (donor _lod0 files); the race model uses
    16-bit indices with restart 0xFFFF."""
    ix = [int(v) for v in x['indices']]
    restart = 0xFFFF if x['ib_fmt'] == 2 else 0xFFFFFFFF
    return [0xFFFFFF if v in (restart, 0xFFFFFF) else v for v in ix]


def make_lod0(C, conv_bytes, donor_lod0_bytes, all_parts=True):
    """Synthesise an FM4 _lod0 carbin from a converted FM2 model: root/LOD range/gauges from the donor
    LOD0 file, parts = every converted part (all_parts) or only those the donor LOD0 has (or their FM2
    stand-ins), meshes = the LOD1 meshes
    relabelled LOD 0 (vertex data moved to VertexBuffer_LOD0, indices widened to 32 bit), ordered lists = LOD1's two lists (FM4 lists: 0..4 category A LOD1-5, 5..9 category B)."""
    t = C.parse_fm4(donor_lod0_bytes); m = C.parse_fm4(conv_bytes)
    want = set()
    for p in t['parts']:
        n = p['name'].lower(); want.add(n)
        al = LOD0_ALIASES.get(n)
        if al: want |= set(al if isinstance(al, tuple) else (al,))
    out = dict(t)
    out['dc'] = m['dc']; out['material_sets'] = m['material_sets']; out['fx'] = m['fx']; out['meshnames'] = m['meshnames']
    remap = {}; parts = []
    for pi, p in enumerate(m['parts']):
        if not all_parts and p['name'].lower() not in want: continue
        meshes = []
        for mi, x in enumerate(p['meshes']):
            if x['lod'] != 1: continue
            remap[(pi, mi)] = (len(parts), len(meshes))
            meshes.append(dict(x, lod=0, ib_fmt=4, indices=lod0_indices(x)))
        if meshes:
            # FM4 _lod0 layout (as on every donor _lod0 part): vertices in VertexBuffer_LOD0, VertexBuffer empty
            parts.append(dict(p, meshes=meshes, vb=dict(count=0, stride=0, data=b''), vb_lod0=p['vb'],
                              morph=dict(count=0, stride=0, data=b''), morph_lod0=p['morph_lod0'] if p['morph_lod0']['count'] else p['morph']))
    nl = len(m['ordered']) // 2
    out['ordered'] = [[remap[r] for r in m['ordered'][0] if r in remap],
                      [remap[r] for r in m['ordered'][nl] if r in remap]]
    out['parts'] = parts
    data = C.write_fm4(out); C.parse_fm4(data)
    return data, [p['name'] for p in parts]


def prune_torque_curves(mini, eng):
    """build_mini_db keeps List_TorqueCurve rows whose id//1000 is ANY owner (car, engine or drivetrain).
    FM4 keys torque curves by engine*1000+level, so a curve whose owner equals this car's drivetrain id
    belongs to another car's engine (e.g. drivetrain 84 vs engine 84). Keep only this car's engines' curves."""
    C = sqlite3.connect(mini)
    rows = C.execute('select TorqueCurveID from List_TorqueCurve').fetchall()
    gone = [v for (v,) in rows if v // 1000 not in eng]
    for v in gone: C.execute('delete from List_TorqueCurve where TorqueCurveID=?', (v,))
    C.commit(); C.execute('vacuum'); C.close()
    print('foreign torque curves removed:', gone or '-')


# ----------------------------------------------------------------------------------------------------
# --physics fm2: FM2 physics written into the donor's FM4 rows (same row set and ids, so every foreign key
# the donor rows have stays valid; port_dlc_car.py then moves them to the new car/engine/drivetrain ids).
# Anything without a clean FM2 -> FM4 mapping keeps the donor's value (see FM2_PHYS_* below).
# Checked against FM2 vs FM4 Elise 111S (same car in both games): spring rates and anti-roll stiffness are
# on the same scale (stock front 36.1 vs 37.5 N/mm, rear 56.4 vs 58.7); dampers and clamps are not
# (bump 1.77 vs 2.70, clamp 30 vs 10); Data_Engine EngineMass/MomentInertia/GasTankSize and weight
# CMHeight use a different convention (284 vs 159 kg, 0.153 vs 0.090, 20 vs 40, 0.23 vs 0.41) -> donor.
FM2_CAR_COLS = (['CurbWeight', 'Displacement', 'NumGears', 'WeightDistribution', 'DriveTypeID',
                 'FrontTireWidthMM', 'FrontTireAspect', 'FrontWheelDiameterIN', 'RearTireWidthMM', 'RearTireAspect', 'RearWheelDiameterIN',
                 'HandlingRating', 'SpeedRating', 'AccelerationRating', 'BrakingRating',
                 'Time:0-60-sec', 'Time:0-100-sec', 'QuarterMileTime-sec', 'QuarterMileSpeed-mph', 'TopSpeed-mph',
                 'AssistsTCSSlipDefTakeoff', 'AssistsTCSSlipDefMoving'])        # + Sim*, Steer*, BodyAero* (prefix match)
FM2_CAR_PREFIX = ('Sim', 'Steer', 'BodyAero')
FM2_ENGINE_COLS = ['Compression', 'EngineGraphingMaxTorque', 'EngineGraphingMaxPower']
FM2_CAM_COLS = ['RedlineRPM', 'StallRPM', 'StartRPM', 'RedlineWearSpeed', 'TorqueCurveMaxRPM', 'NumRPMEntriesArray']
FM2_TRANS_COLS = ['MomentInertia', 'GearShiftTime', 'FinalDriveRatio', 'NumGears'] + ['GearRatio%d' % i for i in range(9)]
FM2_CLUTCH_COLS = ['ClutchMaxTorque', 'ClutchInTime', 'ClutchOutTime']
FM2_DIFF_COLS = ['%sLimitedSlip%s' % (a, b) for a in ('Front', 'Rear', 'Center')
                 for b in ('TorqueAccel', 'TorqueDecel', 'RelVelClamp', 'AccelDefInputTorque')] + ['RearToqueSplit']
FM2_BRAKE_COLS = ['FrontBrakeSize', 'FrontBrakeTypeID', 'FrontCaliperPistons', 'RearBrakeSize', 'RearBrakeTypeID', 'RearCaliperPistons',
                  'GameFrictionScaleBraking', 'BrakeTorqueSlider', 'BrakeBiasSlider', 'BiasHandbrake', 'ReleasePointABS', 'DurationABS',
                  'ReleasePointABSFrontNoSteer', 'ReleasePointABSFrontFullSteer', 'ReleasePointABSRearNoSteer', 'ReleasePointABSRearFullSteer']
FM2_WEIGHT_COLS = ['Mass', 'InitialMass', 'CMBackFront', 'CMLeftRight', 'BlockDimX', 'BlockDimY', 'BlockDimZ']
FM2_ASW_COLS = ['DefSwaybarStiffness', 'MinSwaybarStiffness', 'MaxSwaybarStiffness', 'SwaybarDamping']
FM2_SD_SKIP = ('springdamperphysicsid', 'ordinal')           # every other shared column of List_SpringDamperPhysics is copied


def _rows(D, sql, args=()):
    cur = D.execute(sql, args); cols = [c[0] for c in cur.description]
    return [dict(zip(cols, r)) for r in cur.fetchall()]


def _set(C, table, pk, pkval, src, cols, log):
    """update FM4 row table[pk=pkval] with src[col] for each col (FM2 column names matched case-insensitively)"""
    tcols = {r[1].lower(): r[1] for r in C.execute('pragma table_info("%s")' % table)}
    sl = {k.lower(): v for k, v in src.items()}
    upd = [(tcols[c.lower()], sl[c.lower()]) for c in cols if c.lower() in tcols and c.lower() in sl and sl[c.lower()] is not None]
    if not upd: return 0
    C.execute('update "%s" set %s where "%s"=?' % (table, ','.join('"%s"=?' % c for c, _ in upd), pk), [v for _, v in upd] + [pkval])
    log.append('%s %s: %d cols' % (table, pkval, len(upd)))
    return len(upd)


def _pair(fm4, fm2):
    """pair FM4 donor upgrade rows with FM2 rows: stock with stock, then the rest by Level. -> [(fm4row, fm2row|None)]"""
    s2 = [r for r in fm2 if r['IsStock']]; by_lvl = {r['Level']: r for r in fm2 if not r['IsStock']}
    return [(r, s2[0] if r['IsStock'] and s2 else (None if r['IsStock'] else by_lvl.get(r['Level']))) for r in fm4]


# Stock forced induction. Both games store a naturally aspirated base torque curve and apply the stock
# supercharger/turbo row on top (FM2 Exige 240: 203 Nm x DSC 1.204; FM4 Exige 240: 184 Nm x DSC 1.35), so an FM2
# NA curve under a donor's stock DSC/turbo row is over-boosted. FM2 aspiration ids equal FM4's (1 Natural, 2 Turbo,
# 3 Twin, 4 Quad, 5 DSC, 6 CSC). TorqueDropOff* use a different convention (FM2 8000/12000/0.7, FM4 9000/15000/0.75)
# and stay the donor's.
FM2_FI_TABLE = {2: 'List_UpgradeEngineTurboSingle', 3: 'List_UpgradeEngineTurboTwin', 4: 'List_UpgradeEngineTurboQuad',
                5: 'List_UpgradeEngineDSC', 6: 'List_UpgradeEngineCSC'}
FM2_FI_SRC = {2: 'List_UpgradeEngineTurbo', 3: 'List_UpgradeEngineTurbo', 4: 'List_UpgradeEngineTurbo',
              5: 'List_UpgradeEngineDSC', 6: 'List_UpgradeEngineCSC'}
FM2_FI_COLS = ['MomentInertia', 'MaxScale', 'PowerMaxScale', 'MinScale', 'PowerMinScale', 'RobScale', 'ZeroRPMScale', 'RedlineRPMScale']


def apply_fm2_aspiration(C, G, e4, e2, log, kept):
    """same stock aspiration: FM2 boost scales into the donor's stock FI row. FM2 naturally aspirated, donor not:
    the donor's stock FI and intercooler rows become ordinary level-1 upgrades (price = half the level-2 price, as on
    FM4 NA cars, e.g. Elise 111S DSC 2200/4400, intercooler 800/1600) and AspirationID_Stock becomes 1. Any other
    mismatch is refused (pick a donor with the FM2 car's aspiration). Same-aspiration NA cars: no change."""
    a2 = G.execute('select AspirationID_Stock from Data_Engine where EngineID=?', (e2,)).fetchone()[0]
    a4 = C.execute('select AspirationID_Stock from Data_Engine where EngineID=?', (e4,)).fetchone()[0]
    if a2 == a4:
        if a2 in FM2_FI_TABLE:
            r2 = _rows(G, 'select * from "%s" where EngineID=? and IsStock=1' % FM2_FI_SRC[a2], (e2,))
            r4 = _rows(C, 'select * from "%s" where EngineID=? and IsStock=1' % FM2_FI_TABLE[a2], (e4,))
            if r2 and r4: _set(C, FM2_FI_TABLE[a2], 'Id', r4[0]['Id'], r2[0], FM2_FI_COLS, log)
            else: kept.append('%s stock row (FM2 %d, donor %d rows)' % (FM2_FI_TABLE[a2], len(r2), len(r4)))
        return
    if a2 != 1:
        raise SystemExit('FM2 stock aspiration %d differs from the donor engine %d (%d): pick a donor with the same aspiration'
                         % (a2, e4, a4))
    for t in list(FM2_FI_TABLE.values()) + ['List_UpgradeEngineIntercooler']:
        for r in _rows(C, 'select * from "%s" where EngineID=? and IsStock=1' % t, (e4,)):
            nxt = C.execute('select Price from "%s" where EngineID=? and Level=? and IsStock=0' % t, (e4, r['Level'] + 1)).fetchone()
            price = int(nxt[0] // 2) if nxt else r['Price']
            C.execute('update "%s" set IsStock=0, Price=? where Id=?' % t, (price, r['Id']))
            log.append('%s %d: donor stock row -> level %d upgrade, price %d (FM2 car is naturally aspirated)' % (t, r['Id'], r['Level'], price))
    C.execute('update Data_Engine set AspirationID_Stock=1 where EngineID=?', (e4,))
    log.append('Data_Engine %d: AspirationID_Stock %d -> 1' % (e4, a4))


def apply_fm2_physics(mini, fm2_db, src, donor_id, eng, dt, springs='donor'):
    """--physics fm2: overwrite the donor's physics rows in the mini DB with the FM2 car's values where they map
    cleanly; keep the donor's elsewhere. springs: 'donor' keeps the donor's spring/damper rows and ride heights,
    'fm2' copies FM2's List_SpringDamperPhysics (rates, dampers, clamps, bumpstops, geometry, ride heights)."""
    G = sqlite3.connect('file:%s?mode=ro' % fm2_db, uri=True)
    C = sqlite3.connect(mini); log = []; kept = []
    cid = src['Id']; e4 = sorted(eng)[0]; d4 = sorted(dt)[0]
    e2 = G.execute('select EngineID from List_UpgradeEngine where Ordinal=? and IsStock=1', (cid,)).fetchone()[0]
    body = C.execute('select CarBodyID from List_UpgradeCarBody where Ordinal=? and IsStock=1', (donor_id,)).fetchone()[0]
    eng2 = _rows(G, 'select * from Data_Engine where EngineID=?', (e2,))[0]

    # Data_Car
    car4 = [r[1] for r in C.execute('pragma table_info(Data_Car)')]
    s = dict(src)
    s.update({'Displacement': eng2['Disp_cc'], 'NumGears': src['StockGears'], 'WeightDistribution': src['FrontWeight-pct'],
              'FrontTireWidthMM': src['StockTire-FrontWidth-mm'], 'FrontTireAspect': src['StockTire-FrontAspect-pct'],
              'FrontWheelDiameterIN': src['StockTire-FrontWheel-in'], 'RearTireWidthMM': src['StockTire-RearWidth-mm'],
              'RearTireAspect': src['StockTire-RearAspect-pct'], 'RearWheelDiameterIN': src['StockTire-RearWheel-in'],
              'FrontStockRideHeight': src['ModelFrontStockRideHeight'], 'RearStockRideHeight': src['ModelRearStockRideHeight']})
    pt = G.execute('select DrivetrainId from Powertrains where Id=?', (src['PowertrainID'],)).fetchone()
    if pt: s['DriveTypeID'] = pt[0]
    cols = FM2_CAR_COLS + [c for c in car4 if c.startswith(FM2_CAR_PREFIX) and c in src]
    if springs == 'fm2': cols += ['FrontStockRideHeight', 'RearStockRideHeight']
    _set(C, 'Data_Car', 'Id', donor_id, s, cols, log)
    # Data_CarBody (model geometry; ride heights follow --springs)
    cb = {'ModelWheelbase': src['ModelWheelbase'], 'ModelFrontTrackOuter': src['ModelFrontTrackOuter'],
          'ModelRearTrackOuter': src['ModelRearTrackOuter'], 'BottomCenterWheelbasePosx': src['BottomCenterWheelbasePosx'],
          'BottomCenterWheelbasePosy': src['BottomCenterWheelbasePosy'], 'BottomCenterWheelbasePosZ': src['BottomCenterWheelbasePosz'],
          'ModelFrontStockRideHeight': src['ModelFrontStockRideHeight'], 'ModelRearStockRideHeight': src['ModelRearStockRideHeight']}
    _set(C, 'Data_CarBody', 'Id', body, cb, [k for k in cb if springs == 'fm2' or 'RideHeight' not in k], log)
    # Data_Drivetrain
    if pt: _set(C, 'Data_Drivetrain', 'DrivetrainID', d4, {'DrivetypeID': pt[0]}, ['DrivetypeID'], log)
    # engine
    _set(C, 'Data_Engine', 'EngineID', e4, eng2, FM2_ENGINE_COLS, log)
    apply_fm2_aspiration(C, G, e4, e2, log, kept)
    cam4 = _rows(C, 'select * from List_UpgradeEngineCamshaft where EngineID=? order by Level', (e4,))
    cam2 = _rows(G, 'select * from List_UpgradeEngineCamValves where EngineID=? order by Level', (e2,))
    for r4, r2 in _pair(cam4, cam2):
        if r2 is None: kept.append('List_UpgradeEngineCamshaft %d (no FM2 level %d)' % (r4['Id'], r4['Level'])); continue
        _set(C, 'List_UpgradeEngineCamshaft', 'Id', r4['Id'], r2, FM2_CAM_COLS, log)
        full = _rows(G, 'select * from List_TorqueCurve where TorqueCurveID=?', (r2['TorqueCurveFullThrottleID'],))[0]
        zero = G.execute('select TorqueScale from List_TorqueCurve where TorqueCurveID=?', (r2['TorqueCurveZeroThrottleID'],)).fetchone()
        n = int(full['NumTorqueValues'])
        nv = len([c for c in C.execute('pragma table_info(List_TorqueCurve)') if re.match(r'v\d+$', c[1])])
        if n > nv: raise SystemExit('FM2 torque curve %d has %d points, FM4 holds %d' % (full['TorqueCurveID'], n, nv))
        tc = {'TorqueScale': full['TorqueScale'], 'NumTorqueValues': n, 'ZeroThrottleTorqueScale': zero[0] if zero else None}
        tc.update({'v%d' % i: (full['v%d' % i] if i < n else 0.0) for i in range(nv)})   # v[n-1] is the -2.63 end marker in both games
        _set(C, 'List_TorqueCurve', 'TorqueCurveID', r4['TorqueCurveFullThrottleID'], tc, list(tc), log)
    # drivetrain
    for t4, t2, cols, fk in (('List_UpgradeDrivetrainTransmission', 'List_UpgradeTransmission', FM2_TRANS_COLS, 'DrivetrainID'),
                             ('List_UpgradeDrivetrainClutch', 'List_UpgradeClutch', FM2_CLUTCH_COLS, 'DrivetrainID'),
                             ('List_UpgradeDrivetrainDifferential', 'List_UpgradeDifferential', FM2_DIFF_COLS, 'DrivetrainID'),
                             ('List_UpgradeDrivetrainDriveline', 'List_UpgradeDriveline', ['MomentInertia'], 'DrivetrainId')):
        for r4, r2 in _pair(_rows(C, 'select * from "%s" where "%s"=? order by Level' % (t4, fk), (d4,)),
                            _rows(G, 'select * from "%s" where Ordinal=? order by Level' % t2, (cid,))):
            if r2 is None: kept.append('%s %d' % (t4, r4['Id'])); continue
            if t4 == 'List_UpgradeDrivetrainTransmission': r2 = dict(r2, GearRatio8=r2.get('GearRatio8', -1.0))
            _set(C, t4, 'Id', r4['Id'], r2, cols, log)
    # brakes, weight
    for t4, t2, cols, w4 in (('List_UpgradeBrakes', 'List_UpgradeBrakes', FM2_BRAKE_COLS, ('Ordinal', donor_id)),
                             ('List_UpgradeCarBodyWeight', 'List_UpgradeWeight', FM2_WEIGHT_COLS, ('CarBodyId', body))):
        for r4, r2 in _pair(_rows(C, 'select * from "%s" where "%s"=? order by Level' % (t4, w4[0]), (w4[1],)),
                            _rows(G, 'select * from "%s" where Ordinal=? order by Level' % t2, (cid,))):
            if r2 is None: kept.append('%s %d' % (t4, r4['Id'])); continue
            _set(C, t4, 'Id', r4['Id'], r2, cols, log)
    # anti-roll bars: FM2 one table (front+rear ids) -> FM4 front and rear tables
    asw2 = _rows(G, 'select * from List_UpgradeAntiSway where Ordinal=? order by Level', (cid,))
    for side in ('Front', 'Rear'):
        rows4 = _rows(C, 'select * from List_UpgradeAntiSway%s where Ordinal=? order by Level' % side, (donor_id,))
        done = {}
        for r4, r2 in _pair(rows4, asw2):
            if r2 is None: continue
            p2 = _rows(G, 'select * from List_AntiSwayPhysics where AntiSwayPhysicsID=?', (r2[side + 'AntiSwayPhysicsID'],))[0]
            _set(C, 'List_AntiSwayPhysics', 'AntiSwayPhysicsID', r4['AntiSwayPhysicsID'], p2, FM2_ASW_COLS, log)
            done[r4['Level']] = p2
        for r4 in rows4:          # FM4-only level (rear level 1): midway between its FM2-mapped neighbours
            if r4['Level'] in done: continue
            lo = max((l for l in done if l < r4['Level']), default=None); hi = min((l for l in done if l > r4['Level']), default=None)
            if lo is None or hi is None: kept.append('List_AntiSwayPhysics %d' % r4['AntiSwayPhysicsID']); continue
            k = done[lo]['DefSwaybarStiffness'] + (done[hi]['DefSwaybarStiffness'] - done[lo]['DefSwaybarStiffness']) * (r4['Level'] - lo) / (hi - lo)
            mid = {'DefSwaybarStiffness': k, 'MinSwaybarStiffness': k, 'MaxSwaybarStiffness': k, 'SwaybarDamping': done[lo]['SwaybarDamping']}
            _set(C, 'List_AntiSwayPhysics', 'AntiSwayPhysicsID', r4['AntiSwayPhysicsID'], mid, FM2_ASW_COLS, log)
            log[-1] += ' (interpolated, no FM2 level %d)' % r4['Level']
    # springs / dampers
    if springs == 'fm2':
        sd4cols = [r[1] for r in C.execute('pragma table_info(List_SpringDamperPhysics)') if r[1].lower() not in FM2_SD_SKIP]
        for r4, r2 in _pair(_rows(C, 'select * from List_UpgradeSpringDamper where Ordinal=? order by Level', (donor_id,)),
                            _rows(G, 'select * from List_UpgradeSpringDamper where Ordinal=? order by Level', (cid,))):
            if r2 is None: kept.append('List_UpgradeSpringDamper %d' % r4['Id']); continue
            for side in ('Front', 'Rear'):
                p2 = _rows(G, 'select * from List_SpringDamperPhysics where SpringDamperPhysicsID=?', (r2[side + 'SpringDamperPhysicsID'],))[0]
                _set(C, 'List_SpringDamperPhysics', 'SpringDamperPhysicsID', r4[side + 'SpringDamperPhysicsID'], p2, sd4cols, log)
    # tyre widths: FM2 one table (front+rear) -> FM4 front and rear tables, ids <carbody>+level as the donor's
    tw2 = _rows(G, 'select * from List_UpgradeTireWidth where Ordinal=? order by IsStock desc, Level', (cid,))
    for side in ('Front', 'Rear'):
        t4 = 'List_UpgradeCarBodyTireWidth' + side
        old = {r['Level']: r for r in _rows(C, 'select * from "%s" where CarBodyId=?' % t4, (body,))}
        C.execute('delete from "%s" where CarBodyId=?' % t4, (body,))
        for lvl, r2 in enumerate(tw2):
            d = dict(old.get(lvl) or old[max(old)])
            d.update({'Id': body + lvl, 'Level': lvl, 'IsStock': r2['IsStock'], side + 'TireWidth': r2[side + 'TireWidth'],
                      'MassDiff': r2['MassDiff'] / 2.0, 'DragScale': r2['DragScale'], 'WindInstabilityScale': r2['WindInstabilityScale']})
            C.execute('insert into "%s" (%s) values (%s)' % (t4, ','.join('"%s"' % k for k in d), ','.join('?' * len(d))), list(d.values()))
        log.append('%s: %d rows from FM2 (was %d)' % (t4, len(tw2), len(old)))
    C.commit(); C.execute('vacuum'); C.close(); G.close()
    print('FM2 physics (springs=%s): FM2 car %d engine %d -> donor car %d engine %d drivetrain %d body %d'
          % (springs, cid, e2, donor_id, e4, d4, body))
    for l in log: print('  fm2:', l)
    print('  kept donor rows:', kept or '-')


def donor_zip_from_pack(packs, kind, donor, work):
    """FM4 disc-2 cars (e.g. TVR_TuscanS_01, MIT_LancerEvoVIIIMR_04) have DB rows, thumbnails and audio in the base
    game but their car/wheel files only inside a content package zip under media/<kind>/<donor>/. Copy those entries
    (raw, compression kept) into <work>/donor_<kind>_<donor>.zip, laid out like a base-game car zip."""
    pre = 'media/%s/%s/' % (kind, donor.lower())
    for pk in packs:
        ents = [(i.filename[len(pre):], i.compress_type, i.CRC, i.file_size, raw) for i, raw in raw_entries(pk)
                if i.filename.lower().replace('\\', '/').startswith(pre) and not i.filename.endswith('/')]
        if ents:
            out = os.path.join(work, 'donor_%s_%s.zip' % (kind, donor))
            write_zip(out, ents)
            print('donor %s: %d entries from %s' % (kind, len(ents), pk))
            return out
    return None


def fm2_car(fm2_db, car):
    G = sqlite3.connect('file:%s?mode=ro' % fm2_db, uri=True)
    cols = [r[1] for r in G.execute('pragma table_info(Data_Car)')]
    r = G.execute('select * from Data_Car where MediaName=? collate nocase', (car,)).fetchone()
    if not r: raise SystemExit('car not in FM2 Data_Car: ' + car)
    d = dict(zip(cols, r))
    mk = G.execute('select * from List_CarMake where ID=?', (d['MakeID'],)).fetchone()
    G.close()
    return d, mk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True); ap.add_argument('--car', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--fm4-media', required=True)
    ap.add_argument('--donor', required=True, help='FM4 car (MediaName) giving DB physics, textures, cockpit, brakes, wheels, audio')
    ap.add_argument('--real-model', action='store_true', help='convert the FM2 body carbin with carbin_fm2.convert')
    ap.add_argument('--lod0', choices=('fm2', 'donor'), default='fm2')
    ap.add_argument('--lod0-parts', choices=('all', 'donor'), default='all',
                    help='fm2 _lod0: every converted part (default) or only the parts the donor _lod0 has')
    ap.add_argument('--physics', choices=('donor', 'fm2'), default='donor',
                    help='donor (default): all physics rows from the FM4 donor. fm2: FM2 car physics where it maps cleanly, donor elsewhere')
    ap.add_argument('--springs', choices=('donor', 'fm2'), default='donor',
                    help='with --physics fm2: spring/damper rows and ride heights from the donor (default) or from FM2 (damper scale unverified)')
    ap.add_argument('--donor-pack', action='append', default=[],
                    help='FM4 content package zip (e.g. disc-2 <pkg>/Media/DLCZips/0002_pri_65.zip) holding media/cars/<donor>/ and '
                         'media/wheels/<donor>/, for donors whose car zip is not in <fm4-media>/cars (repeatable)')
    ap.add_argument('--converter-dir', help='folder holding carbin_fm2.py (default: $FM2_CONVERTER_DIR or <fm4-media>/../../fm2_converter)')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    media = M.ci_path(a.game, 'media')
    fm2_db = M.ci_path(media, 'db', 'gamedb.slt')
    fm4_db = M.ci_path(a.fm4_media, 'db', 'gamedb.slt')
    src, mk2 = fm2_car(fm2_db, a.car)
    car = src['MediaName']; cn = car.lower()

    F = sqlite3.connect('file:%s?mode=ro' % fm4_db, uri=True)
    if F.execute('select count(*) from Data_Car where MediaName=? collate nocase', (car,)).fetchone()[0]:
        raise SystemExit('%s is already an FM4 base car' % car)
    dr = F.execute('select Id, MediaName from Data_Car where MediaName=? collate nocase', (a.donor,)).fetchone()
    if not dr: raise SystemExit('donor not in FM4 Data_Car: ' + a.donor)
    donor_id, donor = dr; dn = donor.lower()
    mk4 = F.execute('select ID from List_CarMake where ID=? and ManufacturerCode=?', (src['MakeID'], mk2[2])).fetchone()
    if not mk4: raise SystemExit('FM2 make %s (%s) has no matching FM4 make' % (src['MakeID'], mk2[2]))
    cls = F.execute('select Id from CarClasses where MaxPerformanceIndex>=? order by Id limit 1', (src['PerformanceIndex'],)).fetchone()[0]
    F.close()

    # --- DB: donor rows (FM4 schema), FM2 identity
    mini = os.path.join(a.work, 'mini.slt')
    ids, car_ids, eng, dt = M.build_mini_db(fm4_db, mini, [donor])
    prune_torque_curves(mini, eng)
    C = sqlite3.connect(mini)
    # Car-name keys must sit in the 0xCD49 namespace used by every FM4 car name (FM2 keys use 0xA949 and do not resolve).
    def _cd49(v):
        n = int(str(v)[2:]) if str(v).startswith('_&') else int(v)
        return '_&%d' % ((0xCD49 << 16) | (n & 0xFFFF))
    C.execute('update Data_Car set MediaName=?, Year=?, MakeID=?, DisplayName=?, ModelShort=?, PerformanceIndex=?, ClassID=? where Id=?',
              (car, src['Year'], src['MakeID'], _cd49(src['DisplayName']), _cd49(src['ModelShort']), src['PerformanceIndex'], cls, donor_id))
    C.execute('update List_Wheels set MediaName=?, IconPath=? where ID=?', (car, car, ids[donor + ':wheel']))
    C.commit(); C.execute('vacuum'); C.close()
    if a.physics == 'fm2':
        apply_fm2_physics(mini, fm2_db, src, donor_id, eng, dt, a.springs)
    print('FM2 car %s id %d year %d make %d PI %.4f FM2 class %d -> FM4 class %d; donor %s id %d engines %s drivetrains %s'
          % (car, src['Id'], src['Year'], src['MakeID'], src['PerformanceIndex'], src['ClassID'], cls, donor, donor_id, sorted(eng), sorted(dt)))

    # --- media
    def ren(n): return n.replace(dn, cn).replace(donor, car)
    entries = [stored('media/db/patch/9000_merge.slt', open(mini, 'rb').read())]; notes = []
    skip = lambda ln: ln in ('buildnumber.txt', 'versiondata.xml') or ln.endswith('build_report.html')
    dz = M.ci_path(a.fm4_media, 'cars', donor + '.zip') or donor_zip_from_pack(a.donor_pack, 'cars', donor, a.work)
    wz = M.ci_path(a.fm4_media, 'wheels', donor + '.zip') or donor_zip_from_pack(a.donor_pack, 'wheels', donor, a.work)
    if not dz or not wz: raise SystemExit('donor %s: car or wheel zip not in %s and not in --donor-pack' % (donor, a.fm4_media))
    conv = None
    if a.real_model:
        cdir = a.converter_dir or os.environ.get('FM2_CONVERTER_DIR') or \
            os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(a.fm4_media))), 'fm2_converter')
        sys.path.insert(0, cdir); import carbin_fm2 as CF
        fz = M.ci_path(media, 'cars', car + '.zip')
        fm2_model = M.zread(fz, cn + '.carbin')
        if fm2_model is None: raise SystemExit('no %s.carbin in %s' % (cn, fz))
        conv, _ = CF.convert(fm2_model, M.zread(dz, dn + '.carbin'), log=lambda *x: None)
        nparts = len(CF.parse_fm4(conv)['parts'])
        notes.append('body: FM2 model converted (%d parts, %d B)' % (nparts, len(conv)))
        if a.lod0 == 'fm2':
            lod0, names = make_lod0(CF, conv, M.zread(dz, dn + '_lod0.carbin'), a.lod0_parts == 'all')
            notes.append('lod0: synthesised from FM2 LOD1 (%d parts: %s)' % (len(names), ','.join(names)))
        else:
            lod0 = None; notes.append('lod0: donor')
    for info, raw in raw_entries(dz):
        n = info.filename; ln = n.lower()
        if n.endswith('/') or skip(ln): continue
        dst = 'media/cars/%s/%s' % (car, ren(n))
        if conv is not None and ln == dn + '.carbin':
            entries.append(stored(dst, conv)); continue
        if conv is not None and lod0 is not None and ln == dn + '_lod0.carbin':
            entries.append(stored(dst, lod0)); continue
        entries.append((dst, info.compress_type, info.CRC, info.file_size, raw))
    for info, raw in raw_entries(wz):
        n = info.filename
        if n.endswith('/') or skip(n.lower()): continue
        entries.append(('media/wheels/%s/%s' % (car, ren(n)), info.compress_type, info.CRC, info.file_size, raw))
    th = M.zread(M.ci_path(a.fm4_media, 'ui', 'Textures.zip'), 'Thumbnails/Thumbnail_%d.xds' % donor_id)
    if th is None: notes.append('no donor thumbnail')
    else: entries.append(stored('media/UI/Textures/Thumbnails/Thumbnail_%d.xds' % donor_id, th))
    ac = M.ci_path(a.fm4_media, 'audio', 'cars')
    cmt = M.zread(M.ci_path(ac, 'CarModelTuning.zip'), donor + '_CMT.xml')
    et = M.zread(M.ci_path(ac, 'Engines', 'EngineTuning.zip'), donor + '_ET.xml')
    if cmt: entries.append(stored('media/audio/cars/CarModelTuning/%s_CMT.xml' % car, cmt))
    else: notes.append('no donor CMT')
    if et: entries.append(stored('media/audio/cars/Engines/EngineTuning/%s_ET.xml' % car, et))
    else: notes.append('no donor ET')
    od = os.path.join(a.out, 'Media', 'DLCZips'); os.makedirs(od, exist_ok=True)
    write_zip(os.path.join(od, '9000_pri_65.zip'), entries)
    dc = M.zread(M.ci_path(media, 'stringtables', 'EN.zip'), 'Data_Car.str')       # FM2 strings: DisplayName hashes resolve here
    write_zip(os.path.join(od, 'StringTables_pri_1.zip'), [stored('EN/Data_Car.str', dc)])
    print('entries', len(entries)); print('notes', notes or '-')
    print(car, 'id', donor_id, '(donor id; port_dlc_car.py assigns the new id)')


if __name__ == '__main__':
    main()
