#!/usr/bin/env python3
"""make_ai_drivers_pack.py - build (and optionally install) an FM4 DLC-style pack that turns the
AI drivers into real racing drivers with their real countries, and adds a few hundred more.

  python3 make_ai_drivers_pack.py --fm4-media <FM4 Media dir> --fm4-user <.local/user dir>
        --out <staging dir> --work <local scratch dir> [--install]
        [--offer 130] [--offer-id 4D530910F4FF7900] [--str-pri 25500] [--first-id 1000]

Driver list: ai_drivers_data.py (per region, strongest first).
Also turns AI rubber-banding off and gives drivers personalities (DYNAMIC_AI_SQL below;
--no-dynamic-ai leaves those stock).

How FM4 picks AI (default.xex):
  * Career / grid AI: one driver per grid slot with
      SELECT ... FROM AIPlayers JOIN List_Country ... WHERE RegionID=%d AND Rank=%d   (0x82021508)
    and the game takes the first row. Region 1 Asia, 2 Europe (+Oceania, Africa), 3 Americas;
    Rank 1..16 (1 = strongest). The fm4 hook "AiDriverPoolQuery" (fm4/src/hooks.cpp) adds
    ORDER BY RANDOM() so every driver sharing a region and rank can be picked. Without that
    hook the new drivers are in the DB but only the first driver per region/rank is used.
  * Other modes: a random pick from DriverTypeID = 3 rows (0x8201b434, 0x82020e78). Those 73
    rows (Turn 10 staff names, no country) are renamed to real drivers here; their count is
    unchanged.

What the pack contains:
  * <offer>_pri_65.zip : media/db/patch/<offer>000_merge.slt (one ContentOffers row) and
    media/db/patch/<offer>NNN_execute.sql chunks (the native FM4 "execute" DB patch, as in the
    Porsche Expansion's 100001_execute.sql): List_Country flag fixes and new countries,
    UPDATEs of the existing AIPlayers rows (names, country), INSERTs of the new AIPlayers rows
    and their AIPlayerColors.
  * StringTables_pri_<str-pri>.zip : AIPlayers.str and List_Country.str for every language
    (English text: driver names, short in-game bios, new country names), built on the highest
    installed copy (offer 37 ships List_Country at 25037), so --str-pri must stay above that.
    ContentOffers.str is left alone so other packs' priorities are not disturbed (the pack's
    entry in the DLC list therefore has no name).
"""
import argparse, glob, hashlib, os, random, re, shutil, sqlite3, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip, forza_str, forza_str_write as SW
from port_dlc_car import write_zip, stored, read_member, free_hashes, latest_tables, TITLE
import ai_drivers_data as D

LANGS = ['CHT', 'DE', 'DEV', 'EN', 'ES', 'FR', 'GB', 'HU', 'IT', 'JP', 'KO', 'LOC', 'MX', 'PL', 'RU', 'br', 'cz', 'nl']
TYPE3_COUNT_HINT = 6          # every 6th driver of each list goes to the DriverTypeID 3 pool
SQL_CHUNK = 3800              # bytes per execute.sql (the shipped FM4 one is 4.4 KB)
PALETTE = [0xC8102E, 0x0033A0, 0xFFD100, 0x00843D, 0xFF6A13, 0x6D2077, 0x111111, 0xF2F2F2, 0x8A8D8F,
           0x00A3E0, 0x7A0019, 0x2D5016, 0xB5A642, 0x004C45, 0xE10098, 0x3A3A3A, 0xA6192E, 0x1D4F91]

