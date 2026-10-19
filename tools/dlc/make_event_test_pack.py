#!/usr/bin/env python3
"""make_event_test_pack.py - build (and optionally install) an FM4 DLC-style pack that adds
placeholder career events to the Event List, to test how the grid handles more than 310 tiles.

  python3 make_event_test_pack.py --fm4-media <FM4 Media dir> --fm4-user <.local/user dir>
        --out <staging dir> --work <local scratch dir> [--install]
        [--count 10] [--banner 7] [--offer 120] [--event-id 900] [--cet-id 50] [--str-pri 26000]
        [--sources 1,2,3,...] [--keep-races]

What the pack contains (FM4 DLC layout, same as tools/dlc/port_dlc_car.py output):
  * <offer>_pri_65.zip  : media/db/patch/<offer>000_merge.slt  + event posters
                          (media/UI/Textures/UI4/Logos/Posters[_SM]/EventBkgd<id>.xds, copied raw
                          from the source event's poster in Media/UI/Textures.zip)
  * merge.slt           : 1 CareerEventTypes row (new type, BannerId = --banner, so the events sort
                          after World Championships = a new column on the right of the grid),
                          N Events cloned from --sources (Level 0, GroupMask 0, EventOrder 1..N),
                          their Races (first race only unless --keep-races), EventParticipants,
                          EventRestrictions, and one ContentOffers row for the pack.
  * StringTables_pri_<str-pri>.zip : highest installed Events / CareerEventTypes / ContentOffers
                          .str for every language + the new strings.
  * zipmount.xml, stringtables.xml, <offer>.puboffer, cloned XContent .header.
Rerunning with the same --offer rebuilds the same package folder in place.
"""
import argparse, glob, hashlib, os, re, shutil, sqlite3, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import forza_zip, forza_str, forza_str_write as SW
from port_dlc_car import write_zip, stored, latest_tables, read_member, free_hashes, TITLE

