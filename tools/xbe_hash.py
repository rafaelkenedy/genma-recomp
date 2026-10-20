"""The runtime's texture hash (sdk/runtime/gpu/shaders.cpp, gpu::Hash) in Python, and the XPR0 textures of the game's
files with the names the runtime's HD replacement gives them.

usage: python tools/xbe_hash.py <file.bgs | file.xpr> ...   prints each texture's hash, size and format
"""
import struct
import sys

M64 = 0xFFFFFFFFFFFFFFFF
K0, K1 = 0x9E3779B97F4A7C15, 0xE7037ED1A0B428DB
SEED = 1469598103934665603


def _mix(a, b):
    r = (a & M64) * (b & M64)
    return (r & M64) ^ (r >> 64)


def texture_hash(data, h=SEED):
    """gpu::Hash: 16 bytes a step through a 64 x 64 -> 128 bit multiply"""
    n = len(data)
    seed = (h ^ K0) & M64
    full = n - n % 16
    words = struct.unpack_from(f'<{full // 8}Q', data, 0) if full else ()
    for i in range(0, len(words), 2):
        seed = _mix(words[i] ^ K1, words[i + 1] ^ seed)
    r = n - full
    if r:
        if r >= 8:
            x = struct.unpack_from('<Q', data, full)[0]
            y = struct.unpack_from('<Q', data, n - 8)[0]
        else:
            x = int.from_bytes(data[full:n], 'little')
            y = 0
        seed = _mix(x ^ K1, y ^ seed ^ r)
    return _mix(seed ^ K0, n ^ K1)


# Xbox texture formats: name, bits per pixel, block compressed
FORMATS = {0x0C: ('DXT1', 4, True), 0x0E: ('DXT3', 8, True), 0x0F: ('DXT5', 8, True), 0x0B: ('P8', 8, False),
           0x06: ('A8R8G8B8', 32, False), 0x07: ('X8R8G8B8', 32, False), 0x05: ('R5G6B5', 16, False),
           0x04: ('A4R4G4B4', 16, False), 0x02: ('A1R5G5B5', 16, False), 0x03: ('X1R5G5B5', 16, False)}


def level0_size(fmt, w, h):
    name, bpp, block = FORMATS[fmt]
    if block:
        return max(1, w // 4) * max(1, h // 4) * (8 if fmt == 0x0C else 16)
    return w * h * bpp // 8


def xpr_textures(data, base=0):
    """the textures of one XPR0 block at base: (offset of level 0 in the file, width, height, format, levels)"""
    magic, total, header = struct.unpack_from('<4sII', data, base)
    if magic != b'XPR0':
        return []
    out = []
    o = base + 12
    while o + 20 <= base + header:
        common, offset, lock, fmt, size = struct.unpack_from('<IIIII', data, o)
        if common == 0xFFFFFFFF:
            break
        rtype = (common >> 16) & 7
        if rtype in (4,):  # texture
            f = (fmt >> 8) & 0xFF
            levels = (fmt >> 16) & 15
            w, h = 1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)
            out.append((base + header + offset, w, h, f, levels))
            o += 20
        else:
            o += 20 if rtype in (0, 1, 2) else 4
    return out


def file_textures(data):
    """every XPR0 block in a file; the .bgs files keep other data (depth) between them"""
    out, i = [], 0
    while (i := data.find(b'XPR0', i)) >= 0:
        total, header = struct.unpack_from('<II', data, i + 4)
        if 12 <= header <= total <= len(data) - i:
            out += xpr_textures(data, i)
            i += total
        else:
            i += 4
    return out


if __name__ == '__main__':
    for path in sys.argv[1:]:
        d = open(path, 'rb').read()
        for off, w, h, f, levels in file_textures(d):
            if f not in FORMATS:
                print(f'{path}: {off:#x} {w}x{h} format {f:#x} (unknown)')
                continue
            size = level0_size(f, w, h)
            print(f'{path}: {texture_hash(d[off:off + size]):016X} {w}x{h} {FORMATS[f][0]} levels {levels}')
