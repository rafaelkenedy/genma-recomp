"""Decodes the OIM images inside the game's LINK containers (.ldt) to PNG.

usage: python tools/oim.py <file.ldt> <out folder>

LINK: 'LINK', entry count, ?, then the entries; each image entry is an OIM block:
  +0x00 'OIM '  +0x04 block size
  +0x10 log2 width, log2 height (u8 each), format (u16, 0x14 seen)
  +0x14 pixel data offset, pixel data size (from the block start)
  +0x20 palette format, palette offset, palette size (4 bytes per colour: B G R A, alpha 0..0x80)
Pixels are 4 bits (low nibble first) or 8 bits per pixel, linear. See docs/fontes.md.
The PNGs are the game's own images: they stay on your PC, never in the repository.
"""
import os
import struct
import sys

from PIL import Image


def decode(path, out):
    d = open(path, 'rb').read()
    base = os.path.basename(path)
    i, n = 0, 0
    while (i := d.find(b'OIM ', i)) >= 0:
        wl, hl, fmt = d[i + 0x10], d[i + 0x11], struct.unpack_from('<H', d, i + 0x12)[0]
        doff, dsize = struct.unpack_from('<II', d, i + 0x14)
        _, poff, psize = struct.unpack_from('<III', d, i + 0x20)
        w, h = 1 << wl, 1 << hl
        bpp = dsize * 8 // (w * h)
        px = d[i + doff:i + doff + dsize]
        pal = d[i + poff:i + poff + psize]
        colors = [(pal[k * 4 + 2], pal[k * 4 + 1], pal[k * 4], min(255, pal[k * 4 + 3] * 2)) for k in range(psize // 4)]
        if bpp == 4:
            idx = [v for b in px for v in (b & 15, b >> 4)]
        elif bpp == 8:
            idx = list(px)
        else:
            print(f'{base} #{n}: {bpp} bits per pixel, skipped')
            i, n = i + 4, n + 1
            continue
        img = Image.new('RGBA', (w, h))
        img.putdata([colors[v] if v < len(colors) else (255, 0, 255, 255) for v in idx])
        name = os.path.join(out, f'{base}_{n}_{w}x{h}_{bpp}bpp.png')
        img.save(name)
        print(f'{name}: format {fmt:#x}, {len(colors)} colours')
        i, n = i + 4, n + 1


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    os.makedirs(sys.argv[2], exist_ok=True)
    decode(sys.argv[1], sys.argv[2])
