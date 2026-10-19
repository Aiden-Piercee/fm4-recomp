#!/usr/bin/env python3
"""carbin_fm2.py - convert a Forza Motorsport 2 (Xbox 360) car .carbin to the Forza Motorsport 4 layout.

Usage:  python3 carbin_fm2.py <fm2.carbin> <fm4_template.carbin> <out.carbin>

The FM4 template must be an FM4 car body carbin (Car v2 / Model16 v16 / Model4 v4), ideally of the same
chassis.  Everything FM2 does not carry (car-root 256+80 bytes, emitters, gauges, LOD range, bone table,
DriverControls tail, ShaderVersion) is taken from it; all geometry comes from the FM2 file.

Structure (all big-endian):
  FM4: Car v2 | 256 B | 80 B | Model16 v16 | ShaderVersion | Emitters | DriverControls v3 (16 vec4)
       | StartLOD, EndLOD | 15 gauges x 36 B | 12 B | BoneMappingTable | Model4 v4 | OrderedSubModels v4
       | SubModel_Container (SubModel v5 ...) | MaterialSets | FxFileNames | MeshNames
  FM2: Car v2 | 256 B | ShaderVersion | Emitters | DriverControls v1 (6 vec4) | Model4 v4 | OrderedSubModels v4
       | SubModel_Container (SubModel v713 ...) | 2 shared vertex buffers (60-byte vertices)
See fm2_structure.md and CONVERTER_NOTES.md for the field map.  Fields marked GUESS are not confirmed.
"""
import struct, sys
import numpy as np

# ----------------------------------------------------------------------------------------------------------
# binary helpers
class Fmt(Exception):
    pass

class Rd:
    def __init__(s, d, o=0):
        s.d = d; s.o = o
    def u32(s):
        if s.o + 4 > len(s.d): raise Fmt('u32 past EOF at %x' % s.o)
        v = struct.unpack_from('>I', s.d, s.o)[0]; s.o += 4; return v
    def u8(s):
        if s.o >= len(s.d): raise Fmt('u8 past EOF at %x' % s.o)
        v = s.d[s.o]; s.o += 1; return v
    def raw(s, n):
        if n < 0 or s.o + n > len(s.d): raise Fmt('read past EOF at %x (+%d)' % (s.o, n))
        b = s.d[s.o:s.o + n]; s.o += n; return bytes(b)
    def string(s):
        n = s.u32()
        if n > 4096: raise Fmt('bad string len %d at %x' % (n, s.o - 4))
        return s.raw(n).decode('latin-1')
    def ver(s, allowed, what):
        v = s.u32()
        if v not in allowed: raise Fmt('%s: version %d not in %s at %x' % (what, v, allowed, s.o - 4))
        return v

class Wr:
    def __init__(s):
        s.b = bytearray()
    def u32(s, v): s.b += struct.pack('>I', v)
    def u8(s, v): s.b.append(v)
    def raw(s, b): s.b += b
    def string(s, t):
        b = t.encode('latin-1'); s.u32(len(b)); s.b += b

