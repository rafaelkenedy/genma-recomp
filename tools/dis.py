import sys, struct, capstone
d = open(sys.argv[1],'rb').read()
nsec, sech = struct.unpack_from('<II',d,0x11C); base=0x10000
secs=[]
for i in range(nsec):
    o = sech-base+i*0x38
    fl,va,vs,ro,rs,_ = struct.unpack_from('<IIIIII',d,o); secs.append((va,vs,ro,rs))
def rd(va,n):
    for v,vs,ro,rs in secs:
        if v<=va<v+rs: return d[ro+va-v:ro+va-v+n]
    if va < 0x1000: return d[va:va+n]
    raise SystemExit('va?')
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
va = int(sys.argv[2],16); n = int(sys.argv[3]) if len(sys.argv)>3 else 60
cnt=0
for ins in md.disasm(rd(va,4096), va):
    print(f'{ins.address:08x}  {ins.mnemonic:7} {ins.op_str}'); cnt+=1
    if cnt>=n or (ins.mnemonic in ('ret','jmp') and len(sys.argv)<=4 and cnt>3 and ins.mnemonic=='ret'): break
