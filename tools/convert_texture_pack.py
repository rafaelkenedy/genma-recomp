"""Converts a texture pack made for another release of the game into this port's HD textures.

usage: python tools/convert_texture_pack.py <textures_dump> <pack textures> <out textures folder>
                                            [--texconv texconv.exe] [--min-score 0.97] [--no-colour]

  textures_dump   this port's textures, written by running the game with XBE_DUMP_TEXTURES=1 (<hash>.png)
  pack textures   a folder of DDS files (for example a Special K pack's SK_Res/inject/textures)
  out             the port's textures folder (game/textures in a release, out/build/release/textures for a build)

Each dumped texture is matched by content to the pack's closest image (a 64x48 thumbnail correlation, cached in
<out>/.pack_thumbs.npz). A match at or above --min-score is written as <out>/<hash>.dds (a P8 texture as
<hash>.p<palette signature>.dds, one per palette, since the game may draw the same indices with several palettes): the pack image with its
colours fitted to the port's texture (a linear fit per channel, from the pack image shrunk to the port's size), its
alpha kept where the port's texture has any, then compressed with texconv (BC1 when opaque, BC3 with alpha, all mip
levels). A fit close to identity copies the pack's DDS as it is. Without texconv a PNG is written instead.
A report goes to <out>/pack_report.csv.

The pack's files belong to their authors and to the game's publisher: convert them on your own PC, for your own copy
of the game. Nothing converted here goes into this repository or its releases.
"""
import argparse
import csv
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image

THUMB = (64, 48)


def thumb_of(path):
    try:
        im = Image.open(path)
        im.draft('RGB', (256, 256))
        size = im.size
        return path, size, np.asarray(im.convert('RGB').resize(THUMB, Image.BILINEAR), np.uint8)
    except Exception:
        return path, (0, 0), None


def pack_index(pack, cache):
    files = sorted(f for f in os.listdir(pack) if f.lower().endswith('.dds'))
    if os.path.exists(cache):
        z = np.load(cache)
        if list(z['names']) == files:
            return list(z['names']), z['sizes'], z['thumbs']
    print(f'thumbnails of {len(files)} pack textures (once, cached in {cache}) ...', flush=True)
    names, sizes, thumbs = [], [], []
    with ProcessPoolExecutor() as ex:
        for i, (p, s, t) in enumerate(ex.map(thumb_of, [os.path.join(pack, f) for f in files], chunksize=8)):
            if t is not None:
                names.append(os.path.basename(p))
                sizes.append(s)
                thumbs.append(t)
            if i % 500 == 0:
                print(f'  {i}/{len(files)}', flush=True)
    np.savez_compressed(cache, names=np.array(names), sizes=np.array(sizes), thumbs=np.stack(thumbs))
    return names, np.array(sizes), np.stack(thumbs)


def normalized(a):
    g = a.astype(np.float32).mean(-1)
    g = g - g.mean(axis=(-1, -2), keepdims=True)
    return g / (np.sqrt((g * g).sum(axis=(-1, -2), keepdims=True)) + 1e-6)