# ----------------------------------------------------------------------------------------------------------
# FM4 model: parse into dicts, write back.  write_fm4(parse_fm4(x)) == x for FM4 car body files.
# Grammar = tools/dlc/carbin.py (from the FM4 serializer).
def parse_fm4(d):
    r = Rd(d); m = {}
    m['car_ver'] = r.ver((2,), 'Car')
    m['root256'] = r.raw(256)
    m['car80'] = r.raw(80)
    m['m16_ver'] = r.ver((16,), 'Model16')
    m['shader_ver'] = r.u32()
    r.ver((1,), 'Emitters')
    m['emitters'] = []
    for _ in range(r.u32()):
        r.ver((2,), 'Emitter'); m['emitters'].append(r.raw(8 + 128))   # type, parent, 2 x mtx44
    m['dc_ver'] = r.ver((3,), 'DriverControls')
    m['dc'] = r.raw(256)                                                # 16 vec4
    m['lod_range'] = struct.unpack('>II', r.raw(8))                     # StartLOD, EndLOD
    m['gauges'] = r.raw(15 * 36)
    m['extra12'] = r.raw(12)
    r.ver((1,), 'BoneMappingTable')
    m['bones'] = [r.string() for _ in range(r.u32())]
    r.ver((4,), 'Model4'); r.ver((4,), 'OrderedSubModels')
    nl = r.u32(); lists = []
    for _ in range(2 * nl):
        n = r.u32(); lists.append([struct.unpack('>HH', r.raw(4)) for _ in range(n)])
    m['ordered'] = lists
    r.ver((1,), 'SubModel_Container')
    m['parts'] = [_parse_sub4(r) for _ in range(r.u32())]
    o = r.o; _skip_material_sets(r); m['material_sets'] = d[o:r.o]
    r.ver((1,), 'FxFileNames'); m['fx'] = [r.string() for _ in range(r.u32())]
    r.ver((1,), 'MeshNames'); m['meshnames'] = [r.string() for _ in range(r.u32())]
    if r.o != len(d): raise Fmt('trailing data at %x of %x' % (r.o, len(d)))
    return m

def _vb4(r, what):
    r.ver((2,), what); cnt = r.u32(); size = r.u32()
    return dict(count=cnt, stride=size, data=r.raw(cnt * size))

def _parse_sub4(r):
    p = {}
    p['ver'] = r.ver((5,), 'SubModel5')
    p['floats'] = list(struct.unpack('>14f', r.raw(56)))    # Offset3, targetMin3, targetMax3, f9, f10..12, f13
    r.ver((1,), 'GlassParticles')
    r.ver((1,), 'Glass.Vertices'); p['glass_verts'] = r.raw(16 * r.u32())
    r.ver((1,), 'Glass.Indices'); p['glass_idx'] = r.raw(2 * r.u32())
    p['flag'] = r.u8(); p['u32_after_flag'] = r.u32()
    r.ver((1,), 'SubModel1'); p['name'] = r.string()
    p['vb'] = _vb4(r, 'VertexBuffer')
    r.ver((1,), 'Mesh_Container')
    p['meshes'] = [_parse_mesh4(r) for _ in range(r.u32())]
    p['vb_lod0'] = _vb4(r, 'VertexBuffer_LOD0')
    r.ver((1,), 'MorphTargets'); p['morph_byte'] = r.u8()
    p['morph'] = _vb4(r, 'Morph'); p['morph_lod0'] = _vb4(r, 'Morph_LOD0')
    return p

def _parse_mesh4(r):
    x = {}
    x['ver'] = r.ver((3,), 'Mesh3'); x['byte'] = r.u8()
    x['block'] = list(struct.unpack('>16f', r.raw(64)))    # UV0 off/scale (4), UV1 off/scale (4), 8 x 0
    x['ver2'] = r.ver((2,), 'Mesh2'); x['material'] = r.string()
    x['lod'], x['prim'], x['w3'] = struct.unpack('>III', r.raw(12))
    r.ver((1,), 'UnpackingData'); x['unpack'] = r.raw(48)
    r.ver((3,), 'IndexBuffer'); cnt = r.u32(); fmt = r.u32()
    if fmt not in (2, 4): raise Fmt('IB format %d at %x' % (fmt, r.o))
    x['ib_fmt'] = fmt
    x['indices'] = np.frombuffer(r.raw(cnt * fmt), '>u2' if fmt == 2 else '>u4').astype(np.int64)
    x['trailer'] = r.u32()
    return x

def _skip_material_sets(r):
    r.ver((1,), 'MaterialSets')
    for _ in range(r.u32()):
        r.ver((1,), 'MaterialSet'); r.ver((1,), 'Materials')
        for _ in range(r.u32()):
            r.ver((3,), 'Material'); r.raw(8)
            for sz in (16, 16, 4):
                r.ver((1,), 'Mat.list'); r.raw(sz * r.u32())

