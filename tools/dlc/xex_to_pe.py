# xex_to_pe.py - decrypt/decompress an Xbox 360 XEX2 to its PE image (for looking up
# guest addresses/strings from crash logs: offset = addr - 0x82000000).
#   FZLZX lib needed only for LZX-compressed XEXs.  usage: xex_to_pe.py default.xex out.pe
import struct,sys,ctypes,os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
b=open(sys.argv[1],'rb').read()
magic,flags,pe_off,_,sec_off,nopt=struct.unpack('>4sIIIII',b[:24])
opts={}
for i in range(nopt):
    k,v=struct.unpack('>II',b[24+i*8:32+i*8]); opts[k]=v
ffi=opts[0x3FF]
info_size,enc,comp=struct.unpack('>IHH',b[ffi:ffi+8])
img_size=struct.unpack('>I',b[sec_off+4:sec_off+8])[0]
load=struct.unpack('>I',b[sec_off+0x110:sec_off+0x114])[0]
print('enc',enc,'comp',comp,'image',hex(img_size),'load',hex(load))
aes=b[sec_off+0x150:sec_off+0x160]
def dec_ecb(k,d): c=Cipher(algorithms.AES(k),modes.ECB()).decryptor(); return c.update(d)+c.finalize()
def dec_cbc(k,d): c=Cipher(algorithms.AES(k),modes.CBC(b'\0'*16)).decryptor(); return c.update(d)+c.finalize()
data=b[pe_off:]
data=data[:len(data)//16*16]
for name,key in (('retail',bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')),('devkit',b'\0'*16)):
    sk=dec_ecb(key,aes); d=dec_cbc(sk,data) if enc==1 else data
    if comp==2:
        win=struct.unpack('>I',b[ffi+8:ffi+12])[0]
        bsz=struct.unpack('>I',b[ffi+12:ffi+16])[0]
        p=0; chunks=bytearray()
        while bsz:
            blk=d[p:p+bsz]; nxt=struct.unpack('>I',blk[:4])[0]; q=24
            while q+2<=len(blk):
                cs=struct.unpack('>H',blk[q:q+2])[0]
                if cs==0: break
                chunks+=blk[q:q+2+cs]; q+=2+cs
            p+=bsz; bsz=nxt
        lib=ctypes.CDLL(os.path.expanduser('~/fzbuild/libfzlzx.so'))
        out=ctypes.create_string_buffer(img_size)
        wb=win.bit_length()-1
        rc=lib.fz_lzx_decompress(bytes(chunks),len(chunks),out,img_size,wb)
        img=out.raw; print(name,'lzx rc',rc,'win',hex(win))
    elif comp==1:
        nb=(info_size-8)//8; img=bytearray(); p=0
        for i in range(nb):
            ds,zs=struct.unpack('>II',b[ffi+8+i*8:ffi+16+i*8]); img+=d[p:p+ds]+b'\0'*zs; p+=ds
        img=bytes(img)
    else: img=d
    if img[:2]==b'MZ':
        open(sys.argv[2],'wb').write(img); print('wrote',name,len(img)); break
    print(name,'not MZ',img[:4])