# Dynamic AI block (claude/dynamic-ai-2026-10-13.md). Appended after the roster SQL.
#  * Rubber-banding off: every AIRubberbands row set like row 15 (the game's own "off" row:
#    effect from 4-5 km, no torque cut), only the neutral rubber-band mode (0) is ever picked,
#    and the "far behind" torque boost in AISkills is neutral (1.0). The constant power handicap
#    of the weak skill levels (NominalTorqueScale) is kept: it is the difficulty, not catch-up.
#  * Personalities for career drivers (DriverTypeID 1), fixed per driver Id:
#    SkillModifier (higher = slower) gets -1/0/0/0/+1/+1/+2; AggroModifier (higher = calmer)
#    gets chargers -3 (30%), normal (40%), clean +3 (30%). The game clamps the sums (skill 0..20,
#    temperament 0..10) and the AiFormSkill/AiFormTemper hooks add race-day form on top.
DYNAMIC_AI_SQL = [
    "UPDATE AIRubberbands SET StartRubberbandingDistance=4000,MaxRubberbandingEffectDistance=5000,"
    "StartTorqueCutDistance=4000,MaxTorqueCutDistance=5000,TorqueCutFactor=0;",
    "UPDATE AIRubberbandModeChoices SET RelativeFrequency=CASE WHEN AIRubberbandModes_id=0 THEN 1.0 ELSE 0.0 END,"
    "Amount=CASE WHEN AIRubberbandModes_id=0 THEN Amount ELSE 0.0 END;",
    "UPDATE AISkills SET SoloLapping_FarBehindTorqueBoost=1.0,SoloLapping_TorqueBoostCheatLevel=0;",
    "UPDATE AIPlayers SET SkillModifier=MAX(0,SkillModifier+CASE (Id*7919)%7 WHEN 0 THEN -1 WHEN 4 THEN 1 "
    "WHEN 5 THEN 1 WHEN 6 THEN 2 ELSE 0 END) WHERE DriverTypeID=1;",
    "UPDATE AIPlayers SET AggroModifier=AggroModifier+CASE WHEN (Id*104729)%10<3 THEN -3 "
    "WHEN (Id*104729)%10>6 THEN 3 ELSE 0 END WHERE DriverTypeID=1;",
]

def q(s): return "'" + s.replace("'", "''") + "'"

def display(d, collide):
    if d['eastern']: return '%s %s' % (d['last'], d['first'])
    if collide or '.' in d['first']: return '%s %s' % (d['first'], d['last'])
    return '%s. %s' % (d['first'][0], d['last'])