def write_fm4(m):
    w = Wr()
    w.u32(m['car_ver']); w.raw(m['root256']); w.raw(m['car80'])
    w.u32(m['m16_ver']); w.u32(m['shader_ver'])
    w.u32(1); w.u32(len(m['emitters']))
    for e in m['emitters']: w.u32(2); w.raw(e)
    w.u32(m['dc_ver']); w.raw(m['dc'])
    w.raw(struct.pack('>II', *m['lod_range'])); w.raw(m['gauges']); w.raw(m['extra12'])
    w.u32(1); w.u32(len(m['bones']))
    for b in m['bones']: w.string(b)
    w.u32(4); w.u32(4)
    lists = m['ordered']; assert len(lists) % 2 == 0
    w.u32(len(lists) // 2)
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
        _wvb(w, p['vb'])
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
        _wvb(w, p['vb_lod0'])
        w.u32(1); w.u8(p['morph_byte']); _wvb(w, p['morph']); _wvb(w, p['morph_lod0'])
    w.raw(m['material_sets'])
    w.u32(1); w.u32(len(m['fx']))
    for t in m['fx']: w.string(t)
    w.u32(1); w.u32(len(m['meshnames']))
    for t in m['meshnames']: w.string(t)
    return bytes(w.b)

def _wvb(w, vb):
    assert len(vb['data']) == vb['count'] * vb['stride']
    w.u32(2); w.u32(vb['count']); w.u32(vb['stride']); w.raw(vb['data'])

# ----------------------------------------------------------------------------------------------------------
# FM4 32-byte car vertex (16 big-endian u16 columns).  Layout from fm3vert.py, re-verified on the FM4 Elise:
#   0-2  s16 unit direction from the part Offset (x32767)
#   3    u16 radius / |targetMax - targetMin| x 65535            (Elise: all vertices inside their box, err < 2e-5)
#   4,5  u16 UV0, de-normalised by the mesh block: u = block[0] + c4/65535*block[1], v = block[2] + c5/65535*block[3]
#   6,7  u16 UV1, block[4..7]
#   8-11 s16 quaternion (x,y,z,w) of the frame whose columns are [normal, axis1, axis2]:
#        column0 = normal (agrees with strip winding: even triangles (a,b,c) face along it),
#        column2 = -dP/dv, column1 = column2 x normal.  The quaternion sign is free (FM4 uses both signs).
#   12-15 s16 lighting/AO-like terms (FM2 bytes 28-35 x ~1.5)
def box_diag(floats):
    return float(np.linalg.norm(np.array(floats[6:9]) - np.array(floats[3:6])))

def quat_to_mat(q):
    q = q / np.maximum(np.linalg.norm(q, axis=1, keepdims=True), 1e-12)
    x, y, z, w = q.T
    return np.stack([
        np.stack([1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)], 1),
        np.stack([2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)], 1),
        np.stack([2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)], 1)], 1)

def mat_to_quat(R):
    """(n,3,3) rotation -> (n,4) x,y,z,w (Shepperd, vectorised); sign chosen so the largest component is positive
    (same convention as fm3vert.mat_to_quat)."""
    n = len(R); q = np.zeros((n, 4)); m = R
    t = m[:, 0, 0] + m[:, 1, 1] + m[:, 2, 2]
    k = np.stack([t, m[:, 0, 0], m[:, 1, 1], m[:, 2, 2]], 1).argmax(1)
    i = k == 0
    s = np.sqrt(np.maximum(1 + t[i], 1e-12)) * 2
    q[i] = np.stack([(m[i, 2, 1] - m[i, 1, 2]) / s, (m[i, 0, 2] - m[i, 2, 0]) / s, (m[i, 1, 0] - m[i, 0, 1]) / s, s / 4], 1)
    i = k == 1
    s = np.sqrt(np.maximum(1 + m[i, 0, 0] - m[i, 1, 1] - m[i, 2, 2], 1e-12)) * 2
    q[i] = np.stack([s / 4, (m[i, 0, 1] + m[i, 1, 0]) / s, (m[i, 0, 2] + m[i, 2, 0]) / s, (m[i, 2, 1] - m[i, 1, 2]) / s], 1)
    i = k == 2
    s = np.sqrt(np.maximum(1 + m[i, 1, 1] - m[i, 0, 0] - m[i, 2, 2], 1e-12)) * 2
    q[i] = np.stack([(m[i, 0, 1] + m[i, 1, 0]) / s, s / 4, (m[i, 1, 2] + m[i, 2, 1]) / s, (m[i, 0, 2] - m[i, 2, 0]) / s], 1)
    i = k == 3
    s = np.sqrt(np.maximum(1 + m[i, 2, 2] - m[i, 0, 0] - m[i, 1, 1], 1e-12)) * 2
    q[i] = np.stack([(m[i, 0, 2] + m[i, 2, 0]) / s, (m[i, 1, 2] + m[i, 2, 1]) / s, s / 4, (m[i, 1, 0] - m[i, 0, 1]) / s], 1)
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    big = np.abs(q).argmax(1)
    q *= np.where(q[np.arange(n), big] < 0, -1.0, 1.0)[:, None]
    return q

