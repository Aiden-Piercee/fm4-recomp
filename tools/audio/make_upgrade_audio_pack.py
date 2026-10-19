"""Build an upgrade-audio pack (per-level intake/exhaust sounds).

Needs the FM4-recomp hooks UpgradeAudioLevels / UpgradeAudioSlotGuard
(fm4/src/hooks.cpp); without them the game always uses the L3 slot.
See claude/upgrade-audio-framework-2026-10-12.md.

The pack is two Forza zips mounted over the base folders by zipmount.xml
(same mechanism as Turn 10's CarModelTuning_pri_25034.zip title-update fix):
  EngineTuning_pri_<P>.zip   -> game:\\Media\\Audio\\Cars\\Engines\\EngineTuning\\
  HarmonicTuning_pri_<P>.zip -> game:\\Media\\Audio\\Cars\\Engines\\HarmonicTuning\\

usage: make_upgrade_audio_pack.py --game <extracted game root> --spec spec.json
                                  --out <dir> [--install <DLC package Media/DLCZips dir>]
spec.json:
  {"priority": 26500,
   "engines": {"VW_Fox_11": {"intake":  ["a_HT.xml","b_HT.xml","c_HT.xml","d_HT.xml"],
                             "exhaust": [...4 names...]}},
   "variants": {"New_HT.xml": {"base": "Existing_HT.xml",
                               "replace": [["regex", "replacement"], ...]}}}
"""
import argparse, json, os, re, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'dlc'))
import forza_zip                     # noqa: E402  (needs libfzlzx.so, see build_fzlzx.sh)
from port_dlc_car import write_zip, stored   # noqa: E402
import zipfile                       # noqa: E402

ET_MOUNT = 'game:\\Media\\Audio\\Cars\\Engines\\EngineTuning\\'
HT_MOUNT = 'game:\\Media\\Audio\\Cars\\Engines\\HarmonicTuning\\'

def zip_member(zp, name):
    zf = zipfile.ZipFile(zp)
    with open(zp, 'rb') as f:
        for i in zf.infolist():
            if i.filename.lower() == name.lower():
                return forza_zip.read_entry(f, i)
    raise SystemExit('%s not in %s' % (name, zp))

def zip_names(zp):
    return {i.filename.lower() for i in zipfile.ZipFile(zp).infolist()}

def set_upgrade(et, section, names):
    pat = re.compile(rb'(<%s>\s*<Upgrade)[^/]*(/>)' % section.encode())
    attrs = b''.join(b' L%d="%s"' % (i, n.encode()) for i, n in enumerate(names))
    new, n = pat.subn(lambda m: m.group(1) + attrs + m.group(2), et, count=1)
    if n != 1: raise SystemExit('no <%s><Upgrade> in ET' % section)
    return new

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True); ap.add_argument('--spec', required=True)
    ap.add_argument('--out', required=True); ap.add_argument('--install')
    a = ap.parse_args()
    spec = json.load(open(a.spec)); pri = int(spec.get('priority', 26500))
    eng = os.path.join(a.game, 'Media', 'audio', 'cars', 'Engines')
    et_zip = os.path.join(eng, 'enginetuning.zip'); ht_zip = os.path.join(eng, 'harmonictuning.zip')
    banks = {f.lower() for f in os.listdir(os.path.join(eng, 'soundbanks', 'lod1'))}
    base_hts = zip_names(ht_zip)

    ht_entries, new_hts = [], set()
    for name, v in spec.get('variants', {}).items():
        data = zip_member(ht_zip, v['base'])
        for rx, rep in v.get('replace', []):
            data, n = re.subn(rx.encode(), rep.encode(), data)
            if n == 0: raise SystemExit('%s: pattern not found: %s' % (name, rx))
        ht_entries.append(stored(name, data)); new_hts.add(name.lower())
        print('variant HT  ', name, '(from %s)' % v['base'])

    et_entries = []
    for engine, slots in spec['engines'].items():
        et = zip_member(et_zip, engine + '_ET.xml')
        for section, key in (('EngineIntake', 'intake'), ('Exhaust', 'exhaust')):
            names = slots[key]
            if len(names) != 4: raise SystemExit('%s %s: need 4 slots' % (engine, key))
            for n in names:
                if n.lower() == 'na.xml': continue
                if n.lower() not in base_hts and n.lower() not in new_hts:
                    raise SystemExit('%s: HT %s does not exist' % (engine, n))
                src = zip_member(ht_zip, n) if n.lower() in base_hts else \
                    next(e[4] for e in ht_entries if e[0].lower() == n.lower())
                bank = re.search(rb'<Soundbank name="([^"]+)"', src).group(1).decode()
                if bank.lower() not in banks: raise SystemExit('%s: bank %s missing in LOD1' % (n, bank))
            et = set_upgrade(et, section, names)
            print('%-12s %s: %s' % (engine, key, ' | '.join(names)))
        et_entries.append(stored(engine + '_ET.xml', et))

    os.makedirs(a.out, exist_ok=True)
    et_name = 'EngineTuning_pri_%d.zip' % pri; ht_name = 'HarmonicTuning_pri_%d.zip' % pri
    write_zip(os.path.join(a.out, et_name), et_entries)
    if ht_entries: write_zip(os.path.join(a.out, ht_name), ht_entries)
    lines = ['   <zip Name="%s" Mount="%s" AltRootPath="%s" ShouldCache="0" /> ' % (et_name, ET_MOUNT, ET_MOUNT)]
    if ht_entries:
        lines.append('   <zip Name="%s" Mount="%s" AltRootPath="%s" ShouldCache="0" /> ' % (ht_name, HT_MOUNT, HT_MOUNT))
    open(os.path.join(a.out, 'zipmount_lines.txt'), 'w').write('\n'.join(lines) + '\n')
    print('wrote', et_name, ht_name if ht_entries else '')

    if a.install:
        for n in ([et_name] + ([ht_name] if ht_entries else [])):
            shutil.copyfile(os.path.join(a.out, n), os.path.join(a.install, n))
        zm = os.path.join(a.install, 'zipmount.xml')
        text = open(zm, encoding='utf-8', newline='').read()
        bak = os.path.join(a.out, 'zipmount.xml.pre-upgrade-audio')
        if not os.path.exists(bak):
            shutil.copyfile(zm, bak)
        for ln in lines:
            name = re.search(r'Name="([^"]+)"', ln).group(1)
            if name in text: continue
            text = text.replace('</zipmount>', ln + '\r\n</zipmount>')
        open(zm, 'w', encoding='utf-8', newline='').write(text)
        print('installed into', a.install)

if __name__ == '__main__':
    main()
