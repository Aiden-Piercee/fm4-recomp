#!/usr/bin/env python3
"""forza_str.py - read Forza FM3/FM4 'LSB2' string tables (*.str).

  python3 forza_str.py dump <file.str>            -> key<TAB>hash<TAB>text
  python3 forza_str.py diff <old.str> <new.str>   -> keys only in new

Layout as far as decoded (all big-endian after the 12-byte 'LSB2',1,2 prefix):
  0x0C  u32 x7 : [0x24, 4, sec1_off, sec2_off, sec3_off, sec4_off, sec1_size]
  sec1 : u32 size, u32 n, n x (u16 hash16, u32 char_offset), sentinel
         (0xFFFF, u32 pool_chars), then UTF-16BE NUL-terminated text pool
  sec2 : u32 size, u32 n, n x (u16 hash16, u16 row, u32 byte_offset), then
         ASCII NUL-terminated key pool ("IDS_<Column>_<RowId>")
  sec3 : u32 size, u32 n, n x 8 bytes  (purpose not decoded yet)
  sec4 : u32 size, u32 n, n x u16 hash16 in row order
The DB stores '_&<u32>' references; the low 16 bits are hash16 here and the
high 16 bits are constant per table (e.g. 0xCD49 for Data_Car). FM3 and FM4
produce identical values for the same key, so FM3 references carry over.
"""
import struct, sys

def read(path):
    b = open(path, 'rb').read()
    if b[:4] != b'LSB2':
        raise ValueError('not LSB2')
    _, _, s1, s2, s3, s4, _ = struct.unpack('>7I', b[0xC:0x28])
    _, n = struct.unpack('>II', b[s1:s1 + 8])
    idx = [struct.unpack('>HI', b[s1 + 8 + i * 6:s1 + 14 + i * 6]) for i in range(n)]
    pool = s1 + 8 + (n + 1) * 6
    text = {}
    for h, off in idx:
        st = pool + off * 2; en = st
        while b[en:en + 2] != b'\0\0': en += 2
        text[h] = b[st:en].decode('utf-16-be', 'replace')
    _, n2 = struct.unpack('>II', b[s2:s2 + 8])
    kpool = s2 + 8 + n2 * 8
    keys = []
    for i in range(n2):
        h, row, off = struct.unpack('>HHI', b[s2 + 8 + i * 8:s2 + 16 + i * 8])
        st = kpool + off; en = b.index(b'\0', st)
        keys.append((b[st:en].decode('ascii', 'replace'), h, row))
    keys.sort(key=lambda k: k[2])
    return [(k, h, text.get(h, '')) for k, h, _ in keys]

def main(a):
    if len(a) < 2: print(__doc__); return 1
    if a[0] == 'dump':
        for k, h, t in read(a[1]): print('%s\t%04x\t%s' % (k, h, t))
    elif a[0] == 'diff':
        old = {k for k, _, _ in read(a[1])}
        for k, h, t in read(a[2]):
            if k not in old: print('%s\t%04x\t%s' % (k, h, t))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