def decode_fm4_vertices(raw, floats):
    """-> dict(pos_rel (n,3) metres from the part Offset, normal (n,3), frame (n,3,3), cols (n,16) u16, S signed)."""
    U = np.frombuffer(raw, '>u2').reshape(-1, 16).astype(np.int64)
    S = np.where(U >= 32768, U - 65536, U).astype(np.float64)
    d = S[:, 0:3] / 32767.0
    r = U[:, 3] / 65535.0 * box_diag(floats)
    R = quat_to_mat(S[:, 8:12] / 32767.0)
    return dict(pos_rel=d * r[:, None], normal=R[:, :, 0], frame=R, cols=U, S=S)

def encode_fm4_vertices(pos_rel, normal, tangent, uv_cols, floats, extra=None):
    """pos_rel (n,3), normal (n,3) unit, tangent (n,3) = unit dP/dv _|_ normal, uv_cols (n,4) u16 already quantised,
    extra (n,4) s16 for cols 12-15 or None (zeros)."""
    n = len(pos_rel); out = np.zeros((n, 16), np.int64)
    r = np.linalg.norm(pos_rel, axis=1)
    d = np.where(r[:, None] > 1e-12, pos_rel / np.maximum(r, 1e-12)[:, None], 0.0)
    out[:, 0:3] = np.clip(np.round(d * 32767), -32767, 32767)
    D = box_diag(floats)
    out[:, 3] = np.clip(np.round(r / D * 65535), 0, 65535) if D > 0 else 0
    out[:, 4:8] = uv_cols
    a0 = normal; a2 = -tangent; a1 = np.cross(a2, a0)
    q = mat_to_quat(np.stack([a0, a1, a2], 2))
    out[:, 8:12] = np.clip(np.round(q * 32767), -32767, 32767)
    if extra is not None: out[:, 12:16] = extra
    return (out & 0xFFFF).astype('>u2').tobytes()

# ----------------------------------------------------------------------------------------------------------
# FM2 reader
FM2_SUBMODEL_VER = 713       # 0x2c9: per-part version word (FM4 uses 5)
DECAF = 0x000DECAF           # shared vertex buffer magic (header and trailer)
LIGHT_SCALE = 1.5            # GUESS (empirical): FM4 cols 12-15 ~= 1.5 x FM2 s16 @28..34 (fit 1.42-1.52, corr 0.85-0.97)

