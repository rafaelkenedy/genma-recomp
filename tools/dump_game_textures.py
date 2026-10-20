"""Writes the textures in the game's files the way the runtime's XBE_DUMP_TEXTURES does: <hash>.png, the name the HD
replacement looks for. It reads the XPR0 textures of image/bgs (the rooms' pre-rendered backgrounds, one per camera),
so a pack can be converted for every room without playing through them.

usage: python tools/dump_game_textures.py <game folder with image/> <out folder (textures_dump)>
"""
import glob
import io
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from xbe_hash import FORMATS, file_textures, level0_size, texture_hash  # noqa: E402


def dds(w, h, fourcc, blocks):
    """a DDS file around DXT blocks, for PIL to decode"""
    head = struct.pack('<4sIIIIIII44xII4sIIIIIIIIII', b'DDS ', 124, 0x1007, h, w, len(blocks), 0, 1,
                       32, 4, fourcc, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    return head + blocks


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    game, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    files = sorted(glob.glob(os.path.join(game, 'image', 'bgs', '*.bgs')) + glob.glob(os.path.join(game, 'image', 'bgs', '*.xpr')))
    written = skipped = 0
    for path in files:
        d = open(path, 'rb').read()
        for off, w, h, f, levels in file_textures(d):
            if f not in (0x0C, 0x0E, 0x0F):
                skipped += 1
                continue
            size = level0_size(f, w, h)
            blocks = d[off:off + size]
            name = os.path.join(out, f'{texture_hash(blocks):016X}.png')
            if os.path.exists(name):
                continue
            fourcc = {0x0C: b'DXT1', 0x0E: b'DXT3', 0x0F: b'DXT5'}[f]
            Image.open(io.BytesIO(dds(w, h, fourcc, blocks))).convert('RGBA').save(name)
            written += 1
    print(f'{written} textures written to {out} from {len(files)} files ({skipped} not block compressed, skipped)')


if __name__ == '__main__':
    main()
