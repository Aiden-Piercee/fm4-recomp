#!/usr/bin/env python3
"""fh3_grub.py - reader for Forza Horizon 3 (PC / Xbox One) 'burG' bundles.

Layout (little-endian, verified on the Monaro skeleton.modelbin):
  0x00 'burG' magic (bytes 62 75 72 47)
  0x04 u8 major, u8 minor   (1.1 seen)
  0x10 u32 blob count       (minor >= 1; older files use a u16)
  0x14 blob headers, 24 bytes each:
       tag[4] (stored reversed: 'lekS' = 'Skel'), u16 version, u16 metadata_count,
       u32 metadata_offset (absolute), u32 data_offset (absolute), u32 data_size, u32 pad
  metadata entries, 8 bytes each: tag[4] (reversed), u16 (low 4 bits version, rest size), u16 offset
  Blob data is the byte range [data_offset, data_offset + data_size) of the file.

FH3 zip entry names (e.g. media\\cars\\<CAR>.zip) are obfuscated with a fixed one-to-one character
substitution (FH3_NAME, enc -> plain; '/' and '.' unchanged). See claude/fh3-zip-name-cipher.md.

Usage:  python3 fh3_grub.py file1.modelbin [file2 ...]      (summary)
        python3 fh3_grub.py file --dump-tags                 (every blob + metadata)
        python3 fh3_grub.py --zip car.zip [filter]           (list decoded entry names, optional substring filter)
        python3 fh3_grub.py --zip car.zip --extract <plain name> <out file>
Importable: from fh3_grub import read_bundle, fh3_name, fh3_encode, zip_names, read_zip_entry
"""
import struct, sys, collections, zipfile

# FH3 zip entry-name cipher (enc -> plain), user-supplied 2026-10-14, verified on BMW_E46M3_05.zip
FH3_NAME = dict(zip("l`^6qv{@$7sbg8huf4~1='m]!,y_[0wk(2j};+",
                    "abcdefghijklmnopqrstuvwxyz_-0123456789"))
FH3_NAME_ENC = {v: k for k, v in FH3_NAME.items()}

def fh3_name(s):
    """Decode an obfuscated FH3 zip entry name."""
    return ''.join(FH3_NAME.get(c, c) for c in s)

def fh3_encode(s):
    """Encode a plain (lower-case) name into FH3's zip entry-name form."""
    return ''.join(FH3_NAME_ENC.get(c, c) for c in s.lower())

def zip_names(zip_path):
    """{plain name: ZipInfo} for an FH3 zip."""
    z = zipfile.ZipFile(zip_path)
    return {fh3_name(i.filename): i for i in z.infolist()}

def read_zip_entry(zip_path, plain_name):
    """Bytes of one FH3 zip entry, looked up by its decoded name."""
    names = zip_names(zip_path)
    if plain_name not in names:
        raise KeyError(plain_name)
    return zipfile.ZipFile(zip_path).read(names[plain_name])

def _tag(b):
    return b[:4][::-1].decode('latin-1')

def read_bundle(path_or_bytes):
    d = open(path_or_bytes, 'rb').read() if isinstance(path_or_bytes, str) else path_or_bytes
    if d[:4] != b'burG':
        raise ValueError('not a burG bundle')
    major, minor = d[4], d[5]
    count = struct.unpack_from('<I', d, 0x10)[0] if minor >= 1 else struct.unpack_from('<H', d, 0x10)[0]
    blobs = []
    off = 0x14
    for _ in range(count):
        tag = _tag(d[off:off+4])
        ver, mcount, moff, doff, dsize = struct.unpack_from('<HHIII', d, off + 4)
        meta = []
        for i in range(mcount):
            t, a, o = struct.unpack_from('<4sHH', d, moff + 8 * i)
            meta.append(dict(tag=_tag(t), version=a & 0xF, size=a >> 4, offset=o))
        blobs.append(dict(tag=tag, version=ver, data_offset=doff, data_size=dsize, meta=meta,
                          data=d[doff:doff + dsize]))
        off += 24
    return dict(major=major, minor=minor, size=len(d), blobs=blobs)

def main(argv):
    if argv[:1] == ['--zip']:
        zp = argv[1]
        if len(argv) >= 5 and argv[2] == '--extract':
            open(argv[4], 'wb').write(read_zip_entry(zp, argv[3])); print('wrote', argv[4])
            return
        flt = argv[2] if len(argv) > 2 else ''
        for n, i in sorted(zip_names(zp).items()):
            if flt in n: print('%10d  %s' % (i.file_size, n))
        return
    dump = '--dump-tags' in argv
    for p in [a for a in argv if not a.startswith('--')]:
        b = read_bundle(p)
        cnt = collections.Counter(x['tag'] for x in b['blobs'])
        print('%s  v%d.%d  %d bytes  %d blobs' % (p, b['major'], b['minor'], b['size'], len(b['blobs'])))
        print('   tags:', dict(cnt))
        if dump:
            for x in b['blobs']:
                print('   %-4s v%d data@0x%x size=%d meta=%s' % (x['tag'], x['version'], x['data_offset'],
                      x['data_size'], [(m['tag'], m['size'], m['offset']) for m in x['meta']]))

if __name__ == '__main__':
    main(sys.argv[1:])
