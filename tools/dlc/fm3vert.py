"""FM3 car vertex (40 B) -> FM4 car vertex (32 B).  Derived from Enzo (FM3 vs FM4) ground truth.
FM3 u16 cols: 0-2 half unit direction; 3 half radius (m, from submodel centre); 4,5 half UV1; 6,7 half UV2;
              bytes16-19 u32 (z10|y11|x11 snorm) frame axis0 = normal; bytes20-35 8 x s16 SH-ish; bytes36-39 u32 axis2.
FM4 u16 cols: 0-2 s16 direction; 3 radius/diag*65535; 4,5 UV1 normalised by per-mesh (min,range) block; 6,7 UV2 likewise;
              8-11 s16 quaternion(x,y,z,w) of frame [axis0 axis1 axis2], axis1 = axis2 x axis0; 12-15 = FM3 cols 10-13 (approx).
Per-mesh block (64 B after the mesh u8): floats umin,urange,vmin,vrange, u2min,u2range,v2min,v2range, 8 zeros."""
import numpy as np

def _sx(v, b): return np.where(v >= (1 << (b - 1)), v - (1 << b), v)

def decode_word(w):
    z = _sx((w >> 22) & 0x3FF, 10) / 511.0
    y = _sx((w >> 11) & 0x7FF, 11) / 1023.0
    x = _sx(w & 0x7FF, 11) / 1023.0
    v = np.stack([x, y, z], 1)
    return v / np.maximum(np.linalg.norm(v, axis=1, keepdims=True), 1e-9)

def mat_to_quat(R):
    """R: (n,3,3) rotation matrices -> (n,4) x,y,z,w, largest component positive (Shepperd)."""
    n = len(R); q = np.zeros((n, 4))
    t = R[:, 0, 0] + R[:, 1, 1] + R[:, 2, 2]
    cands = np.stack([t, R[:, 0, 0], R[:, 1, 1], R[:, 2, 2]], 1)
    k = cands.argmax(1)
    for i in range(n):
        m = R[i]
        if k[i] == 0:
            s = np.sqrt(max(1 + t[i], 1e-12)) * 2; w = s / 4
            x = (m[2, 1] - m[1, 2]) / s; y = (m[0, 2] - m[2, 0]) / s; z = (m[1, 0] - m[0, 1]) / s
        elif k[i] == 1:
            s = np.sqrt(max(1 + m[0, 0] - m[1, 1] - m[2, 2], 1e-12)) * 2; x = s / 4
            w = (m[2, 1] - m[1, 2]) / s; y = (m[0, 1] + m[1, 0]) / s; z = (m[0, 2] + m[2, 0]) / s
        elif k[i] == 2:
            s = np.sqrt(max(1 + m[1, 1] - m[0, 0] - m[2, 2], 1e-12)) * 2; y = s / 4
            w = (m[0, 2] - m[2, 0]) / s; x = (m[0, 1] + m[1, 0]) / s; z = (m[1, 2] + m[2, 1]) / s
        else:
            s = np.sqrt(max(1 + m[2, 2] - m[0, 0] - m[1, 1], 1e-12)) * 2; z = s / 4
            w = (m[1, 0] - m[0, 1]) / s; x = (m[0, 2] + m[2, 0]) / s; y = (m[1, 2] + m[2, 1]) / s
        q[i] = (x, y, z, w)
    q /= np.linalg.norm(q, axis=1, keepdims=True)
    return q

