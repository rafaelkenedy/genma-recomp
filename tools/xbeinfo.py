import sys, struct
d = open(sys.argv[1],'rb').read()
assert d[:4]==b'XBEH'
base = struct.unpack_from('<I',d,0x104)[0]
def va2off(v): return v-base
ep = struct.unpack_from('<I',d,0x128)[0]^0xA8FC57AB
print('base',hex(base),'entry(retail)',hex(ep))
nsec, sech = struct.unpack_from('<II',d,0x11C)
for i in range(nsec):
    o = va2off(sech)+i*0x38
    fl,va,vs,ro,rs,nameva = struct.unpack_from('<IIIIII',d,o)
    n = d[va2off(nameva):va2off(nameva)+16].split(b'\0')[0].decode()
    print(f'  sec {n:10} va {va:08x} vsize {vs:08x} raw {ro:08x} rsize {rs:08x} flags {fl:x}')
nlib, libva = struct.unpack_from('<II',d,0x160)
for i in range(nlib):
    o=va2off(libva)+i*16
    name=d[o:o+8].split(b'\0')[0].decode(); maj,mn,bld,fl=struct.unpack_from('<HHHH',d,o+8)
    print(f'  lib {name:9} {maj}.{mn}.{bld}')
cert = struct.unpack_from('<I',d,0x118)[0]; co=va2off(cert)
tid = struct.unpack_from('<I',d,co+8)[0]
title = d[co+12:co+12+80].decode('utf-16le').split('\0')[0]
print('title id', hex(tid), title)