def parse_fm2(d):
    r = Rd(d); m = {}
    m['car_ver'] = r.ver((2,), 'Car')
    m['root256'] = r.raw(256)                  # 4 x 4 vec4; meaning differs from FM4 (GUESS) -> not used
    m['shader_ver'] = r.u32()                  # 714 -> not used, FM4 template value written
    r.ver((1,), 'Emitters')
    m['emitters'] = []
    for _ in range(r.u32()):
        r.ver((2,), 'Emitter'); m['emitters'].append(r.raw(8 + 128))   # parent = 17/18, frame unsolved -> not used
    r.ver((1,), 'DriverControls'); m['dc6'] = r.raw(96)              # 6 vec4, same meaning as FM4's first 6
    r.ver((4,), 'Model4'); r.ver((4,), 'OrderedSubModels')
    nl = r.u32(); lists = []
    for _ in range(2 * nl):
        n = r.u32(); lists.append([struct.unpack('>HH', r.raw(4)) for _ in range(n)])
    m['ordered'] = lists
    r.ver((1,), 'SubModel_Container')
    m['parts'] = [_parse_sub2(r) for _ in range(r.u32())]
    # two shared vertex buffers: [2][1][count][60][DECAF] data [DECAF]
    m['buffers'] = []
    while r.o < len(d):
        r.ver((2,), 'SharedVB'); one = r.u32(); cnt = r.u32(); stride = r.u32(); magic = r.u32()
        if one != 1 or stride != 60 or magic != DECAF: raise Fmt('shared VB header at %x' % (r.o - 20))
        data = r.raw(cnt * stride)
        if r.u32() != DECAF: raise Fmt('shared VB trailer at %x' % (r.o - 4))
        m['buffers'].append(np.frombuffer(data, np.uint8).reshape(cnt, stride))
    if len(m['buffers']) != 2: raise Fmt('expected 2 shared vertex buffers, got %d' % len(m['buffers']))
    return m

def _parse_sub2(r):
    p = {}
    r.ver((FM2_SUBMODEL_VER,), 'SubModel(FM2)')
    p['floats'] = list(struct.unpack('>14f', r.raw(56)))   # Offset3, targetMin3, targetMax3, f9, f10..12, f13
    r.ver((1,), 'GlassParticles')
    r.ver((1,), 'Glass.Vertices'); p['glass_verts'] = r.raw(16 * r.u32())
    r.ver((1,), 'Glass.Indices'); p['glass_idx'] = r.raw(2 * r.u32())
    p['first'], p['last'] = struct.unpack('>HH', r.raw(4))  # inclusive range into the shared buffer
    p['flag'] = r.u8()                                       # 1 -> buffer 0 ("alpha"), 0 -> buffer 1 ("opaque")
    r.ver((1,), 'SubModel1'); p['name'] = r.string()
    stub = struct.unpack('>4I', r.raw(16))                   # [2][1][0][60]: empty VB stub
    if stub != (2, 1, 0, 60): raise Fmt('VB stub %s at %x' % (stub, r.o - 16))
    r.ver((1,), 'Mesh_Container')
    p['meshes'] = []
    for _ in range(r.u32()):
        x = {}
        r.ver((1,), 'Mesh(FM2)'); r.ver((3,), 'Mesh2(FM2)')
        x['material'] = r.string()
        x['prim'] = r.u32(); x['lod'] = r.u32()               # FM2 order: prim, LOD
        r.ver((3,), 'IndexBuffer'); cnt = r.u32(); fmt = r.u32()
        if fmt != 2: raise Fmt('FM2 IB format %d at %x' % (fmt, r.o))
        x['indices'] = np.frombuffer(r.raw(2 * cnt), '>u2').astype(np.int64)
        p['meshes'].append(x)
    return p

