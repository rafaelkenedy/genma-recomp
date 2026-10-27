"""Replay original USA state handlers; stub only unrelated helper calls.

Run from the repository root with assets/default.xbe and Unicorn installed:
    python -P tools/test_modern_controls_native.py
The optional out/diagnostic-python directory holds local diagnostic packages.
No original executable bytes are embedded in this test.
"""
import struct, sys
from pathlib import Path
sys.path.insert(0, str(Path('out/diagnostic-python').resolve()))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX
data=Path('assets/default.xbe').read_bytes()
n,table=struct.unpack_from('<II',data,0x11c)
sections=[struct.unpack_from('<IIIIII',data,table-0x10000+i*56)for i in range(n)]
player,actions,stack,stop=0x547440,0x543050,0xd0001000,0x600000
def run(entry,state,held,window):
    uc=Uc(UC_ARCH_X86,UC_MODE_32)
    uc.mem_map(0x10000,0x600000)
    uc.mem_map(0xd0000000,0x2000)
    for _,va,vs,raw,size,_ in sections:
        uc.mem_write(va,data[raw:raw+size])
    def w(a,v):uc.mem_write(a,struct.pack('<I',v))
    w(player+0x24,state);w(player+0xe8c,0x1000000 if window else 0)
    w(player+0x1df4,0);w(actions,held);w(actions+8,held)
    w(stack,stop);w(stack+4,player);w(stack+8,actions)
    uc.reg_write(UC_X86_REG_ESP,stack)
    def code(uc,address,size,user):
        if address in (0x10d980,0x10da80,0x10dbb0):
            sp=uc.reg_read(UC_X86_REG_ESP)
            ret=struct.unpack('<I',uc.mem_read(sp,4))[0]
            uc.reg_write(UC_X86_REG_EAX,0)
            uc.reg_write(UC_X86_REG_ESP,sp+4)
            uc.reg_write(UC_X86_REG_EIP,ret)
    uc.hook_add(UC_HOOK_CODE,code)
    uc.emu_start(entry,stop,count=10000)
    assert uc.reg_read(UC_X86_REG_EIP)==stop
    assert uc.reg_read(UC_X86_REG_ESP)==stack+4
    return uc.mem_read(player+0x24,1)[0]

# Missing directions reproduce each lock; a held forward request (D-pad or
# modern synthesis) permits the original code to leave the action state.
assert run(0x10e920,12,0x200,True)==12
assert run(0x10e920,12,0x201,True)==2
assert run(0x10fa80,17,0,True)==17
assert run(0x10fa80,17,1,True)==2
# The same request must NOT bypass the attack animation's native cancel window.
assert run(0x10fa80,17,1,False)==17
print('PASS: original x86 soul/attack handlers accept held forward; closed attack cancel window stays locked')
