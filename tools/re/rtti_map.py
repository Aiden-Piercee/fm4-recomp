#!/usr/bin/env python3
"""FM4 RTTI class map (static, read-only).

The Xbox 360 exe keeps MSVC RTTI: every vtable is preceded by a pointer to a
CompleteObjectLocator -> TypeDescriptor (".?AVName@Namespace@@"). This tool lists
every class with its vtable address(es), base classes, virtual functions and
the code that loads the vtable address (lis/addi pairs = constructors and
destructors), with the containing function taken from .pdata.

Usage:
  python3 rtti_map.py default.pe out_dir [--filter REGEX]
Writes out_dir/rtti_classes.tsv (one row per vtable), out_dir/rtti_classes.md
(readable, filtered) and out_dir/rtti_vtables.json (vtable -> name, for the
runtime trace and other tools).
"""
import json, re, struct, sys, bisect
import numpy as np

def load_pe(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
    secs = []
    o = pe + 24 + opt
    for _ in range(nsec):
        name = d[o:o + 8].rstrip(b'\0').decode('latin1')
        vs, va, rs, ra = struct.unpack_from('<IIII', d, o + 8)
        # This default.pe is a flat memory image (file offset = RVA), so the
        # raw pointers in the section table are ignored.
        vs = min(vs, len(d) - va) if va < len(d) else 0
        secs.append((name, base + va, vs, va, vs))
        o += 40
    return d, base, secs

class Image:
    def __init__(self, path):
        self.d, self.base, self.secs = load_pe(path)
    def off(self, va):
        for n, sva, size, ra, rs in self.secs:
            if sva <= va < sva + min(size, rs):
                return ra + (va - sva)
        return None
    def u32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from('>I', self.d, o)[0]
    def cstr(self, va, n=512):
        o = self.off(va)
        if o is None: return None
        e = self.d.find(b'\0', o, o + n)
        return self.d[o:e].decode('latin1') if e > 0 else None
    def sec(self, name):
        for s in self.secs:
            if s[0] == name: return s
    def is_code(self, va):
        for n, sva, size, ra, rs in self.secs:
            if n in ('.text', 'PAGELK', '_TEXT') and sva <= va < sva + size:
                return True
        return False

def demangle(n):
    # ".?AVCMainMenu@ForzaUI@@" -> "ForzaUI::CMainMenu" (templates left as is)
    s = n[4:] if n.startswith('.?A') else n
    if s.endswith('@@'): s = s[:-2]
    if '?$' in s or s.startswith('?'):
        return n
    return '::'.join(reversed([p for p in s.split('@') if p]))

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    img = Image(sys.argv[1]); out = sys.argv[2]
    flt = None
    if '--filter' in sys.argv:
        flt = re.compile(sys.argv[sys.argv.index('--filter') + 1], re.I)
    d = img.d

    # all words of the data sections as big-endian u32 with their VA
    def words_of(names):
        res = []
        for n, sva, size, ra, rs in img.secs:
            if n in names:
                a = np.frombuffer(d[ra:ra + (rs // 4) * 4], dtype='>u4')
                res.append((sva, a))
        return res
    data_words = words_of(('.rdata', '.data'))

    # 1. type descriptors
    tds = {}
    for m in re.finditer(rb'\.\?A[VU][^\0]{1,400}?\0', d):
        o = m.start()
        # map file offset back to VA
        for n, sva, size, ra, rs in img.secs:
            if ra <= o < ra + rs:
                tds[sva + (o - ra) - 8] = m.group()[:-1].decode('latin1'); break
    tdset = np.array(sorted(tds), dtype=np.uint32)

    # 2. complete object locators: signature 0, pTD at +12
    cols = {}
    for sva, a in data_words:
        hit = np.nonzero(np.isin(a, tdset))[0]
        for i in hit:
            if i >= 3 and a[i - 3] == 0:
                col = sva + (i - 3) * 4
                cols[col] = (int(a[i]), int(a[i - 2]), int(a[i + 1]) if i + 1 < len(a) else 0)
    colset = np.array(sorted(cols), dtype=np.uint32)

    # 3. vtables: word == COL address, vtable starts right after it
    vtables = []
    for sva, a in data_words:
        hit = np.nonzero(np.isin(a, colset))[0]
        for i in hit:
            col = int(a[i]); vt = sva + (i + 1) * 4
            first = int(a[i + 1]) if i + 1 < len(a) else 0
            if not img.is_code(first): continue
            td, offset, chd = cols[col]
            vtables.append((vt, col, td, offset, chd))
    vtables.sort()
    vtset = {v[0] for v in vtables}

    # base classes via class hierarchy descriptor
    def bases(chd):
        n = img.u32(chd + 8); arr = img.u32(chd + 12)
        if not n or not arr or n > 64: return []
        r = []
        for k in range(1, n):
            bcd = img.u32(arr + 4 * k)
            td = img.u32(bcd) if bcd else None
            if td in tds: r.append(demangle(tds[td]))
        return r

    # 4. code refs (lis rX,hi ; addi rY,rX,lo within 12 insns) and pdata function starts
    tn, tva, tsize, tra, trs = img.sec('.text')
    code = np.frombuffer(d[tra:tra + (trs // 4) * 4], dtype='>u4').astype(np.uint32)
    op = code >> 26
    rd = (code >> 21) & 31; ra_ = (code >> 16) & 31; imm = (code & 0xFFFF).astype(np.int64)
    lis_idx = np.nonzero((op == 15) & (ra_ == 0))[0]
    refs = {}
    vt_arr = np.array(sorted(vtset), dtype=np.int64)
    for k in range(1, 13):
        j = lis_idx + k; j = j[j < len(code)]; li = j - k
        ok = (op[j] == 14) & (ra_[j] == rd[li])
        val = ((imm[li] << 16) + np.where(imm[j] >= 0x8000, imm[j] - 0x10000, imm[j])) & 0xFFFFFFFF
        ok &= np.isin(val, vt_arr)
        for jj, v in zip(j[ok], val[ok]):
            refs.setdefault(int(v), set()).add(tva + int(jj) * 4)
    pn, pva, psize, pra, prs = img.sec('.pdata')
    pd = np.frombuffer(d[pra:pra + (prs // 8) * 8], dtype='>u4').reshape(-1, 2)
    fstarts = sorted(int(x) for x in pd[:, 0] if x)
    def func_of(addr):
        i = bisect.bisect_right(fstarts, addr) - 1
        return fstarts[i] if i >= 0 else 0

    # 5. virtual functions
    def vfuncs(vt, maxn=200):
        r = []; a = vt
        while len(r) < maxn:
            v = img.u32(a)
            if v is None or not img.is_code(v) or (a != vt and a in vtset): break
            r.append(v); a += 4
        return r

    rows = []
    for vt, col, td, offset, chd in vtables:
        name = demangle(tds[td])
        fns = vfuncs(vt)
        rf = sorted(refs.get(vt, ()))
        rows.append(dict(vtable=vt, name=name, mangled=tds[td], offset=offset, bases=bases(chd),
                         nvirt=len(fns), virt=fns, refs=rf, ref_funcs=sorted({func_of(x) for x in rf})))

    import os
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'rtti_classes.tsv'), 'w') as f:
        f.write('vtable\tclass\tsubobject_offset\tnum_virtuals\tbases\tvtable_loaded_in_functions\n')
        for r in rows:
            f.write('%08X\t%s\t%d\t%d\t%s\t%s\n' % (r['vtable'], r['name'], r['offset'], r['nvirt'],
                    ', '.join(r['bases']), ' '.join('%08X' % x for x in r['ref_funcs'])))
    with open(os.path.join(out, 'rtti_vtables.json'), 'w') as f:
        json.dump({'%08X' % r['vtable']: r['name'] for r in rows}, f, indent=0)
    with open(os.path.join(out, 'rtti_classes.md'), 'w') as f:
        f.write('# FM4 RTTI classes%s\n\n' % (' (filter: %s)' % flt.pattern if flt else ''))
        f.write('Generated by rtti_map.py from default.pe. "loaded in" = functions that load the vtable '
                'address (usually the constructor and destructor). Slot n = vtable + 4*n.\n\n')
        for r in rows:
            if flt and not (flt.search(r['name']) or any(flt.search(b) for b in r['bases'])): continue
            f.write('## %s  (vtable %08X, subobject +%d)\n' % (r['name'], r['vtable'], r['offset']))
            if r['bases']: f.write('- bases: %s\n' % ', '.join(r['bases']))
            f.write('- loaded in: %s\n' % (' '.join('sub_%08X' % x for x in r['ref_funcs']) or '-'))
            f.write('- virtuals (%d): %s\n\n' % (r['nvirt'], ' '.join(
                '%d:%08X' % (i, v) for i, v in enumerate(r['virt']))))
    print('type descriptors %d, locators %d, vtables %d, vtables with code refs %d' % (
        len(tds), len(cols), len(rows), sum(1 for r in rows if r['refs'])))
    return 0

if __name__ == '__main__':
    sys.exit(main())
