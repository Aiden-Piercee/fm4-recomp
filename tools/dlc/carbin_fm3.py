"""carbin_fm3.py - convert Forza Motorsport 3 .carbin models (car root v1 / wheel root v2, Model16 v13, SubModel v2,
Mesh v2, 40/44-byte vertices) to the FM4 layout.  Vertex re-encoding lives in fm3vert.py.
Templates (gauge tail etc.) are taken from an FM4 car: call init_templates(fm4_media_dir) first.
  convert3(bytes) -> (fm4 bytes, stats)"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import carbin, fm3vert
from carbin import *
import make_fh_base_pack as _M

GAUGE_TAIL = THIRD_U32 = CAR80 = None
def init_templates(fm4_media):
    global GAUGE_TAIL, THIRD_U32, CAR80
    cz = _M.ci_path(fm4_media, 'cars', 'CHE_CobaltSS_10.zip')
    tb = _M.zread(cz, 'che_cobaltss_10_caliperlf_lod0.carbin'); tbody = _M.zread(cz, 'che_cobaltss_10_lod0.carbin')
    GAUGE_TAIL = tb[0x120+4*36:0x120+15*36]; THIRD_U32 = tb[0x120+15*36+8:0x120+15*36+12]
    CAR80 = tbody[4+256:4+256+80]

def tris_of(ib,n):
    t=[];strip=[]
    def flush():
        for i in range(len(strip)-2):
            a,b,c=strip[i:i+3]
            if len({a,b,c})==3: t.append((a,b,c) if i%2==0 else (b,a,c))
    for v in ib:
        if v>=n: flush(); strip.clear()
        else: strip.append(int(v))
    flush(); return np.array(t) if t else np.zeros((0, 3), np.int64)

class C3(carbin.Conv):
    m13=False
    def run(s):
        first,second = struct.unpack_from('>II', s.r.d, 0)[:2]
        if first==1 and second not in (13,14,15,16,17):
            # FM3 car root v1
            s.r.u32(); s.w.u32(2); s.copy(256); s.w.raw(CAR80); s.model16(); s.model4()
            if s.r.o != len(s.r.d): raise Fmt('trailing data: stopped at %x of %x'%(s.r.o,len(s.r.d)))
            return bytes(s.w.b)
        if first==2 and second==0 and struct.unpack_from('>I',s.r.d,24)[0]==13:
            # FM3 wheel root (v2, 24 byte header) -> FM4 wheel root v3 (+12 floats of part extents, scaled from Spark template)
            s.r.raw(24); s.w.u32(3); s.w.raw(bytes(68)); s.hdrs=[]
            s.r.o=24-0; s.model16(); s.model4()
            if s.r.o != len(s.r.d): raise Fmt('trailing data: stopped at %x of %x'%(s.r.o,len(s.r.d)))
            h=s.first_hdr; hw=abs(h[6]-h[3])/2; hr=max(abs(h[4]),abs(h[5]),abs(h[7]),abs(h[8]))
            f=[-0.08250004,-0.07487817,0.08537607,0.0831044,-0.08250004,-0.08244002,-0.07926551,0.0831044,0.32331172,0.20786236,0.32325557,0.29009217]
            sw=hw/0.0887; sr=hr/0.29
            fl=[x*(sw if i<8 else sr) for i,x in enumerate(f)]
            s.w.b[4+20:4+68]=struct.pack('>12f',*fl)
            s.w.b[4:24]=bytes(20)
            return bytes(s.w.b)
        return super().run()
    def model16(s):
        v = s.ver((13,16,17), out=16, what='Model16')
        s.m13 = v==13
        sv=s.r.u32(); s.w.u32(16 if s.m13 else sv)
        s.ver((1,), what='Emitters')
        for _ in range(s.cu32()):
            s.ver((2,), what='Emitter'); s.copy(8 + 128)
        s.ver((3,), what='DriverControls'); s.copy(256)
        s.copy(8)
        if s.m13:
            s.copy(4*36); s.w.raw(GAUGE_TAIL)
            s.copy(8); s.w.raw(THIRD_U32)
            s.w.u32(1); s.w.u32(0)           # empty BoneMappingTable
        else:
            s.copy(15*36); s.copy(12); s.strlist('BoneMappingTable')
MORPH=bytes.fromhex('0000000100000000020000000000000000000000020000000000000000')
def submodel(s):
    v = s.ver((2, 3, 4, 5), out=5, what='SubModel5')
    if v==3: raise Fmt('SubModel v3 (triggers) unsupported')
    s.copy(56)
    s.ver((1,), what='GlassParticles')
    s.counted('Glass.Vertices', 16)
    s.counted('Glass.Indices', 2)
    s.copy(1)
    if v >= 3: s.copy(4)
    else: s.w.u32(0); s.note('submodel_v%d'%v)
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
    else:
        s.w.raw(MORPH)
C3.submodel=submodel

def _mesh(s):
    v = s.ver((1, 2, 3), out=3, what='Mesh3')
    if v >= 2: s.copy(1)
    else: s.w.u8(0)
    if v >= 3: s.copy(64); s.cur_blocks.append(None)
    else:
        s.cur_blocks.append(len(s.w.b)); s.w.raw(bytes(64))
    v2 = s.ver((1, 2), what='Mesh2')
    s.cstr(); s.copy(12)
    s.ver((1,), what='UnpackingData'); s.copy(48)
    o = s.r.o
    s.ib()
    d = s.r.d
    iv = struct.unpack_from('>I', d, o)[0]; p = o + 4 + (4 if iv >= 4 else 0)
    cnt, fmt = struct.unpack_from('>II', d, p); p += 8
    s.cur_ibs.append(np.frombuffer(d[p:p + cnt * fmt], '>u2' if fmt == 2 else '>u4').astype(np.int64))
    if v2 >= 2: s.copy(4)
C3.mesh=_mesh
def _vb(s, what):
    v = s.ver((2, 3), out=2, what=what)
    cnt = s.r.u32(); size = s.r.u32()
    if size in (16, 40, 44) and v == 2:
        raw = s.r.raw(cnt * size)
        tris = None
        if size in (16, 44):
            tris = np.concatenate([tris_of(i, cnt) for i in s.cur_ibs]) if s.cur_ibs else None
        out, blocks, info = fm3vert.convert_vb(raw, s.cur_hdr, s.cur_ibs, stride=size, tris=tris)
        s.w.u32(cnt); s.w.u32(32); s.w.raw(out)
        for off, b in zip(s.cur_blocks, blocks):
            if off is not None: s.w.b[off:off + 64] = b.ljust(64, b'\0')
        s.note('vb40to32')
        s.cur_blocks = []; s.cur_ibs = []
        return
    s.w.u32(cnt); s.w.u32(size); s.w.raw(s.r.raw(cnt * size))
C3.vb=_vb
_old_sub = submodel
def submodel2(s):
    s.cur_blocks = []; s.cur_ibs = []
    s.cur_hdr = struct.unpack_from('>14f', s.r.d, s.r.o + 4)
    if not hasattr(s,'first_hdr'): s.first_hdr=s.cur_hdr
    _old_sub(s)
C3.submodel = submodel2

def convert3(data):
    c = C3(data); c.fh = False
    out = c.run()
    return out, c.stats

if __name__ == '__main__':
    init_templates(sys.argv[1])
    d = open(sys.argv[2], 'rb').read()
    try:
        out, st = convert3(d); print('OK', len(d), len(out), st)
        if len(sys.argv) > 3: open(sys.argv[3], 'wb').write(out)
    except Exception as e:
        print('FAIL', e)