def palette_signature(dump, h):
    """the runtime's PaletteSignature (sdk/runtime/gpu/replace.h) of a P8 texture's dump: FNV-1a 64 over each index
    present, in order, and its colour's B G R A bytes; None when the texture has no indices dump (not P8)"""
    idx_path = os.path.join(dump, h + '.idx.png')
    if not os.path.exists(idx_path):
        return None
    idx = np.asarray(Image.open(idx_path).convert('L'))
    names = [f for f in os.listdir(dump)
             if f.upper().startswith(h) and f.lower().endswith('.png') and '.idx.' not in f.lower()]
    if not names:
        return None
    rgba = np.asarray(Image.open(os.path.join(dump, sorted(names)[0])).convert('RGBA'))
    sig = 1469598103934665603
    for i in range(256):
        where = np.argwhere(idx == i)
        if not len(where):
            continue
        y, x = where[0]
        r, g, b, a = (int(v) for v in rgba[y, x])
        for byte in (i, b, g, r, a):
            sig = ((sig ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return sig


def colour_fit(pack_small, port):
    """per channel a, b with port ~ a * pack + b, from the pack shrunk to the port's size"""
    fit = []
    for c in range(3):
        x = pack_small[..., c].ravel().astype(np.float64)
        y = port[..., c].ravel().astype(np.float64)
        if x.std() < 1:
            fit.append((1.0, float(y.mean() - x.mean())))
            continue
        a, b = np.polyfit(x, y, 1)
        fit.append((float(a), float(b)))
    return fit


def convert(task):
    """one matched texture: the pack image, colours fitted to the port's, written as DDS (or PNG without texconv)"""
    row, src, port_path, out_stem, out, texconv, no_colour = task
    port = Image.open(port_path).convert('RGBA')
    w, hgt = port.size
    hd = Image.open(src).convert('RGBA')
    port_a = np.asarray(port, np.float32)
    hd_a = np.asarray(hd, np.float32).copy()
    fit = [(1.0, 0.0)] * 3 if no_colour else colour_fit(np.asarray(hd.resize((w, hgt), Image.BOX), np.float32), port_a)
    row['colour'] = ' '.join(f'{a:.3f}*x{b:+.1f}' for a, b in fit)
    identity = all(abs(a - 1) < 0.03 and abs(b) < 3 for a, b in fit)
    opaque = port_a[..., 3].min() >= 250
    out_dds = os.path.join(out, out_stem + '.dds')
    if identity and (not opaque or hd_a[..., 3].min() >= 250):
        shutil.copyfile(src, out_dds)
        row['result'] = 'copied'
        return row
    for c, (a, b) in enumerate(fit):
        hd_a[..., c] = np.clip(hd_a[..., c] * a + b, 0, 255)
    if opaque:
        hd_a[..., 3] = 255
    img = Image.fromarray(hd_a.astype(np.uint8), 'RGBA')
    if not (texconv and os.path.exists(texconv)):
        img.save(os.path.join(out, out_stem + '.png'))
        row['result'] = 'colour fitted, PNG (no texconv)'
        return row
    with tempfile.TemporaryDirectory(dir=out) as tmp:  # beside the output: the PNG of a 4K texture is tens of MB
        png = os.path.join(tmp, 'tex.png')
        img.save(png)
        fmt = 'BC1_UNORM' if opaque else 'BC3_UNORM'
        r = subprocess.run([texconv, '-nologo', '-y', '-f', fmt, '-m', '0', '-o', tmp, png], capture_output=True, text=True)
        made = os.path.join(tmp, 'tex.dds')
        if r.returncode or not os.path.exists(made):
            row['result'] = 'texconv failed: ' + (r.stdout + r.stderr).strip()[-200:]
            return row
        shutil.move(made, out_dds)
    row['result'] = 'colour fitted, ' + ('BC1' if opaque else 'BC3')
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump')
    ap.add_argument('pack')
    ap.add_argument('out')
    ap.add_argument('--texconv', default=shutil.which('texconv') or '')
    ap.add_argument('--min-score', type=float, default=0.97)
    ap.add_argument('--no-colour', action='store_true')
    ap.add_argument('--force', action='store_true', help='convert again textures already in out')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    names, sizes, thumbs = pack_index(args.pack, os.path.join(args.out, '.pack_thumbs.npz'))
    tn = normalized(thumbs)
    dumps = sorted(f for f in os.listdir(args.dump) if f.lower().endswith('.png') and not f.lower().endswith('.idx.png'))
    rows, tasks = [], []
    for f in dumps:
        h = f[:16].upper()
        stem = f[:-4]
        # a P8 texture is replaced per palette: <hash>.p<signature>
        if '.p' in stem.lower():
            out_stem = h + '.p' + stem[18:34].upper()
        else:
            sig = palette_signature(args.dump, h)
            out_stem = h if sig is None else f'{h}.p{sig:016X}'
            # a conversion from before the palette names
            old = os.path.join(args.out, h + '.dds')
            if sig is not None and os.path.exists(old) and not os.path.exists(os.path.join(args.out, out_stem + '.dds')):
                os.replace(old, os.path.join(args.out, out_stem + '.dds'))
                print(f'{h}.dds renamed {out_stem}.dds (per palette)')
        if not args.force and any(os.path.exists(os.path.join(args.out, out_stem + e)) for e in ('.dds', '.png')):
            continue
        if not args.force and out_stem == h and os.path.exists(os.path.join(args.out, h + '.idx.png')):
            continue
        port = Image.open(os.path.join(args.dump, f)).convert('RGBA')
        w, hgt = port.size
        if w < 64 or hgt < 64:
            continue
        t = np.asarray(port.convert('RGB').resize(THUMB, Image.BILINEAR), np.float32)[None]
        score = (tn * normalized(t)).sum(axis=(-1, -2))
        k = int(score.argmax())
        best, pw, ph = float(score[k]), int(sizes[k][0]), int(sizes[k][1])
        row = {'hash': out_stem, 'port_size': f'{w}x{hgt}', 'pack_file': names[k], 'pack_size': f'{pw}x{ph}', 'score': f'{best:.4f}'}
        if best < args.min_score or pw < w or ph < hgt:
            row['result'] = 'no match'
            rows.append(row)
            continue
        tasks.append((row, os.path.join(args.pack, names[k]), os.path.join(args.dump, f), out_stem, args.out,
                      args.texconv, args.no_colour))
    # the conversions in parallel: each decodes a large DDS, fits it and runs texconv
    with ProcessPoolExecutor(args.jobs) as ex:
        for row in ex.map(convert, tasks):
            rows.append(row)
            print(f"{row['hash']} {row['port_size']:>9} <- {row['pack_file']} {row['pack_size']} {row['score']} "
                  f"{row['result']}", flush=True)
    with open(os.path.join(args.out, 'pack_report.csv'), 'w', newline='') as fh:
        wr = csv.DictWriter(fh, fieldnames=['hash', 'port_size', 'pack_file', 'pack_size', 'score', 'colour', 'result'])
        wr.writeheader()
        wr.writerows(rows)
    done = sum(1 for r in rows if r['result'] != 'no match' and not r['result'].startswith('texconv failed'))
    print(f'{done} converted, {len(rows) - done} without a match or failed; report in pack_report.csv')


if __name__ == '__main__':
    sys.exit(main())
