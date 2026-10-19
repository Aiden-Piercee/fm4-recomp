#!/usr/bin/env python3
"""carbin_fm2_wheel.py - convert a Forza Motorsport 2 wheel .carbin to the FM4 wheel layout.

Usage: python3 carbin_fm2_wheel.py <fm2_wheel.carbin> <fm4_template_wheel.carbin> <out.carbin>

FM2 wheel (all big-endian): [1] [ShaderVersion 714] Emitters(v1, 0) DriverControls(v1, 96 B)
  Model4 v4 | OrderedSubModels v4 (5 LOD list pairs) | one SubModel 'wheel' (meshes LOD1..5, no LOD0)
  | 2 shared 60-byte vertex buffers.
FM4 wheel: [3] 68 B root | Model16 v16 ... | 6 LOD list pairs | SubModel v5 with VertexBuffer (LOD1-5) and
  VertexBuffer_LOD0 (LOD0), 32-byte vertices, u32 index strips restarted by 0xFFFFFF.

Mapping: FM2 LOD1 (rim, inner_rim, blur_rim, blur_lip; no tyre) is also written as FM4 LOD0, FM2 LOD1..5 as
FM4 LOD1..5. Vertices come from the FM2 shared buffer re-encoded with carbin_fm2.encode_fm4_vertices.
UV1 is a copy of UV0 (FM2 has no second set). Damage morph targets are dropped.
"""
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import carbin_fm2 as C
from carbin_fm2 import Rd, Wr, Fmt

RESTART32 = 0xFFFFFF
REF_R0 = 0.194          # native LOD0 rim radius, identical on every FM4 wheel


def parse_fm2_wheel(d):
    r = Rd(d); m = {}
    r.ver((1,), 'Wheel'); m['shader'] = r.u32()
    r.ver((1,), 'Emitters')
    if r.u32() != 0: raise Fmt('FM2 wheel has emitters')
    r.ver((1,), 'DriverControls'); r.raw(96)
    r.ver((4,), 'Model4'); r.ver((4,), 'OrderedSubModels')
    nl = r.u32(); lists = []
    for _ in range(2 * nl):
        n = r.u32(); lists.append([struct.unpack('>HH', r.raw(4)) for _ in range(n)])
    r.ver((1,), 'SubModel_Container')
    m['parts'] = [C._parse_sub2(r) for _ in range(r.u32())]
    m['buffers'] = []
    while r.o < len(d):
        r.ver((2,), 'SharedVB'); one = r.u32(); cnt = r.u32(); stride = r.u32()
        if cnt == 0:                                   # empty second buffer: 16-byte stub, no magic
            m['buffers'].append(np.zeros((0, 60), np.uint8)); continue
        magic = r.u32()
        if one != 1 or stride != 60 or magic != C.DECAF: raise Fmt('shared VB header at %x' % (r.o - 20))
        data = r.raw(cnt * stride)
        if r.u32() != C.DECAF: raise Fmt('shared VB trailer')
        m['buffers'].append(np.frombuffer(data, np.uint8).reshape(cnt, stride))
    return m


def parse_fm4_wheel(d):
    r = Rd(d); m = {}
    m['ver'] = r.ver((3,), 'Wheel'); m['root68'] = r.raw(68)
    m['m16_ver'] = r.ver((16,), 'Model16'); m['shader_ver'] = r.u32()
    r.ver((1,), 'Emitters'); m['emitters'] = []
    for _ in range(r.u32()):
        r.ver((2,), 'Emitter'); m['emitters'].append(r.raw(8 + 128))
    m['dc_ver'] = r.ver((3,), 'DriverControls'); m['dc'] = r.raw(256)
    m['lod_range'] = struct.unpack('>II', r.raw(8)); m['gauges'] = r.raw(15 * 36); m['extra12'] = r.raw(12)
    r.ver((1,), 'BoneMappingTable'); m['bones'] = [r.string() for _ in range(r.u32())]
    r.ver((4,), 'Model4'); r.ver((4,), 'OrderedSubModels')
    nl = r.u32(); lists = []
    for _ in range(2 * nl):
        n = r.u32(); lists.append([struct.unpack('>HH', r.raw(4)) for _ in range(n)])
    m['ordered'] = lists
    r.ver((1,), 'SubModel_Container'); m['parts'] = [C._parse_sub4(r) for _ in range(r.u32())]
    o = r.o; C._skip_material_sets(r); m['material_sets'] = d[o:r.o]
    r.ver((1,), 'FxFileNames'); m['fx'] = [r.string() for _ in range(r.u32())]
    r.ver((1,), 'MeshNames'); m['meshnames'] = [r.string() for _ in range(r.u32())]
    if r.o != len(d): raise Fmt('trailing data')
    return m