TABLES = ['ContentOffers', 'CareerEventTypes', 'Events', 'Races', 'EventParticipants', 'EventRestrictions']
DEFAULT_SOURCES = [1, 2, 3, 4, 6, 7, 8, 50, 51, 60]   # Level-0 road-race events (CET 1 / 2)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fm4-media', required=True); ap.add_argument('--fm4-user', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--work', required=True)
    ap.add_argument('--install', action='store_true')
    ap.add_argument('--count', type=int, default=10); ap.add_argument('--banner', type=int, default=7)
    ap.add_argument('--offer', type=int, default=120); ap.add_argument('--offer-id', default='4D530910F4FF7800')
    ap.add_argument('--event-id', type=int, default=900); ap.add_argument('--cet-id', type=int, default=50)
    ap.add_argument('--str-pri', type=int, default=26000)
    ap.add_argument('--sources', help='comma list of base Events.Id to clone (default: %s)' % DEFAULT_SOURCES)
    ap.add_argument('--keep-races', action='store_true', help='keep every race of the source event (default: first race only)')
    ap.add_argument('--pack-name', default='Event Grid Test Pack')
    ap.add_argument('--type-name', default='Test Events', help='name of the new event group (CareerEventTypes)')
    ap.add_argument('--event-name', default='Test Event {n:02d}', help='event name; {n} = number (starts at --name-start)')
    ap.add_argument('--name-start', type=int, default=1)
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    src_ids = [int(x) for x in a.sources.split(',')] if a.sources else DEFAULT_SOURCES
    src_ids = (src_ids * ((a.count + len(src_ids) - 1) // len(src_ids)))[:a.count]
    pkg = hashlib.sha1(('FM4EVENTTEST:%d' % a.offer).encode()).hexdigest().upper()[:40] + '4D'
    pkgs_dir = os.path.join(a.fm4_user, '0000000000000000', TITLE, '00000002')

    # ---- every installed merge (except our own) + base db: used ids
    base_db = os.path.join(a.fm4_media, 'db', 'gamedb.slt')
    dbs = [sqlite3.connect(base_db)]; merge_paths = []
    for z in sorted(glob.glob(os.path.join(pkgs_dir, '*', 'Media', 'DLCZips', '*_pri_65.zip'))):
        if pkg in z: continue
        for i in zipfile.ZipFile(z).infolist():
            if i.filename.lower().endswith('_merge.slt'):
                p = os.path.join(a.work, 'm_%s_%s' % (z.split(os.sep)[-4][:8], os.path.basename(i.filename)))
                with open(z, 'rb') as f: open(p, 'wb').write(forza_zip.read_entry(f, i))
                if os.path.basename(i.filename).startswith('%d000_' % a.offer):
                    raise SystemExit('offer %d already used by %s' % (a.offer, z))
                dbs.append(sqlite3.connect(p)); merge_paths.append((dbs[-1], p))
    def used(table, col):
        s = set()
        for d in dbs:
            try: s |= {r[0] for r in d.execute('select "%s" from "%s"' % (col, table))}
            except sqlite3.Error: pass
        return s
    new_events = list(range(a.event_id, a.event_id + a.count))
    clash = set(new_events) & used('Events', 'Id')
    if clash: raise SystemExit('Events.Id already used: %s' % sorted(clash))
    if a.cet_id in used('CareerEventTypes', 'id'): raise SystemExit('CareerEventTypes.id %d already used' % a.cet_id)
    if a.offer_id in used('ContentOffers', 'ID'): raise SystemExit('offer id %s already used' % a.offer_id)
    next_race = max(used('Races', 'Id')) + 1
    next_part = max(used('EventParticipants', 'ID')) + 1
    next_rest = max(used('EventRestrictions', 'ID')) + 1
    B = dbs[0]

    # ---- string tables (highest installed copy per language)
    best = latest_tables(a.fm4_user, {'Events', 'CareerEventTypes', 'ContentOffers'}, skip=pkg)
    parsed = {k: SW.parse(read_member(z, n)) for k, (_, z, n) in best.items()}
    en_events = {}
    p = os.path.join(a.work, 'en_events.str'); open(p, 'wb').write(read_member(best[('EN', 'Events')][1], best[('EN', 'Events')][2]))
    for _, h, txt in forza_str.read(p): en_events[h] = txt
    def free(tbl, n): return free_hashes([t for (l, tb), t in parsed.items() if tb == tbl], n)
    ev_h = free('Events', 2 * a.count); cet_h = free('CareerEventTypes', 2); off_h = free('ContentOffers', 2)
    def hi(tbl, sample): return int(sample[2:]) >> 16
    EV_HI = hi('Events', B.execute('select Name from Events limit 1').fetchone()[0])
    CET_HI = hi('CareerEventTypes', B.execute('select Name from CareerEventTypes limit 1').fetchone()[0])
    OFF_HI = hi('ContentOffers', next(r[0] for d in dbs[1:] for r in d.execute('select DisplayName from ContentOffers limit 1')))
    ref = lambda h, lo: '_&%d' % ((h << 16) | lo)

    # ---- merge.slt: template = an installed FM4 DLC merge that has all the tables (keeps FM4's
    #      1024-byte pages and its CREATE TABLE text; a fresh python sqlite file is not read by the game)
    tmpl_path = None
    for d, path in merge_paths:
        have = {r[0] for r in d.execute("select name from sqlite_master where type='table'")}
        if set(TABLES) <= have: tmpl_path = path; break
    if not tmpl_path: raise SystemExit('no installed FM4 merge has all of %s (install the Porsche Expansion)' % TABLES)
    out_slt = os.path.join(a.work, '%d000_merge.slt' % a.offer)
    shutil.copyfile(tmpl_path, out_slt)
    O = sqlite3.connect(out_slt)
    for (t,) in O.execute("select name from sqlite_master where type='table'").fetchall():
        O.execute('delete from "%s"' % t)
    def cols(t): return [r[1] for r in O.execute('pragma table_info("%s")' % t)]
    def bcols(t): return [r[1] for r in B.execute('pragma table_info("%s")' % t)]
    def ins(t, d):
        c = cols(t); O.execute('insert into "%s" (%s) values (%s)' % (t, ','.join('"%s"' % x for x in c), ','.join('?' * len(c))), [d.get(x) for x in c])
    offer_int = int(a.offer_id, 16)
    tmpl = dict(zip(bcols('ContentOffers'), next(r for d in dbs[1:] for r in d.execute('select * from ContentOffers where ImagePath like ? limit 1', ('%Porsche%',)))
                    if any(d.execute('select 1 from ContentOffers where ImagePath like ?', ('%Porsche%',)).fetchone() for d in dbs[1:])
                    else next(r for d in dbs[1:] for r in d.execute('select * from ContentOffers limit 1'))))
    tmpl.update(ID=a.offer_id, OfferId=offer_int, PubOfferingID=a.offer, DisplayName=ref(OFF_HI, off_h[0]),
                Description=ref(OFF_HI, off_h[1]), LicenseLevel=1, LicenseMask=1, IsDLC=1, IsInstalled=0,
                IsPurchased=0, IsVIP=0, Hidden=0, IsALaCarte=0, SeasonPassOffer=0)
    ins('ContentOffers', tmpl)
    cet = dict(zip(bcols('CareerEventTypes'), B.execute('select * from CareerEventTypes where id=2').fetchone()))
    cet.update(id=a.cet_id, Name=ref(CET_HI, cet_h[0]), Description=ref(CET_HI, cet_h[1]), BannerId=a.banner)
    ins('CareerEventTypes', cet)
    new_str = {'Events': [], 'CareerEventTypes': [('IDS_Name_%d' % a.cet_id, cet_h[0], a.type_name),
                                                  ('IDS_Description_%d' % a.cet_id, cet_h[1], 'Placeholder events (%s).' % a.pack_name)],
               'ContentOffers': [('IDS_DisplayName_%s' % a.offer_id, off_h[0], a.pack_name),
                                 ('IDS_Description_%s' % a.offer_id, off_h[1], 'Adds %d placeholder events to the Event List (grid test).' % a.count)]}
    posters = []; report = []
    for k, (src, nid) in enumerate(zip(src_ids, new_events)):
        e = dict(zip(bcols('Events'), B.execute('select * from Events where Id=?', (src,)).fetchone()))
        src_name = en_events.get(int(e['Name'][2:]) & 0xFFFF, '?')
        races = B.execute('select * from Races where EventId=? order by RaceNumber', (src,)).fetchall()
        if not a.keep_races: races = races[:1]
        e.update(Id=nid, Name=ref(EV_HI, ev_h[2 * k]), Description=ref(EV_HI, ev_h[2 * k + 1]), Level=0,
                 CareerTypeId=a.cet_id, NumberOfRaces=len(races), IsDLC=0, EventOrder=k + 1, GroupMask=0)
        ins('Events', e)
        for r in races:
            d = dict(zip(bcols('Races'), r)); d.update(Id=next_race, EventId=nid); next_race += 1; ins('Races', d)
        for r in B.execute('select * from EventParticipants where EventID=?', (src,)).fetchall():
            d = dict(zip(bcols('EventParticipants'), r)); d.update(ID=next_part, EventID=nid); next_part += 1; ins('EventParticipants', d)
        for r in B.execute('select * from EventRestrictions where EventID=?', (src,)).fetchall():
            d = dict(zip(bcols('EventRestrictions'), r)); d.update(ID=next_rest, EventID=nid); next_rest += 1; ins('EventRestrictions', d)
        new_str['Events'] += [('IDS_Name_%d' % nid, ev_h[2 * k], a.event_name.format(n=a.name_start + k)),
                              ('IDS_Description_%d' % nid, ev_h[2 * k + 1], 'Placeholder event (copy of "%s", %d race%s).' % (src_name, len(races), '' if len(races) == 1 else 's'))]
        posters.append((src, nid)); report.append((nid, src, src_name, [r[3] for r in races]))
    O.commit(); O.execute('vacuum'); O.close()

    # ---- main zip: merge + posters (raw copies of the source posters)
    tex = os.path.join(a.fm4_media, 'UI', 'Textures.zip'); tz = zipfile.ZipFile(tex)
    names = {i.filename.lower(): i for i in tz.infolist()}
    entries = [stored('media/db/patch/%d000_merge.slt' % a.offer, open(out_slt, 'rb').read())]
    with open(tex, 'rb') as f:
        for sub in ('Posters', 'Posters_SM'):
            for src, nid in posters:
                i = names.get(('UI4/Logos/%s/EventBkgd%d.xds' % (sub, src)).lower())
                if i is None: print('warning: no %s poster for event %d' % (sub, src)); continue
                entries.append(('media/UI/Textures/UI4/Logos/%s/EventBkgd%d.xds' % (sub, nid), i.compress_type,
                                i.CRC, i.file_size, forza_zip.raw_entry(f, i)))
    root = os.path.join(a.out, pkg); pm = os.path.join(root, 'Media'); pdz = os.path.join(pm, 'DLCZips')
    os.makedirs(pdz, exist_ok=True)
    open(os.path.join(pm, '%d.puboffer' % a.offer), 'wb').write(b' \r\n')
    write_zip(os.path.join(pdz, '%d_pri_65.zip' % a.offer), entries)

    # ---- string tables
    st = []
    for (lang, tbl) in sorted(parsed):
        st.append(stored('%s/%s.str' % (lang, tbl), SW.rebuild(parsed[(lang, tbl)], new_str[tbl])))
    st_name = 'StringTables_pri_%d.zip' % a.str_pri
    write_zip(os.path.join(pdz, st_name), st)
    open(os.path.join(pdz, 'zipmount.xml'), 'w', newline='\r\n').write(
        '<?xml version="1.0" ?> \n<zipmount> \n   <zip Name="%s" Mount="game:\\Media\\StringTables\\"  /> \n</zipmount> \n' % st_name)
    open(os.path.join(pdz, 'stringtables.xml'), 'w', newline='\r\n').write(
        '<?xml version="1.0" ?> \n<stringtables> \n   <table Name="CareerEventTypes" /> \n   <table Name="ContentOffers" /> \n   <table Name="Events" /> \n</stringtables> \n')

    # ---- XContent header (clone the Porsche Expansion header if present)
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
    print('package   ', pkg, '(installed)' if a.install else '')
    print('offer     ', a.offer, a.offer_id, ' merge %d000_merge.slt' % a.offer, ' strings', st_name, len(st), 'files')
    print('event type', a.cet_id, 'BannerId', a.banner)
    for nid, src, nm, tr in report: print('event     ', nid, '<- %d "%s"' % (src, nm), 'tracks', tr)
    print('posters   ', len(entries) - 1, ' races/participants/restrictions next ids', next_race, next_part, next_rest)

if __name__ == '__main__':
    main()
