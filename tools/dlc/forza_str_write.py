#!/usr/bin/env python3
"""forza_str_write.py - add entries to a Forza 'LSB2' string table (*.str).

Keeps every existing byte (text pool, key pool, change-log section 3) and
appends new strings, so `rebuild(parse(b), [])` == b.  See forza_str.py for
the layout.  Each new entry: (key, hash16, text).
"""
import struct

def parse(b):
    if b[:4] != b'LSB2':
        raise ValueError('not LSB2')
    hdr = b[:0xC]
    w = struct.unpack('>7I', b[0xC:0x28])
    _, _, s1, s2, s3, s4, _ = w
    sz1, n1 = struct.unpack('>II', b[s1:s1 + 8])
    idx1 = [struct.unpack('>HI', b[s1 + 8 + i * 6:s1 + 14 + i * 6]) for i in range(n1)]
    sent_h, total_chars = struct.unpack('>HI', b[s1 + 8 + n1 * 6:s1 + 14 + n1 * 6])
    p1 = s1 + 8 + (n1 + 1) * 6
    pool1 = b[p1:p1 + total_chars * 2]
    sz2, n2 = struct.unpack('>II', b[s2:s2 + 8])
    idx2 = [struct.unpack('>HHI', b[s2 + 8 + i * 8:s2 + 16 + i * 8]) for i in range(n2)]
    pool2 = b[s2 + 8 + n2 * 8:s2 + sz2]
    sec3 = b[s3:s4] if s3 else b''  # section 3 (change log) is optional; offset 0 = absent
    sz4, n4 = struct.unpack('>II', b[s4:s4 + 8])
    rows = list(struct.unpack('>%dH' % n4, b[s4 + 8:s4 + 8 + n4 * 2]))
    return dict(hdr=hdr, w0=w[0], w1=w[1], idx1=idx1, sent=sent_h, pool1=pool1,
                pad1=sz1 - (8 + (n1 + 1) * 6 + len(pool1)), idx2=idx2, pool2=pool2,
                sec3=sec3, rows=rows)

def hashes(t):
    return {h for h, _ in t['idx1']}

def rebuild(t, new):
    idx1 = list(t['idx1']); pool1 = bytearray(t['pool1'])
    idx2 = list(t['idx2']); pool2 = bytearray(t['pool2'])
    rows = list(t['rows'])
    used = hashes(t)
    for key, h, text in new:
        if h in used or h == 0xFFFF:
            raise ValueError('hash %04x already used' % h)
        used.add(h)
        idx1.append((h, len(pool1) // 2))
        pool1 += text.encode('utf-16-be') + b'\0\0'
        idx2.append((h, len(rows), len(pool2)))
        pool2 += key.encode('ascii') + b'\0'
        rows.append(h)
    idx1.sort(); idx2.sort()
    s1 = bytearray(struct.pack('>II', 0, len(idx1)))
    for h, o in idx1: s1 += struct.pack('>HI', h, o)
    s1 += struct.pack('>HI', t['sent'], len(pool1) // 2) + pool1
    pad = t['pad1'] if not new else (-len(s1)) % 4
    s1 += b'\0' * pad
    s1[0:4] = struct.pack('>I', len(s1))
    s2 = bytearray(struct.pack('>II', 0, len(idx2)))
    for h, r, o in idx2: s2 += struct.pack('>HHI', h, r, o)
    s2 += pool2; s2[0:4] = struct.pack('>I', len(s2))
    s4 = struct.pack('>II', 8 + 2 * len(rows), len(rows)) + struct.pack('>%dH' % len(rows), *rows)
    o1 = 0x24; o2 = o1 + len(s1); o4 = o2 + len(s2) + len(t['sec3'])
    o3 = o2 + len(s2) if t['sec3'] else 0
    head = t['hdr'] + struct.pack('>6I', t['w0'], t['w1'], o1, o2, o3, o4)  # sec1 starts at 0x24 with its own size word
    return bytes(head + s1 + s2 + t['sec3'] + s4)