def fm2_vertex_fields(rows):
    """rows: (n,60) uint8 -> pos_rel (n,3) metres, normal (n,3) unit, uv (n,2), light (n,4) s16 for FM4 cols 12-15.
    FM2 60-byte vertex (big-endian):
      0-5   half3 direction, 6-7 half scale: position = dir x scale, relative to the part Offset    [confirmed]
      8-11  u16 x2 UV0: uv = (u16 - 32768) * 25/32768 (range [-25,25), no V flip)                  [confirmed vs FM4]
      12-15 s16 x2, always 0 on this car (UV1 slot?)                                                [not used]
      16-19 u32 normal, x bits 0-10 / y 11-21 (snorm /1023), z 22-31 (snorm /511)                   [confirmed]
      20-27 4 x u16 unknown (NOT UV: no correlation with FM4 UV0/UV1; not a tangent either)         [not used]
      28-35 s16 x4 lighting/AO-like terms = FM4 cols 12-15 / ~1.5                                   [empirical]
      36-59 s16 x12 unknown                                                                         [not used]"""
    H = np.ascontiguousarray(rows[:, 0:8]).view('>f2').astype(np.float64)
    pos = H[:, 0:3] * H[:, 3:4]
    w = np.ascontiguousarray(rows[:, 16:20]).view('>u4').astype(np.int64)[:, 0]
    def sx(v, b):
        v = np.where(v >= (1 << (b - 1)), v - (1 << b), v); return v / float((1 << (b - 1)) - 1)
    nrm = np.stack([sx(w & 0x7FF, 11), sx((w >> 11) & 0x7FF, 11), sx((w >> 22) & 0x3FF, 10)], 1)
    nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
    U = np.ascontiguousarray(rows[:, 8:12]).view('>u2').astype(np.float64)
    uv = (U - 32768.0) * (25.0 / 32768.0)       # u16 biased by 0x8000, 1310.72 steps per UV unit
    L = np.ascontiguousarray(rows[:, 28:36]).view('>i2').astype(np.float64)
    light = np.clip(np.round(L * LIGHT_SCALE), -32767, 32767).astype(np.int64)
    return pos, nrm, uv, light

# ----------------------------------------------------------------------------------------------------------
# conversion
def strip_tris(idx, n):
    """Triangle strip with 0xFFFF restarts -> (m,3) triangles (degenerates dropped, odd triangles re-wound)."""
    out = []; s = []
    def flush():
        for i in range(len(s) - 2):
            a, b, c = s[i:i + 3]
            if a != b and b != c and a != c: out.append((a, b, c) if i % 2 == 0 else (b, a, c))
    for v in idx:
        if v >= n: flush(); s.clear()
        else: s.append(int(v))
    flush()
    return np.array(out, np.int64).reshape(-1, 3)

def tangents(pos, nrm, uv, tris):
    """Per-vertex dP/dv (the V-direction tangent) from UV derivatives, Gram-Schmidt against the normal;
    fallback for vertices without usable UVs: normal x X (or normal x Y).
    FM4 frame convention measured on the FM4 Elise: column2 = -dP/dv (dot -1.00 on every part checked),
    column1 = column2 x normal (= +-dP/du; the quaternion cannot carry handedness, FM4 does not encode it)."""
    n = len(pos); tv = np.zeros((n, 3))
    if len(tris) and np.ptp(uv) > 0:
        a, b, c = tris.T
        e1 = pos[b] - pos[a]; e2 = pos[c] - pos[a]; d1 = uv[b] - uv[a]; d2 = uv[c] - uv[a]
        det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
        ok = np.abs(det) > 1e-12
        dv = np.zeros_like(e1)
        dv[ok] = (e2[ok] * d1[ok, 0:1] - e1[ok] * d2[ok, 0:1]) / det[ok, None]   # dP/dv
        for k in (a, b, c): np.add.at(tv, k, dv)
    tv -= (tv * nrm).sum(1, keepdims=True) * nrm
    nv = np.linalg.norm(tv, axis=1, keepdims=True)
    fb = np.cross(nrm, np.array([1.0, 0, 0]))
    bad = np.linalg.norm(fb, axis=1) < 1e-3
    fb[bad] = np.cross(nrm[bad], np.array([0, 1.0, 0]))
    fb /= np.linalg.norm(fb, axis=1, keepdims=True)
    return np.where(nv > 1e-9, tv / np.maximum(nv, 1e-9), fb)

