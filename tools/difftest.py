"""Runs the original x86 code of a function in Unicorn from difftest_before.bin and compares the memory it leaves
with difftest_after.bin, which the recompiled code produced from the same state (src/game/difftest.cpp).

usage: python tools/difftest.py ENTRY [--out DIR]
Prints the dwords that differ, as integers and as floats, grouped into runs.
"""
import struct
import sys
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESP,
                               UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_FPCW)

OUT = Path(__file__).resolve().parent.parent / 'out' / 'build' / 'release'


def load(path):
    data = path.read_bytes()
    regs = struct.unpack_from('<10I', data, 0)
    blocks, o = [], 40
    while o < len(data):
        addr, size = struct.unpack_from('<II', data, o)
        blocks.append((addr, data[o + 8:o + 8 + size]))
        o += 8 + size
    return regs, blocks


def main():
    entry = int(sys.argv[1], 16)
    out = Path(sys.argv[sys.argv.index('--out') + 1]) if '--out' in sys.argv else OUT
    regs, before = load(out / 'difftest_before.bin')
    _, after = load(out / 'difftest_after.bin')
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for addr, blob in before:
        uc.mem_map(addr, len(blob))
        uc.mem_write(addr, blob)
    for r, v in zip([UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP,
                     UC_X86_REG_ESI, UC_X86_REG_EDI], regs[:8]):
        uc.reg_write(r, v)
    uc.reg_write(UC_X86_REG_FPCW, regs[9])
    esp = regs[4]
    ret = struct.unpack('<I', uc.mem_read(esp, 4))[0]
    print(f'entry {entry:08X} esp {esp:08X} returns to {ret:08X} fcw {regs[9]:04X}')

    def unmapped(uc, access, addr, size, value, data):
        print(f'unmapped access {access} at {addr:08X} from eip {uc.reg_read(UC_X86_REG_EIP):08X}')
        return False
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
    try:
        uc.emu_start(entry, ret, count=50_000_000)
    except UcError as e:
        print('emulation stopped:', e, f'eip {uc.reg_read(UC_X86_REG_EIP):08X}')
        return
    print(f'emulation done, eip {uc.reg_read(UC_X86_REG_EIP):08X}')

    diffs = []
    for addr, blob in after:
        # the stack under the caller's frame is scratch: compare from esp up
        try:
            emu = bytes(uc.mem_read(addr, len(blob)))
        except UcError:
            continue
        if emu == blob:
            continue
        for o in range(0, len(blob) - 3, 4):
            a = addr + o
            if a < esp and a >= esp - 0x10000:
                continue
            x, y = blob[o:o + 4], emu[o:o + 4]
            if x == y:
                continue
            fx, fy = struct.unpack('<f', x)[0], struct.unpack('<f', y)[0]
            # x87 80 bit against the recompiler's double: allow a little
            if abs(fx - fy) <= 1e-3 * max(1.0, abs(fy)):
                continue
            diffs.append((a, struct.unpack('<I', x)[0], struct.unpack('<I', y)[0], fx, fy))
    print(len(diffs), 'differing dwords (recompiled vs original)')
    run = None
    for a, ix, iy, fx, fy in diffs[:400]:
        if run is None or a != run + 4:
            print('--')
        run = a
        print(f'  {a:08X}  recomp {ix:08X} ({fx: .5g})   x86 {iy:08X} ({fy: .5g})')


if __name__ == '__main__':
    main()