def convert_vb(raw, hdr, mesh_vsets, stride=40, tris=None):
    """raw: bytes n*40; hdr: submodel floats (center3, lo3, hi3 ...); mesh_vsets: list of int arrays (vertices per mesh).
    Returns (fm4 bytes n*32, [64-byte block per mesh], info)."""
    A = np.frombuffer(raw, np.uint8).reshape(-1, stride); n = len(A); k4 = stride // 4
    H = np.ascontiguousarray(A).view('>f2').reshape(n, stride // 2).astype(np.float64)
    U = np.ascontiguousarray(A).view('>u2').reshape(n, stride // 2).astype(np.int64)
    W = np.ascontiguousarray(A).view('>u4').reshape(n, k4).astype(np.int64)
    lo = np.array(hdr[3:6]); hi = np.array(hdr[6:9]); D = float(np.linalg.norm(hi - lo))
    out = np.zeros((n, 16), np.uint16)
    d = np.nan_to_num(H[:, 0:3])
    out[:, 0:3] = np.clip(np.round(d * 32767), -32768, 32767).astype(np.int16).view(np.uint16)
    r = np.nan_to_num(H[:, 3])
    out[:, 3] = np.clip(np.round(r / D * 65535), 0, 65535).astype(np.uint16) if D > 0 else 0
    # UV sets quantised per mesh block
    blocks = []; assigned = np.zeros(n, bool)
    uvs = [np.nan_to_num(H[:, 4]), np.nan_to_num(H[:, 5]), np.nan_to_num(H[:, 6]), np.nan_to_num(H[:, 7])]
    if stride in (16, 44): uvs[2] = np.zeros(n); uvs[3] = np.zeros(n)
    any_uv2 = bool(np.any(uvs[2]) or np.any(uvs[3]))
    vsets = [np.unique(v[v < n]) for v in mesh_vsets] or [np.arange(n)]
    for vs in vsets:
        blk = [0.0] * 16
        if len(vs):
            for set_i, (a, b) in enumerate(((0, 1), (2, 3))):
                if set_i == 1 and not any_uv2: continue
                for comp, col in enumerate((a, b)):
                    c = uvs[col]; sub = c[vs]; mn = float(sub.min()); rg = float(sub.max() - sub.min())
                    blk[set_i * 4 + comp * 2] = mn; blk[set_i * 4 + comp * 2 + 1] = rg
                    new = ((c[vs] - mn) / rg * 65535) if rg > 0 else np.zeros(len(vs))
                    m = ~assigned[vs]
                    out[vs[m], 4 + set_i * 2 + comp] = np.clip(np.round(new[m]), 0, 65535).astype(np.uint16)
            assigned[vs] = True
        blocks.append(np.array(blk, '>f4').tobytes())
    rest = ~assigned
    if rest.any():     # vertices no mesh references: use the first block's range
        pass
    # tangent frame
    if stride in (16, 44):  # wheel/tyre/brake vertex (16-byte brake = prefix of the 44-byte wheel one): normal in bytes 12-15, frame from UV tangents of the triangles
        a0 = decode_word(W[:, 3])
        pos = np.nan_to_num(H[:, 0:3]) * np.nan_to_num(H[:, 3])[:, None]; uv = np.nan_to_num(H[:, 4:6])
        tv = np.zeros((n, 3))
        if tris is not None and len(tris):
            a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
            e1 = pos[b] - pos[a]; e2 = pos[c] - pos[a]; d1 = uv[b] - uv[a]; d2 = uv[c] - uv[a]
            det = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]; det = np.where(np.abs(det) < 1e-12, 1e-12, det)
            dv = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / det[:, None]
            for kk in (a, b, c): np.add.at(tv, kk, dv)
        tv = tv - (tv * a0).sum(1, keepdims=True) * a0
        nv = np.linalg.norm(tv, axis=1, keepdims=True)
        fb = np.cross(a0, np.array([1.0, 0, 0])); fb[np.linalg.norm(fb, axis=1) < 1e-6] = (0, 1, 0)
        tv = np.where(nv > 1e-9, tv / np.maximum(nv, 1e-9), fb / np.maximum(np.linalg.norm(fb, axis=1, keepdims=True), 1e-9))
        a2 = -tv; a1 = np.cross(a2, a0)
        out[:, 12:16] = U[:, 8:12].astype(np.uint16) if stride == 44 else 0
    else:
        a0 = decode_word(W[:, 4]); a2 = decode_word(W[:, 9]); a1 = np.cross(a2, a0)
        out[:, 12:16] = U[:, 10:14].astype(np.uint16)
    R = np.stack([a0, a1, a2], 2)
    q = mat_to_quat(R)
    out[:, 8:12] = np.clip(np.round(q * 32767), -32768, 32767).astype(np.int16).view(np.uint16)
    return out.astype('>u2').tobytes(), blocks, dict(D=D, uv2=any_uv2, unassigned=int(rest.sum()))
