#!/usr/bin/env python3
"""fill_morph.py - give a car with no damage-morph data a baseline copied from a similar native car.

FM4 dents a car by moving each vertex along its per-vertex Morph value (exterior submodels, main file:
Morph, lod0/cockpit files: Morph_LOD0; 4 bytes per vertex, see claude/damage-model-research doc).
Ported cars often carry none, so they never dent. This copies the donor's values by nearest vertex:
  1. same-name submodel in the same file kind (body, bumperFa, hooda, ...): nearest vertex after
     normalising both parts to their own bounding box;
  2. otherwise, if the part's family (leading letters: bumper, hood, wing, ...) has morph data on the donor:
     nearest donor morph vertex over the whole car, after normalising both cars to their exterior bbox.
Parts with no donor equivalent stay as they are.

  python3 fill_morph.py target.carbin donor.carbin out.carbin    (pairs the same file kind: main/lod0/cockpit)
As a module: fill(target_bytes, donor_bytes) -> (out_bytes, report) or (None, report) if nothing to do.
"""
import re, struct, sys
import numpy as np
import carbin_fm2 as C


def positions(p):
    vb = p['vb'] if p['vb']['count'] else p['vb_lod0']
    if not vb['count'] or vb['stride'] < 8: return None
    a = np.frombuffer(vb['data'], '>u2').reshape(-1, vb['stride'] // 2)
    f = np.array(p['floats']); Cn, H = f[0:3], f[6:9]
    d = a[:, 0:3].astype(np.int16).astype(float) / 32767
    return Cn + d * (a[:, 3].astype(float) / 65535 * np.linalg.norm(2 * H))[:, None]


def has_morph(p):
    return bool(p['morph_byte']) and (p['morph']['count'] > 0 or p['morph_lod0']['count'] > 0)


def family(name):
    """bumperFa -> bumper, winga/wingb/wingrace -> wing, hooda -> hood, exhaustRa -> exhaust, glassLF -> glass"""
    m = re.match(r'[a-z]+', name)
    f = m.group(0) if m else name.lower()
    return re.sub(r'(race|[a-d])$', '', f) if len(f) > 4 else f


def nearest(src, dst):
    """index into src of the nearest point for every row of dst (brute force, chunked; no scipy)."""
    src = np.ascontiguousarray(src, np.float32); dst = np.ascontiguousarray(dst, np.float32)
    out = np.empty(len(dst), np.int64)
    s2 = (src ** 2).sum(1)
    chunk = max(8, 8_000_000 // max(len(src), 1))      # keeps the distance block around 32 MB
    for i in range(0, len(dst), chunk):
        q = dst[i:i + chunk]
        d = s2[None, :] - 2 * q @ src.T
        out[i:i + chunk] = d.argmin(1)
    return out


def _norm(P, lo, hi): return (P - lo) / np.maximum(hi - lo, 1e-3)


def fill(target, donor):
    T = C.parse_fm4(target); D = C.parse_fm4(donor)
    rep = dict(filled=[], skipped=[], already=0)
    dparts = {}
    allP = []; allM = []; fams = set(); keys = []
    for p in D['parts']:
        if not has_morph(p): continue
        P = positions(p)
        if P is None: continue
        key = 'morph' if p['morph']['count'] else 'morph_lod0'
        M = np.frombuffer(p[key]['data'], '>u4')
        if len(M) != len(P): continue
        dparts[p['name'].lower()] = (P, M); allP.append(P); allM.append(M); fams.add(family(p['name'])); keys.append(key)
    if not dparts:
        rep['error'] = 'donor has no morph data'; return None, rep
    key = max(set(keys), key=keys.count)
    allP = np.concatenate(allP); allM = np.concatenate(allM)
    tP = [positions(p) for p in T['parts']]
    ext = [P for p, P in zip(T['parts'], tP) if P is not None and family(p['name']) in fams]
    tlo, thi = (np.concatenate(ext).min(0), np.concatenate(ext).max(0)) if ext else (None, None)
    dlo, dhi = allP.min(0), allP.max(0)
    for p, P in zip(T['parts'], tP):
        if P is None or not len(P): continue
        if has_morph(p): rep['already'] += 1; continue
        nm = p['name'].lower()
        if nm in dparts:
            DP, DM = dparts[nm]
            M = DM[nearest(_norm(DP, DP.min(0), DP.max(0)), _norm(P, P.min(0), P.max(0)))]; how = 'same part'
        elif family(p['name']) in fams and tlo is not None:
            M = allM[nearest(_norm(allP, dlo, dhi), _norm(P, tlo, thi))]; how = 'whole car'
        else:
            rep['skipped'].append(p['name']); continue
        md = dict(count=len(P), stride=4, data=M.astype('>u4').tobytes())
        empty = dict(count=0, stride=0, data=b'')
        p['morph_byte'] = 1
        p['morph'], p['morph_lod0'] = (md, empty) if key == 'morph' else (empty, md)
        rep['filled'].append((p['name'], how, len(P)))
    if not rep['filled']: return None, rep
    out = C.write_fm4(T)
    chk = C.parse_fm4(out)
    for p in chk['parts']:
        if p['morph_byte']:
            n = (p['vb'] if p['vb']['count'] else p['vb_lod0'])['count']
            assert max(p['morph']['count'], p['morph_lod0']['count']) in (0, n), p['name']
    return out, rep


def needs_fill(carbin_bytes):
    """True if the file has exterior geometry but no submodel carries morph data."""
    m = C.parse_fm4(carbin_bytes)
    return not any(has_morph(p) for p in m['parts']) and any(p['vb']['count'] or p['vb_lod0']['count'] for p in m['parts'])


if __name__ == '__main__':
    t, d, o = sys.argv[1:4]
    out, rep = fill(open(t, 'rb').read(), open(d, 'rb').read())
    print(rep)
    if out: open(o, 'wb').write(out)


# ------------------------------------------------------------------ car-level helpers (used by make_damage_pack.py)
CAR_FILES = ('', '_lod0', '_cockpit')


def bbox_dims(maxdata_xml):
    m = re.search(r'<BoundingBox ([^>]*)/?>', maxdata_xml)
    if not m: return None
    v = dict(re.findall(r'(\w+)="([-0-9.]+)"', m.group(1)))
    try: return np.array([float(v['MaxX']) - float(v['MinX']), float(v['MaxY']) - float(v['MinY']), float(v['MaxZ']) - float(v['MinZ'])])
    except (KeyError, ValueError): return None


def pick_donor(target, tdims, candidates, overrides=None):
    """candidates: {car: dims}. Same model word in the name wins, then same make, then closest size."""
    if overrides and target in overrides: return [overrides[target]]
    words = set(w.lower() for w in re.findall(r'[A-Za-z][a-z]+|[A-Z0-9]{2,}', target.split('_', 1)[-1]) if len(w) > 2)
    make = target.split('_')[0].lower()
    scored = []
    for c, d in candidates.items():
        if c.lower() == target.lower() or d is None or tdims is None or c.lower().startswith('nul_') or 'slod' in c.lower(): continue
        sc = float(np.abs(np.log(np.maximum(tdims, 0.1) / np.maximum(d, 0.1))).sum())
        cw = set(w.lower() for w in re.findall(r'[A-Za-z][a-z]+|[A-Z0-9]{2,}', c.split('_', 1)[-1]) if len(w) > 2)
        if words & cw: sc -= 0.5
        if c.split('_')[0].lower() == make: sc -= 0.15
        scored.append((sc, c))
    return [c for _, c in sorted(scored)]
