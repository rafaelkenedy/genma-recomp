"""Builds the HD replacement of the game's font atlas (mojist_e page 0) from a free font.

usage: python tools/gen_hd_font.py <mojist_e.ldt from your game> <font.ttf> <small font.ttf> <out.idx.png>

The output is an 8-bit palette-index image, 4x the game's 256x256, for the runtime's texture replacement
(sdk/runtime/gpu/replace.h): textures/E21310868617436E.idx.png. Every glyph is drawn with the given font; the game's
own atlas is only read for where each glyph sits (its ink box per cell), the layout the game draws by. No pixel of
the game's font goes into the output. The style follows the game's indices: 1 fill, 2 and 3 the shading at the
bottom of strokes, 9 the dark outline. See docs/fontes.md.

Made with M PLUS Rounded 1c (SIL Open Font License, Coji Morishita and the M+ Fonts Project).
"""
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S = 4  # scale
FILL, SHADE, SHADE2, OUTLINE = 1, 2, 3, 9
TOP_GAP = S // 2  # empty rows at the top of each big cell, half a game pixel

# big glyphs: rows of 16 cells of 16x16 from y = 32; None leaves a cell empty; a (text, cells) pair spans cells
BIG = [
    [chr(c) for c in range(0x20, 0x30)],
    [chr(c) for c in range(0x30, 0x40)],
    [chr(c) for c in range(0x40, 0x50)],
    [chr(c) if c != 0x5C else '¥' for c in range(0x50, 0x60)],
    ['‘'] + [chr(c) for c in range(0x61, 0x70)],
    [chr(c) for c in range(0x70, 0x7F)] + ['“'],
    ['·', '–', '–', '→', '←', '↑', '↓', '○', '×', '△', '□', 'R1', 'R2', 'L1', 'L2', '‾'],
    ['＼', '!!', '!?', '÷', '…', 'EJECT', '。', '「', '」', '、', '『', '』', '▼', '▷', '®', None],
    [('∞', 2), ('LvMAX', 3)],
]
# small glyphs: rows of 32 cells of 8x8 from y = 0
SMALL = [
    [chr(c) for c in range(0x20, 0x40)],
    [chr(c) if c != 0x5C else '¥' for c in range(0x40, 0x60)],
    ['×'] + [chr(c) for c in range(0x61, 0x7B)] + ['□', '△', '○', '~', '‾'],
]


def load_page0(path):
    d = open(path, 'rb').read()
    i = d.find(b'OIM ')
    doff, dsize = struct.unpack_from('<II', d, i + 0x14)
    a = np.frombuffer(d[i + doff:i + doff + dsize], np.uint8)
    idx = np.empty(a.size * 2, np.uint8)
    idx[0::2] = a & 15
    idx[1::2] = a >> 4
    return idx.reshape(256, 256)


def ink_box(mask):
    ys, xs = np.nonzero(mask)
    if not len(xs):
        return None
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def render(text, font):
    """the text's ink as a boolean array, cropped; None when the font has no glyph for it"""
    if text == 'EJECT':  # ⏏: a triangle over a bar, drawn
        m = Image.new('L', (200, 200), 0)
        dr = ImageDraw.Draw(m)
        dr.polygon([(100, 10), (190, 130), (10, 130)], fill=255)
        dr.rectangle([10, 160, 190, 195], fill=255)
        return np.asarray(m) > 127
    a = draw(text, font)
    if a is None or any(np.array_equal(draw(ch, font), missing(font)) for ch in text):
        return None
    return a


def draw(text, font):
    size = 256
    img = Image.new('L', (size * max(1, len(text)) + 64, size + 64), 0)
    ImageDraw.Draw(img).text((32, 16), text, font=font, fill=255)
    a = np.asarray(img) > 127
    box = ink_box(a)
    if box is None:
        return None
    x0, y0, x1, y1 = box
    return a[y0:y1, x0:x1]


_missing = {}


def missing(font):
    """what the font draws for a character it does not have"""
    if id(font) not in _missing:
        _missing[id(font)] = draw('􏿽', font)
    return _missing[id(font)]


def place(out, mask, box):
    """the ink scaled into box (x0, y0, x1, y1) of the 4x canvas, as FILL"""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return
    img = Image.fromarray((mask * 255).astype(np.uint8)).resize((w, h), Image.LANCZOS)
    m = np.asarray(img) > 110
    region = out[y0:y1, x0:x1]
    region[m] = FILL


def style(canvas, region):
    """outline and shading for the fill inside region (y0, y1, x0, x1) of the canvas"""
    y0, y1, x0, x1 = region
    fill = canvas[y0:y1, x0:x1] == FILL
    # outline: the fill grown by one game pixel (S) all round
    grown = np.asarray(Image.fromarray((fill * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * S + 1))) > 0
    sub = canvas[y0:y1, x0:x1]
    sub[grown & ~fill] = OUTLINE
    # shading: the lowest game pixel of every stroke a little darker, its last quarter darker still
    below = lambda k: np.vstack([fill[k:], np.zeros((k, fill.shape[1]), bool)])
    edge1 = fill & ~below(S // 4 if S >= 4 else 1)
    edge2 = fill & ~below(S) & ~edge1
    sub[edge2] = SHADE
    sub[edge1] = SHADE2


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    orig = load_page0(sys.argv[1])
    big_font = ImageFont.truetype(sys.argv[2], 200)
    small_font = ImageFont.truetype(sys.argv[3], 200)
    canvas = np.zeros((256 * S, 256 * S), np.uint8)

    # big glyphs: the game's fill (indices 1..8) gives each glyph's box
    for r, row in enumerate(BIG):
        col = 0
        for entry in row:
            if entry is None:
                col += 1
                continue
            text, span = entry if isinstance(entry, tuple) else (entry, 1)
            cx, cy = col * 16, 32 + r * 16
            cell = orig[cy:cy + 16, cx:cx + 16 * span]
            box = ink_box((cell >= 1) & (cell <= 8))
            col += span
            if box is None or text == ' ':
                continue
            mask = render(text, big_font)
            if mask is None:
                print(f'no glyph for {text!r}, cell left empty')
                continue
            bx0, by0, bx1, by1 = box
            place(canvas, mask, ((cx + bx0) * S, (cy + by0) * S, (cx + bx1) * S, (cy + by1) * S))
            style(canvas, ((cy) * S, (cy + 16) * S, (cx) * S, (cx + 16 * span) * S))
        # the game's glyph quads end on the cell's bottom edge; above 480p the filter reads a sliver of the next cell
        # row, so the top of every cell stays empty: the outline of the glyph below no longer shows as a dash under
        # the text (the original font does it too, at high resolution)
        canvas[(32 + r * 16) * S:(32 + r * 16) * S + TOP_GAP] = 0

    # small glyphs: thin, fill only, as the game's (indices 1 and 5)
    for r, row in enumerate(SMALL):
        for c, text in enumerate(row):
            cx, cy = c * 8, r * 8
            cell = orig[cy:cy + 8, cx:cx + 8]
            box = ink_box(cell != 0)
            if box is None or text == ' ':
                continue
            mask = render(text, small_font)
            if mask is None:
                continue
            bx0, by0, bx1, by1 = box
            place(canvas, mask, ((cx + bx0) * S, (cy + by0) * S, (cx + bx1) * S, (cy + by1) * S))

    Image.fromarray(canvas, 'L').save(sys.argv[4], optimize=True)
    print(f'{sys.argv[4]}: {canvas.shape[1]}x{canvas.shape[0]}, indices {sorted(np.unique(canvas).tolist())}')


if __name__ == '__main__':
    main()