def convert(fm2_bytes, tmpl_bytes, log=print):
    f2 = parse_fm2(fm2_bytes)
    t = parse_fm4(tmpl_bytes)
    unpack = t['parts'][0]['meshes'][0]['unpack']      # constant UnpackingData (identical on every FM4 mesh)
    out = dict(t)                                      # root 256+80, ShaderVersion, emitters, LOD range, gauges,
                                                       # 12 extra bytes, bone table: all from the FM4 template
    out['dc'] = f2['dc6'] + t['dc'][96:]               # DriverControls: FM2 vec4 0-5 + template vec4 6-15
    out['ordered'] = f2['ordered']                     # same (part, mesh) numbering -> copied verbatim
    out['material_sets'] = struct.pack('>II', 1, 0)    # empty (as on the FM4 Elise)
    out['fx'] = []; out['meshnames'] = []
    parts = []
    for i, p in enumerate(f2['parts']):
        rows = f2['buffers'][0 if p['flag'] else 1][p['first']:p['last'] + 1]
        n = len(rows)
        used = [x['indices'][x['indices'] != 0xFFFF] for x in p['meshes']]
        maxidx = max(int(u.max()) for u in used if len(u))
        if maxidx + 1 != n: raise Fmt('%s: max index %d vs range %d' % (p['name'], maxidx, n))
        pos, nrm, uv, light = fm2_vertex_fields(rows)
        tris = np.concatenate([strip_tris(x['indices'], n) for x in p['meshes'] if x['lod'] == 1] or [np.zeros((0, 3), np.int64)])
        tan = tangents(pos, nrm, uv, tris)
        # UV0 quantised with one (min, range) block per part, repeated in every mesh of the part: the part's
        # vertices are shared by all its meshes/LODs, so one block keeps every mesh consistent.  FM2's UV step is
        # 25/32768; a range <= 50 over 65535 steps is at least as fine, so this is lossless in practice.
        uv_cols = np.zeros((n, 4), np.int64)
        umin, vmin = uv.min(0); urg, vrg = np.ptp(uv, 0)
        if urg > 0: uv_cols[:, 0] = np.round((uv[:, 0] - umin) / urg * 65535)
        if vrg > 0: uv_cols[:, 1] = np.round((uv[:, 1] - vmin) / vrg * 65535)
        block = [umin, urg, vmin, vrg] + [0.0] * 12          # UV1 block zero (FM2 has no UV1), 8 trailing zeros
        fl = list(p['floats']); fl[9] = 1.0                 # GUESS: f9 is always 1.0 in FM4, 0/1 in FM2
        vb = encode_fm4_vertices(pos, nrm, tan, uv_cols, fl, extra=light)
        meshes = [dict(ver=3, byte=0, block=list(block), ver2=2, material=x['material'],
                       lod=x['lod'], prim=x['prim'], w3=0, unpack=unpack, ib_fmt=2,
                       indices=x['indices'], trailer=0) for x in p['meshes']]
        parts.append(dict(ver=5, floats=fl, glass_verts=p['glass_verts'], glass_idx=p['glass_idx'],
                          flag=p['flag'],                    # FM2 alpha-buffer flag kept (same meaning as FM4's byte)
                          u32_after_flag=0, name=p['name'],
                          vb=dict(count=n, stride=32, data=vb),
                          meshes=meshes,
                          vb_lod0=dict(count=0, stride=0, data=b''),       # empty, as on every FM4 Elise part
                          morph_byte=0,                                     # no damage morphs (FM2 has none)
                          morph=dict(count=0, stride=0, data=b''),
                          morph_lod0=dict(count=0, stride=0, data=b'')))
        log('%2d %-20s buf%d %5d-%5d verts %5d meshes %2d uv [%.3f..%.3f]x[%.3f..%.3f]' % (
            i, p['name'], 0 if p['flag'] else 1, p['first'], p['last'], n, len(meshes), umin, umin + urg, vmin, vmin + vrg))
    out['parts'] = parts
    return write_fm4(out), dict(fm2=f2, template=t)

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__); sys.exit(2)
    src = open(sys.argv[1], 'rb').read(); tmpl = open(sys.argv[2], 'rb').read()
    data, _ = convert(src, tmpl)
    parse_fm4(data)                                     # self-check: the output must re-parse
    open(sys.argv[3], 'wb').write(data)
    print('wrote %s (%d bytes; FM2 %d, template %d)' % (sys.argv[3], len(data), len(src), len(tmpl)))
