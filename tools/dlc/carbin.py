"""Forza .carbin transcoder: FH (Model16 v17 / Car v5 / IB v4 / VB v3) -> FM4 (v16 / v2 / v3 / v2).

Grammar derived from the FM4 and FH default.xex serializers. Reads either dialect, always writes FM4.
FH-only data dropped: Car v3+ item list (32-byte records), VB/IB flags+key.
FH 28-byte car vertices are expanded to FM4's 32-byte layout (bytes 24..27: s8 -> s16 << 8).
Validated: FM4 files round-trip byte-identical; FH Challenger brakes convert to the FM4
Challenger brakes' exact size and structure (only low vertex bytes differ).
Usage: python carbin.py in.carbin out.carbin   (or import convert())
"""
import struct, sys

class Fmt(Exception): pass

class R:
    def __init__(s, d): s.d = d; s.o = 0
    def u32(s):
        v = struct.unpack_from('>I', s.d, s.o)[0]; s.o += 4; return v
    def u8(s):
        v = s.d[s.o]; s.o += 1; return v
    def raw(s, n):
        if s.o + n > len(s.d): raise Fmt('read past EOF at %x (+%d)' % (s.o, n))
        b = s.d[s.o:s.o + n]; s.o += n; return b
    def string(s):
        n = s.u32()
        if n > 4096: raise Fmt('bad string len %d at %x' % (n, s.o - 4))
        return s.raw(n)

class W:
    def __init__(s): s.b = bytearray()
    def u32(s, v): s.b += struct.pack('>I', v)
    def u8(s, v): s.b.append(v)
    def raw(s, b): s.b += b
    def string(s, b): s.u32(len(b)); s.b += b

