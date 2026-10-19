import sys, struct, capstone
d = open(sys.argv[1],'rb').read()
nsec, sech = struct.unpack_from('<II',d,0x11C); base=0x10000
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.skipdata = True
want = sys.argv[2].split(',')
for i in range(nsec):
    o = sech-base+i*0x38
    fl,va,vs,ro,rs,nva = struct.unpack_from('<IIIIII',d,o)
    n = d[nva-base:nva-base+16].split(b'\0')[0].decode()
    if n not in want: continue
    for ins in md.disasm(d[ro:ro+rs], va):
        print(f'{ins.address:08x}  {ins.mnemonic:7} {ins.op_str}')
