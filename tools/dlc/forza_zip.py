#!/usr/bin/env python3
"""forza_zip.py - list / extract Forza (FM2/FM3/FM4/FH) zips whose entries use
compression method 21 (Xbox XMemCompress LZX). Methods 0 and 8 also handled.

  python3 forza_zip.py list    <zip>
  python3 forza_zip.py extract <zip> <outdir> [--match substring]

Needs libfzlzx.so (build: see build_fzlzx.sh) next to this script or in $FZLZX.
"""
import ctypes, os, struct, sys, zipfile, zlib

_lib = None
def _load():
    global _lib
    if _lib is None:
        here = os.path.dirname(os.path.abspath(__file__))
        cands = [os.environ.get('FZLZX', ''), os.path.join(here, 'libfzlzx.so'),
                 os.path.expanduser('~/fzbuild/libfzlzx.so')]
        for c in cands:
            if c and os.path.exists(c):
                _lib = ctypes.CDLL(c); break
        if _lib is None:
            raise SystemExit('libfzlzx.so not found - run build_fzlzx.sh')
        _lib.fz_lzx_decompress.argtypes = [ctypes.c_char_p, ctypes.c_long,
                                           ctypes.c_char_p, ctypes.c_long, ctypes.c_int]
    return _lib

def lzx(data, out_len, window_bits=17):
    lib = _load()
    out = ctypes.create_string_buffer(out_len)
    rc = lib.fz_lzx_decompress(data, len(data), out, out_len, window_bits)
    if rc != 0:
        raise ValueError('lzx rc=%d' % rc)
    return out.raw

def raw_entry(f, info):
    f.seek(info.header_offset)
    h = f.read(30)
    nl, el = struct.unpack('<HH', h[26:30])
    f.seek(info.header_offset + 30 + nl + el)
    return f.read(info.compress_size)

def read_entry(f, info):
    d = raw_entry(f, info)
    m = info.compress_type
    if m == 0:
        return d
    if m == 8:
        return zlib.decompress(d, -15)
    if m == 21:
        last = None
        for wb in (17, 16, 15, 18, 19, 20, 21):
            try:
                return lzx(d, info.file_size, wb)
            except ValueError as e:
                last = e
        raise last
    raise ValueError('unsupported method %d' % m)

def main(a):
    if len(a) < 2:
        print(__doc__); return 1
    cmd, zp = a[0], a[1]
    zf = zipfile.ZipFile(zp)
    if cmd == 'list':
        for i in zf.infolist():
            print('%3d %10d %s' % (i.compress_type, i.file_size, i.filename))
        return 0
    out = a[2]; match = None
    if '--match' in a:
        match = a[a.index('--match') + 1].lower()
    bad = 0
    with open(zp, 'rb') as f:
        for i in zf.infolist():
            if i.is_dir() or (match and match not in i.filename.lower()):
                continue
            dest = os.path.join(out, *i.filename.replace('\\', '/').split('/'))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            try:
                data = read_entry(f, i)
            except Exception as e:
                print('FAIL', i.filename, e); bad += 1; continue
            with open(dest, 'wb') as o:
                o.write(data)
    print('done, failures:', bad)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