class Conv:
    def __init__(s, data, log=None):
        s.r = R(data); s.w = W(); s.stats = {}; s.log = log
    def note(s, k): s.stats[k] = s.stats.get(k, 0) + 1
    # helpers
    def copy(s, n): s.w.raw(s.r.raw(n))
    def cu32(s): v = s.r.u32(); s.w.u32(v); return v
    def ver(s, allowed, out=None, what=''):
        v = s.r.u32()
        if v not in allowed: raise Fmt('%s: version %d not in %s at %x' % (what, v, allowed, s.r.o - 4))
        s.w.u32(out if out is not None else v); return v
    def cstr(s): s.w.string(s.r.string())

    def counted(s, what, elem):
        s.ver((1,), what=what)
        n = s.cu32()
        s.copy(n * elem)
        return n

    def strlist(s, what):
        s.ver((1,), what=what)
        n = s.cu32()
        for _ in range(n): s.cstr()

    # ---- top level ----
    def run(s):
        first = struct.unpack_from('>I', s.r.d, 0)[0]
        if first == 1:
            s.ver((1,), what='Model'); s.model16(); s.model4()
        elif first == 3:                       # wheel/tyre root: identical in FM4 and FH
            s.ver((3,), what='Wheel'); s.copy(68); s.model16(); s.model4()
        elif first in (2, 5):
            s.car_root(); s.model16(); s.model4()
        else:
            raise Fmt('unknown root version %d' % first)
        if s.r.o != len(s.r.d):
            raise Fmt('trailing data: stopped at %x of %x' % (s.r.o, len(s.r.d)))
        return bytes(s.w.b)

    def car_root(s):
        v = s.ver((2, 3, 4, 5), out=2, what='Car')
        s.copy(256)
        if v > 1: s.copy(80)
        if v >= 3:
            n = s.r.u32()
            sz = {3: 27, 4: 30, 5: 32}[v]
            if n: s.note('car_fh_items_dropped=%d' % n)
            s.r.raw(n * sz)

    def model16(s):
        v = s.ver((16, 17), out=16, what='Model16')
        s.cu32()                                   # ShaderVersion
        s.ver((1,), what='Emitters')
        for _ in range(s.cu32()):
            s.ver((2,), what='Emitter'); s.copy(8 + 128)   # type, parent, 2x mtx64
        s.ver((3,), what='DriverControls'); s.copy(256)
        s.copy(8)                                  # StartLOD, EndLOD
        s.copy(15 * 36)                            # gauges
        s.copy(12)                                 # v>=8 two u32, v>=0xe one u32
        s.strlist('BoneMappingTable')

    def model4(s):
        s.ver((4,), what='Model4')
        s.ver((4,), what='OrderedSubModels')
        nl = s.cu32()
        for _ in range(nl):
            for _ in range(2):
                n = s.cu32(); s.copy(n * 4)
        s.ver((1,), what='SubModel_Container')
        n = s.cu32()
        for _ in range(n): s.submodel()
        s.material_sets()
        s.strlist('FxFileNames')
        s.strlist('MeshNames')

    def submodel(s):
        v = s.ver((4, 5), what='SubModel5')
        s.copy(56)
        s.ver((1,), what='GlassParticles')
        s.counted('Glass.Vertices', 16)
        s.counted('Glass.Indices', 2)
        s.copy(1)
        if v >= 3: s.copy(4)
        # inner TSubModel
        s.ver((1,), what='SubModel1')
        s.cstr()
        s.vb('VertexBuffer')
        s.ver((1,), what='Mesh_Container')
        n = s.cu32()
        for _ in range(n): s.mesh()
        if v > 1: s.vb('VertexBuffer_LOD0')
        if v >= 5:
            s.ver((1,), what='MorphTargets'); s.copy(1)
            s.vb('Morph'); s.vb('Morph_LOD0')

    def _bad(s, m): raise Fmt(m + ' at %x' % s.r.o)

    def vb(s, what):
        v = s.ver((2, 3), out=2, what=what)
        cnt = s.cu32(); size = s.cu32()
        if v == 3:
            flags = s.r.u32()
            if flags & 1: s.r.u32(); s.note('vb_key')
            if flags & ~3: s.note('vb_flags_%x' % flags)
            if flags & 2:
                raise Fmt('%s: data absent (flags=%x) at %x' % (what, flags, s.r.o))
        if s.fh and size == 28:
            # FH packs the last 4 signed 16-bit components (FM4 bytes 24..31) as signed bytes.
            s.w.b[-4:] = struct.pack('>I', 32)
            src = s.r.raw(cnt * 28); out = bytearray(cnt * 32)
            for i in range(cnt):
                o = i * 28; p = i * 32
                out[p:p + 24] = src[o:o + 24]
                out[p + 24:p + 32] = bytes((src[o + 24], 0, src[o + 25], 0, src[o + 26], 0, src[o + 27], 0))
            s.w.raw(bytes(out)); s.note('vb28to32')
            return
        s.copy(cnt * size)

    def mesh(s):
        v = s.ver((1, 2, 3), what='Mesh3')
        if v >= 2: s.copy(1)
        if v >= 3: s.copy(64)
        v2 = s.ver((1, 2), what='Mesh2')
        s.cstr(); s.copy(12)
        s.ver((1,), what='UnpackingData'); s.copy(48)
        s.ib()
        if v2 >= 2: s.copy(4)

    def ib(s):
        v = s.ver((3, 4), out=3, what='IndexBuffer')
        if v >= 4:
            flags = s.r.u32()
            if flags & 1: s.r.u32(); s.note('ib_key')
            if flags & ~1: s.note('ib_flags_%x' % flags)
        cnt = s.cu32(); fmt = s.cu32()
        if fmt not in (2, 4): raise Fmt('IB format %d at %x' % (fmt, s.r.o))
        s.copy(cnt * (2 if fmt == 2 else 4))

    def material_sets(s):
        s.ver((1,), what='MaterialSets')
        n = s.cu32()
        for _ in range(n):
            s.ver((1,), what='MaterialSet')
            s.ver((1,), what='Materials')
            m = s.cu32()
            for _ in range(m):
                s.ver((3,), what='Material')
                s.copy(8)
                s.counted('Mat.vec4a', 16)
                s.counted('Mat.vec4b', 16)
                s.counted('Mat.u32', 4)

def convert(data, fh=None):
    c = Conv(data)
    if fh is None:
        # dialect detection: FH files carry Model16 v17
        first = struct.unpack_from('>I', data, 0)[0]
        m16 = {1: 4, 3: 0x48}.get(first)
        fh = first >= 4 or (m16 is not None and data[m16:m16 + 4] == b'\x00\x00\x00\x11')
    c.fh = fh
    out = c.run()
    return out, c.stats

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    out, st = convert(d)
    if len(sys.argv) > 2: open(sys.argv[2], 'wb').write(out)
    print(len(d), '->', len(out), st)