def write_fm4_wheel(m):
    w = Wr()
    w.u32(m['ver']); w.raw(m['root68'])
    w.u32(m['m16_ver']); w.u32(m['shader_ver'])
    w.u32(1); w.u32(len(m['emitters']))
    for e in m['emitters']: w.u32(2); w.raw(e)
    w.u32(m['dc_ver']); w.raw(m['dc'])
    w.raw(struct.pack('>II', *m['lod_range'])); w.raw(m['gauges']); w.raw(m['extra12'])
    w.u32(1); w.u32(len(m['bones']))
    for b in m['bones']: w.string(b)
    w.u32(4); w.u32(4)
    lists = m['ordered']; w.u32(len(lists) // 2)
    for L in lists:
        w.u32(len(L))
        for a, b in L: w.raw(struct.pack('>HH', a, b))
    w.u32(1); w.u32(len(m['parts']))
    for p in m['parts']:
        w.u32(p['ver']); w.raw(struct.pack('>14f', *p['floats']))
        w.u32(1)
        w.u32(1); w.u32(len(p['glass_verts']) // 16); w.raw(p['glass_verts'])
        w.u32(1); w.u32(len(p['glass_idx']) // 2); w.raw(p['glass_idx'])
        w.u8(p['flag']); w.u32(p['u32_after_flag'])
        w.u32(1); w.string(p['name'])
        C._wvb(w, p['vb'])
        w.u32(1); w.u32(len(p['meshes']))
        for x in p['meshes']:
            w.u32(x['ver']); w.u8(x['byte']); w.raw(struct.pack('>16f', *x['block']))
            w.u32(x['ver2']); w.string(x['material'])
            w.raw(struct.pack('>III', x['lod'], x['prim'], x['w3']))
            w.u32(1); w.raw(x['unpack'])
            idx = np.asarray(x['indices'])
            w.u32(3); w.u32(len(idx)); w.u32(x['ib_fmt'])
            w.raw(idx.astype('>u2' if x['ib_fmt'] == 2 else '>u4').tobytes())
            w.u32(x['trailer'])
        C._wvb(w, p['vb_lod0'])
        w.u32(1); w.u8(p['morph_byte']); C._wvb(w, p['morph']); C._wvb(w, p['morph_lod0'])
    w.raw(m['material_sets'])
    w.u32(1); w.u32(len(m['fx']))
    for t in m['fx']: w.string(t)
    w.u32(1); w.u32(len(m['meshnames']))
    for t in m['meshnames']: w.string(t)
    return bytes(w.b)


def convert(fm2_bytes, tmpl_bytes, log=print):
    f2 = parse_fm2_wheel(fm2_bytes)
    t = parse_fm4_wheel(tmpl_bytes)
    tp = t['parts'][0]
    unpack = tp['meshes'][0]['unpack']
    if len(f2['parts']) != 1: raise Fmt('expected one wheel part')
    p = f2['parts'][0]
    rows = f2['buffers'][0 if p['flag'] else 1][p['first']:p['last'] + 1]
    n = len(rows)
    pos, nrm, uv, light = C.fm2_vertex_fields(rows)
    tris = np.concatenate([C.strip_tris(x['indices'], n) for x in p['meshes'] if x['lod'] == 1])
    tan = C.tangents(pos, nrm, uv, tris)
    uv_cols = np.zeros((n, 4), np.int64)
    umin, vmin = uv.min(0); urg, vrg = np.ptp(uv, 0)
    if urg > 0: uv_cols[:, 0] = np.round((uv[:, 0] - umin) / urg * 65535)
    if vrg > 0: uv_cols[:, 1] = np.round((uv[:, 1] - vmin) / vrg * 65535)
    uv_cols[:, 2:4] = uv_cols[:, 0:2]                      # UV1 = UV0
    block = [umin, urg, vmin, vrg, umin, urg, vmin, vrg] + [0.0] * 8
    fl = list(p['floats']); fl[9] = 1.0
    vb = C.encode_fm4_vertices(pos, nrm, tan, uv_cols, fl, extra=light)
    # FM4 LOD0 models are all authored at one reference rim radius (0.194 m, measured on native rims) and the
    # game scales them to the chosen rim size; LOD1-5 are authored at the wheel's own rim radius (root float 9).
    # FM2's LOD1 rim is larger than 0.194, so a straight copy came out 1-3 inches too big: squash the LOD0 copy
    # radially (y,z; the axle axis x keeps its width) to the reference radius.
    off = np.array(fl[0:3]); ab = pos + off
    rim_idx = np.unique(np.concatenate([x['indices'][x['indices'] != 0xFFFF] for x in p['meshes']
                                        if x['lod'] == 1 and x['material'] in ('rim', 'inner_rim', 'blur_lip')]))
    R1 = float(np.hypot(ab[rim_idx, 1], ab[rim_idx, 2]).max())
    k = REF_R0 / R1
    pos0 = pos.copy(); pos0[:, 1:3] *= k
    n0 = nrm.copy(); n0[:, 1:3] /= k; n0 /= np.linalg.norm(n0, axis=1, keepdims=True)
    tan0 = C.tangents(pos0, n0, uv, tris)
    vb0 = C.encode_fm4_vertices(pos0, n0, tan0, uv_cols, fl, extra=light)

    # meshes: FM4 LOD0 from FM2 LOD1 without the tyre, then FM2 LOD1..5 as is
    src = []
    for x in p['meshes']:
        idx = np.asarray(x['indices'], np.int64)
        idx = np.where(idx == 0xFFFF, RESTART32, idx)
        src.append((x['lod'], x['material'], idx))
    out_meshes = []
    for lod_out in range(6):
        lod_in = max(lod_out, 1)
        for lod, mat, idx in src:
            if lod != lod_in: continue
            if lod_out == 0 and mat == 'tire': continue
            out_meshes.append(dict(ver=3, byte=0, block=list(block), ver2=2, material=mat, lod=lod_out, prim=6, w3=0,
                                   unpack=unpack, ib_fmt=4, indices=idx, trailer=0))
    lists_t = [[] for _ in range(6)]; lists_o = [[] for _ in range(6)]
    for i, x in enumerate(out_meshes):
        (lists_t if x['material'] == 'blur_rim' else lists_o)[x['lod']].append(i)
    for l in range(6):
        lists_o[l].sort(key=lambda i: out_meshes[i]['material'])
    ordered = [[(0, i) for i in L] for L in lists_t] + [[(0, i) for i in L] for L in lists_o]

    # root: 12 floats.  Meaning measured on native rims: [1] inner_rim x min, [3]=[7] tyre x max, [5] tyre x min,
    # [2] inner_rim x max, [9] LOD1 rim radius, [11] tyre (part box) radius; [8]=[10]=0.324 constant;
    # [0]=[4]=[6] vary little between wheels and keep the template value scaled by the width ratio.
    tf = list(struct.unpack('>12f', t['root68'][20:68]))
    th = (tp['floats'][6] - tp['floats'][3]) / 2
    sw = ((p['floats'][6] - p['floats'][3]) / 2) / th
    def xr(mat, lod=1):
        ix = np.unique(np.concatenate([x['indices'][x['indices'] != 0xFFFF] for x in p['meshes'] if x['lod'] == lod and x['material'] == mat]))
        return float(ab[ix, 0].min()), float(ab[ix, 0].max())
    rf = [v * sw for v in tf]
    inn = xr('inner_rim'); tir = xr('tire')
    rf[1] = inn[0]; rf[2] = inn[1]; rf[3] = rf[7] = tir[1]; rf[5] = tir[0]
    rf[9] = R1; rf[11] = max(abs(p['floats'][4]), abs(p['floats'][5]))
    rf[8] = tf[8]; rf[10] = tf[10]
    root = bytes(20) + struct.pack('>12f', *rf)
    sr = rf[11] / tf[11]

    out = dict(t)
    out['root68'] = root; out['ordered'] = ordered
    out['parts'] = [dict(ver=5, floats=fl, glass_verts=b'', glass_idx=b'', flag=1, u32_after_flag=0, name=p['name'],
                         vb=dict(count=n, stride=32, data=vb), meshes=out_meshes,
                         vb_lod0=dict(count=n, stride=32, data=vb0),
                         morph_byte=0, morph=dict(count=0, stride=0, data=b''), morph_lod0=dict(count=0, stride=0, data=b''))]
    log('verts %d  meshes %d  R1 %.4f k %.3f sw %.3f sr %.3f  uv [%.3f..%.3f]x[%.3f..%.3f]' % (n, len(out_meshes), R1, k, sw, sr, umin, umin + urg, vmin, vmin + vrg))
    return write_fm4_wheel(out)


if __name__ == '__main__':
    if len(sys.argv) != 4: print(__doc__); sys.exit(2)
    data = convert(open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read())
    parse_fm4_wheel(data)
    open(sys.argv[3], 'wb').write(data)
    print('wrote', sys.argv[3], len(data))