BIO = {
    0: ['One of the fastest drivers in the field, {n} races for {c}.',
        '{n} from {c} is a top-tier racer who rarely makes a mistake.',
        'Smooth and relentless, {n} sets the pace for {c}.'],
    1: ['{n} is a quick and consistent driver from {c}.',
        'A strong racer from {c}, {n} is always in the hunt.',
        '{n} from {c} picks passing chances carefully.'],
    2: ['{n} is a solid midfield racer from {c}.',
        'Racing for {c}, {n} is still finding the last tenth.',
        '{n} from {c} is quick on an open track but struggles in traffic.'],
    3: ['{n} is one of the rising drivers from {c}.',
        'Still learning the ropes, {n} from {c} shows flashes of speed.',
        '{n} from {c} is young, eager and sometimes a little wild.'],
}
def bio(name, cc, rank, aggro, i):
    c = D.COUNTRIES[cc][3]
    if rank <= 0: t = '{n} joins the grid from {c}.'
    else: t = BIO[min(3, (rank - 1) // 4)][i % 3]
    s = t.format(n=name, c=c)
    if aggro >= 3: s += ' Not afraid to trade paint.'
    return s[0].upper() + s[1:]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--no-dynamic-ai', action='store_true', help='leave rubber-banding and driver personalities stock')
    ap.add_argument('--offer', type=int, default=130); ap.add_argument('--offer-id', default='4D530910F4FF7900')
    ap.add_argument('--str-pri', type=int, default=25500)
    ap.add_argument('--first-id', type=int, default=1000)
    ap.add_argument('--first-country', type=int, default=30)
    ap.add_argument('--pack-name', default='Real Racing Drivers')
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    pkg = hashlib.sha1(('FM4AIDRIVERS:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')

    # ---- base db + every installed merge (except ours): clash checks
    B = sqlite3.connect('file:%s?mode=ro' % os.path.join(a.fm4_media, 'db', 'gamedb.slt'), uri=True)
    dbs = [B]; merge_paths = []; patch_pri = set()
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
        for i in zipfile.ZipFile(z).infolist():
            fn = i.filename.replace('\\', '/').lower()
            if '/db/patch/' in fn:
                m = re.match(r'(\d+)_', os.path.basename(fn))
                if m: patch_pri.add(int(m.group(1)))
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
    if a.offer_id in used('ContentOffers', 'ID'): raise SystemExit('offer id %s already used' % a.offer_id)

    # ---- existing AI rows
    cols = [r[1] for r in B.execute('pragma table_info(AIPlayers)')]
    rows = [dict(zip(cols, r)) for r in B.execute('select * from AIPlayers order by Id')]
    region_of = {r[0]: r[1] for r in B.execute('select CountryID, RegionID from List_Country')}
    AI_HI = {int(r['Name'][2:]) >> 16 for r in rows} | {int(r['Bio'][2:]) >> 16 for r in rows}
    assert len(AI_HI) == 1, AI_HI; AI_HI = AI_HI.pop()
    LC_HI = {int(r[0][2:]) >> 16 for r in B.execute('select Country from List_Country')}
    assert len(LC_HI) == 1, LC_HI; LC_HI = LC_HI.pop()
    career = {}       # (region, rank) -> row
    type3 = []
    for r in rows:
        if r['DriverTypeID'] == 3: type3.append(r)
        else:
            k = (region_of[r['CountryId']], r['Rank'])
            assert k not in career, k; career[k] = r
    assert sorted(career) == [(g, k) for g in (1, 2, 3) for k in range(1, 17)], sorted(career)
    if used('AIPlayers', 'Id') & set(range(a.first_id, a.first_id + 2000)):
        raise SystemExit('AIPlayers ids from %d already used' % a.first_id)

    # ---- countries
    cid = a.first_country; lc_used = used('List_Country', 'CountryID'); new_countries = []
    country_id = {}
    for cc, (i, reg, name, _, flag) in D.COUNTRIES.items():
        if i is None:
            while cid in lc_used: cid += 1
            i = cid; cid += 1; new_countries.append((cc, i))
        country_id[cc] = i
    flagless = [(cc, i) for cc, (i, *_ ) in D.COUNTRIES.items() if i is not None and
                not (B.execute('select IconPath from List_Country where CountryID=?', (i,)).fetchone()[0] or '')]

    # ---- assign drivers
    t3_drivers = []; career_lists = {}
    for g, lst in D.REGIONS.items():
        career_lists[g] = [d for i, d in enumerate(lst) if i % TYPE3_COUNT_HINT != TYPE3_COUNT_HINT - 1]
        t3_drivers += [d for i, d in enumerate(lst) if i % TYPE3_COUNT_HINT == TYPE3_COUNT_HINT - 1]
    if len(t3_drivers) < len(type3): raise SystemExit('not enough drivers for the %d DriverTypeID 3 rows' % len(type3))
    spare = t3_drivers[len(type3):]; t3_drivers = t3_drivers[:len(type3)]
    for d in spare: career_lists[d['region']].append(d)
    keyc = {}
    for lst in D.REGIONS.values():
        for d in lst: keyc[(d['first'][0], d['last'])] = keyc.get((d['first'][0], d['last']), 0) + 1
    for lst in D.REGIONS.values():
        for d in lst:
            d['name'] = display(d, keyc[(d['first'][0], d['last'])] > 1)
            assert len(d['name']) <= 22, d['name']

    updates = []      # (row, driver, rank)
    inserts = []      # (new id, driver, template row)
    nid = a.first_id
    for g in (1, 2, 3):
        lst = career_lists[g]; n = len(lst); seen = set()
        for i, d in enumerate(lst):
            rank = 1 + i * 16 // n
            if rank not in seen:
                seen.add(rank); updates.append((career[(g, rank)], d, rank))
            else:
                inserts.append((nid, d, career[(g, rank)])); nid += 1
    for r, d in zip(type3, t3_drivers): updates.append((r, d, 0))

    # ---- string tables (highest installed copy per language, else the base game's)
    def base_tables():
        out = {}
        for lang in LANGS:
            zp = os.path.join(a.fm4_media, 'StringTables', '%s.zip' % lang)
            zf = zipfile.ZipFile(zp); names = {i.filename.lower(): i for i in zf.infolist()}
            with open(zp, 'rb') as f:
                for tbl in ('AIPlayers', 'List_Country'):
                    out[(lang, tbl)] = forza_zip.read_entry(f, names[('%s.str' % tbl).lower()])
        return out
    raw = base_tables()
    best = latest_tables(a.fm4_user, {'AIPlayers', 'List_Country'}, skip=pkg)
    for k, (pri, z, n) in best.items():
        if pri >= a.str_pri: raise SystemExit('%s ships %s at priority %d; use --str-pri above it' % (z, n, pri))
        raw[k] = read_member(z, n)   # newer copy from an installed pack (e.g. List_Country from offer 37)
    parsed = {k: SW.parse(v) for k, v in raw.items()}
    ai_free = free_hashes([t for (l, tb), t in parsed.items() if tb == 'AIPlayers'], 2 * len(inserts), start=0x0100)
    lc_free = free_hashes([t for (l, tb), t in parsed.items() if tb == 'List_Country'], len(new_countries), start=0x0100)
    ref = lambda hi, lo: '_&%d' % ((hi << 16) | lo)
    lo = lambda r: int(r[2:]) & 0xFFFF
    set_text = {'AIPlayers': {}, 'List_Country': {}}
    new_str = {'AIPlayers': [], 'List_Country': []}
    sql = []

    for cc, i in flagless:
        sql.append("UPDATE List_Country SET IconPath=%s WHERE CountryID=%d;" %
                   (q('GAME:\\Media\\UI\\Textures\\UI4\\Logos\\Country_Flags\\L_FLA_%s_flag.tga' % D.COUNTRIES[cc][4]), i))
    for k, (cc, i) in enumerate(new_countries):
        _, reg, name, _, flag = D.COUNTRIES[cc]
        h = lc_free[k]
        new_str['List_Country'].append(('IDS_Country_%d' % i, h, name))
        sql.append("INSERT INTO List_Country (CountryID,Country,RegionID,IconPath) VALUES (%d,%s,%d,%s);" %
                   (i, q(ref(LC_HI, h)), reg, q('GAME:\\Media\\UI\\Textures\\UI4\\Logos\\Country_Flags\\L_FLA_%s_flag.tga' % flag)))

    report = []
    for k, (r, d, rank) in enumerate(updates):
        set_text['AIPlayers'][lo(r['Name'])] = d['name']
        set_text['AIPlayers'][lo(r['Bio'])] = bio(d['name'], d['cc'], rank, r['HiredAggro'] or 0, k)
        sql.append("UPDATE AIPlayers SET FirstName=%s,LastName=%s,CountryId=%d WHERE Id=%d;" %
                   (q(d['first']), q(d['last']), country_id[d['cc']], r['Id']))
        report.append((r['Id'], r['DriverTypeID'], d['region'] if rank else 0, rank, d['name'], d['cc'], 'renamed from %s %s' % (r['FirstName'], r['LastName'])))
    rng = random.Random(4)
    for k, (i, d, t) in enumerate(inserts):
        hn, hb = ai_free[2 * k], ai_free[2 * k + 1]
        new_str['AIPlayers'] += [('IDS_Name_%d' % i, hn, d['name']),
                                 ('IDS_Bio_%d' % i, hb, bio(d['name'], d['cc'], t['Rank'], t['HiredAggro'] or 0, k))]
        v = dict(t); v.update(Id=i, Name=ref(AI_HI, hn), Bio=ref(AI_HI, hb), FirstName=d['first'], LastName=d['last'],
                              CountryId=country_id[d['cc']])
        sql.append('INSERT INTO AIPlayers (%s) VALUES (%s);' % (','.join(cols), ','.join(
            q(v[c]) if isinstance(v[c], str) else ('NULL' if v[c] is None else str(v[c])) for c in cols)))
        sql.append('INSERT INTO AIPlayerColors (PlayerId,PrimaryColorValid,PrimaryColor,IsMetallic,SpecialColorId) VALUES (%d,1,%d,%d,0);' %
                   (i, rng.choice(PALETTE), rng.random() < 0.5))
        report.append((i, t['DriverTypeID'], d['region'], t['Rank'], d['name'], d['cc'], 'new'))

    # ---- dynamic AI (claude/dynamic-ai-2026-10-13.md): no rubber-banding, wider driver personalities
    if not a.no_dynamic_ai:
        sql.extend(DYNAMIC_AI_SQL)

    # ---- check the SQL on a copy of the base db (+ every installed merge is not needed: only base rows touched)
    chk = os.path.join(a.work, 'check.slt'); shutil.copyfile(os.path.join(a.fm4_media, 'db', 'gamedb.slt'), chk)
    C = sqlite3.connect(chk)
    for s in sql: C.execute(s)
    C.execute('ALTER TABLE AIPlayers ADD COLUMN isUsed INT DEFAULT 0 NOT NULL')
    per = C.execute('SELECT RegionID, Rank, COUNT(*) FROM AIPlayers A JOIN List_Country C ON A.CountryID=C.CountryID '
                    'WHERE Rank>0 GROUP BY RegionID, Rank').fetchall()
    assert len(per) == 48 and all(n >= 2 for _, _, n in per), per
    assert C.execute('SELECT COUNT(*) FROM AIPlayers WHERE DriverTypeID=3').fetchone()[0] == len(type3)
    assert C.execute('SELECT COUNT(*) FROM AIPlayers A LEFT JOIN List_Country C ON A.CountryID=C.CountryID WHERE C.CountryID IS NULL').fetchone()[0] == 0
    if not a.no_dynamic_ai:
        assert C.execute('SELECT MIN(StartRubberbandingDistance), MAX(TorqueCutFactor) FROM AIRubberbands').fetchone() == (4000.0, 0.0)
        assert C.execute('SELECT MAX(RelativeFrequency) FROM AIRubberbandModeChoices WHERE AIRubberbandModes_id<>0').fetchone()[0] == 0.0
        assert C.execute('SELECT MIN(SkillModifier) FROM AIPlayers WHERE DriverTypeID=1').fetchone()[0] >= 0
        print('dynamic AI  skill mods', C.execute('SELECT MIN(SkillModifier),MAX(SkillModifier),AVG(SkillModifier) FROM AIPlayers WHERE DriverTypeID=1').fetchone(),
              ' aggro mods', C.execute('SELECT MIN(AggroModifier),MAX(AggroModifier) FROM AIPlayers WHERE DriverTypeID=1').fetchone())
    C.close()

    # ---- execute.sql chunks
    chunks = [[]]
    for s in sql:
        if chunks[-1] and sum(len(x) + 2 for x in chunks[-1]) + len(s) > SQL_CHUNK: chunks.append([])
        chunks[-1].append(s)
    pris = [a.offer * 1000 + 1 + k for k in range(len(chunks))]
    clash = set(pris) & patch_pri
    if clash: raise SystemExit('db patch priorities already used: %s' % sorted(clash))
    if len(chunks) > 998: raise SystemExit('too many SQL chunks')

    # ---- merge.slt with the ContentOffers row (template = an installed FM4 DLC merge)
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
    off_hi = int(srcrow['DisplayName'][2:]) >> 16
    srcrow.update(ID=a.offer_id, OfferId=int(a.offer_id, 16), PubOfferingID=a.offer, DisplayName=ref(off_hi, 0xF4F0),
                  Description=ref(off_hi, 0xF4F1), LicenseLevel=1, LicenseMask=1, IsDLC=1, IsInstalled=0,
                  IsPurchased=0, IsVIP=0, Hidden=0, IsALaCarte=0, SeasonPassOffer=0)
    O.execute('insert into ContentOffers (%s) values (%s)' % (','.join('"%s"' % c for c in oc), ','.join('?' * len(oc))), [srcrow.get(c) for c in oc])
    O.commit(); O.execute('vacuum'); O.close()

    root = os.path.join(a.out, pkg); pm = os.path.join(root, 'Media'); pdz = os.path.join(pm, 'DLCZips')
    if os.path.isdir(root): shutil.rmtree(root)
    os.makedirs(pdz)
    entries = [stored('media/db/patch/%d000_merge.slt' % a.offer, open(out_slt, 'rb').read())]
    for p, ch in zip(pris, chunks):
        entries.append(stored('media/db/patch/%d_execute.sql' % p, '\r\n'.join(ch).encode('ascii')))
    open(os.path.join(pm, '%d.puboffer' % a.offer), 'wb').write(b' \r\n')
    write_zip(os.path.join(pdz, '%d_pri_65.zip' % a.offer), entries)

    # ---- string tables
    st = []
    for (lang, tbl), t in sorted(parsed.items()):
        pool = bytearray(t['pool1']); idx = []
        for h, off in t['idx1']:
            if h in set_text[tbl]:
                off = len(pool) // 2; pool += set_text[tbl][h].encode('utf-16-be') + b'\0\0'
            idx.append((h, off))
        missing = set(set_text[tbl]) - {h for h, _ in t['idx1']}
        if missing: raise SystemExit('%s %s: no string for hashes %s' % (lang, tbl, sorted(missing)))
        t['pool1'] = bytes(pool); t['idx1'] = idx
        data = SW.rebuild(t, new_str[tbl])
        p = os.path.join(a.work, 'chk.str'); open(p, 'wb').write(data)
        got = {h: x for _, h, x in forza_str.read(p)}
        for h, x in set_text[tbl].items(): assert got[h] == x, (lang, tbl, h)
        for _, h, x in new_str[tbl]: assert got[h] == x, (lang, tbl, h)
        st.append(stored('%s/%s.str' % (lang, tbl), data))
    st_name = 'StringTables_pri_%d.zip' % a.str_pri
    write_zip(os.path.join(pdz, st_name), st)
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='\r\n').write(
        '<?xml version="1.0" ?> \n<zipmount> \n   <zip Name="%s" Mount="game:\\Media\\StringTables\\"  /> \n</zipmount> \n' % st_name)
    open(os.path.join(pdz, 'stringtables.xml'), 'w', newline='\r\n').write(
        '<?xml version="1.0" ?> \n<stringtables> \n   <table Name="AIPlayers" /> \n   <table Name="List_Country" /> \n</stringtables> \n')

    # ---- XContent header (clone the Porsche Expansion header if present)
    hdr_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, 'Headers', '00000002')
    hdrs = sorted(glob.glob(os.path.join(hdr_dir, '*.header')))
    tmplh = next((h for h in hdrs if os.path.basename(h).startswith('E3470A83')), None) or next(h for h in hdrs if len(os.path.basename(h)) > 30)
    h = bytearray(open(tmplh, 'rb').read())
    h[0x08:0x108] = a.pack_name[:127].encode('utf-16-be').ljust(256, b'\0')
    h[0x108:0x132] = pkg.encode('ascii'); h[0x132:0x134] = b'\0\0'; h[0x134:0x13C] = b'\0' * 8
    hdr_out = os.path.join(a.out, pkg + '.header'); open(hdr_out, 'wb').write(h)

    with open(os.path.join(a.out, 'ai_drivers_report.tsv'), 'w') as f:
        f.write('Id\tDriverType\tRegion\tRank\tName\tCountry\tNote\n')
        for r in sorted(report): f.write('\t'.join(str(x) for x in r) + '\n')

    if a.install:
        shutil.copytree(root, os.path.join(pkgs_dir, pkg), dirs_exist_ok=True)
        shutil.copyfile(hdr_out, os.path.join(hdr_dir, pkg + '.header'))
    print('package   ', pkg, '(installed)' if a.install else '')
    print('offer     ', a.offer, a.offer_id, ' sql chunks', len(chunks), pris[0], '-', pris[-1], ' strings', st_name, len(st), 'files')
    print('countries  new', [(cc, i) for cc, i in new_countries], ' flags added', [cc for cc, _ in flagless])
    print('drivers    renamed', len(updates), ' new', len(inserts), ' total career', sum(n for *_, n in per))
    print('per region/rank', {(g, k): n for g, k, n in per})

if __name__ == '__main__':
    main()
