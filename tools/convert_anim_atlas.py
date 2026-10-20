"""Converts the game's animated background atlases (smoke, fire, water) from a texture pack whose release stored each
frame as its own texture (the Onimusha: Warlords 4K pack: one 1920x1152 file a frame, where Genma's Xbox release puts
30 frames of 320x192 in one 2048x1024 texture).

usage: python tools/convert_anim_atlas.py <textures_dump> <pack textures> <out textures folder> [--texconv texconv.exe]
                                          [--jobs N] [--min-score 0.92] [--only HASH ...] [--redo]
                                          [--game <game folder with image/>]

For each dumped texture without a replacement in <out>:
  - the frame size is found from the pack: for each pack image size and scale K in 4, 6, 8, the atlas's top left
    (size / K) is compared with the pack's images of that size; the best match at or above --min-score sets the grid
  - the atlas's frames are the grid cells that look like the first one; with --game, atlases one after another in a
    game file with one grid are one animation (the river's water: 4 atlases of 8 frames), their frames in file order
  - the pack's frames are put in time order (each next to the one it looks most like) and spread over the game's
    frames, from the start and direction whose motion (each frame minus the mean of all) matches the game's best;
    the game may have the pack's frames (the smoke) or its own animation (the Xbox water)
  - the atlas is rebuilt K times larger (at most 16384 pixels a side) from the pack's frames, its colours fitted to the
    game's, and written as <hash>.dds (BC1, or BC3 with alpha) with texconv
With --game, animations the game keeps one frame a texture (a run of alike textures of one size in a game file) are
converted the same way, one DDS a frame. A report goes to <out>/anim_report.csv. As with convert_texture_pack.py, what is converted stays on your PC.
"""
import argparse
import csv
import glob
import os
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image

sys.path.append(os.path.dirname(os.path.abspath(__file__)))  # appended: tools/ has scripts named like stdlib modules
from convert_texture_pack import THUMB, colour_fit, normalized, pack_index  # noqa: E402

Image.MAX_IMAGE_PIXELS = None
SCALES = (6, 4, 8)
MAX_SIDE = 16384


def thumb(a):
    return normalized(np.asarray(Image.fromarray(a).resize(THUMB, Image.BILINEAR), np.float32)[None])[0]


def find_grid(a, groups, tn, min_score):
    """(frame w, frame h, K, pack size, score) of the best grid, or None"""
    H, W = a.shape[:2]
    tried, best = {}, None
    for (pw, ph), idx in groups.items():
        for k in SCALES:
            if pw % k or ph % k:
                continue
            fw, fh = pw // k, ph // k
            if fw < 32 or fh < 32 or fw > W or fh > H or (W // fw) * (H // fh) < 2:
                continue
            if (fw, fh) not in tried:
                tried[(fw, fh)] = thumb(a[:fh, :fw])
            s = float((tn[idx] * tried[(fw, fh)]).sum(axis=(-1, -2)).max())
            if s >= min_score and (best is None or s > best[4]):
                best = (fw, fh, k, (pw, ph), s)
    return best


def frames_of(a, fw, fh):
    """the grid cells that look like the first: [(x, y)]"""
    t0 = thumb(a[:fh, :fw])
    out = []
    for y in range(a.shape[0] // fh):
        for x in range(a.shape[1] // fw):
            c = a[y * fh:(y + 1) * fh, x * fw:(x + 1) * fw]
            if c[..., :3].mean() < 4:
                continue
            if x == 0 and y == 0 or float((thumb(c) * t0).sum()) > 0.7:
                out.append((x, y))
    return out


def residuals(v):
    r = v - v.mean(0)
    r = r - r.mean(axis=(1, 2), keepdims=True)
    return r / (np.linalg.norm(r, axis=(1, 2), keepdims=True) + 1e-6)


def time_order(pp):
    """the frames as a loop where each sits between the two it looks most like: nearest neighbour first, then moving
    single frames and reversing stretches (2-opt) while that makes the loop smoother. The nearest neighbour alone
    skips frames and leaves them at the end, out of place, and the animation jumps there."""
    n = len(pp)
    order, left = [0], set(range(1, n))
    while left:
        nxt = max(left, key=lambda j: pp[order[-1], j])
        order.append(nxt)
        left.remove(nxt)
    def gain_move(order):
        """move one frame to the place where the loop gains the most; False when none gains"""
        best = (1e-6, None)
        for k in range(n):
            v, p, q = order[k], order[k - 1], order[(k + 1) % n]
            removed = pp[p, v] + pp[v, q] - pp[p, q]
            rest = order[:k] + order[k + 1:]
            for m in range(len(rest)):
                a, b = rest[m], rest[(m + 1) % len(rest)]
                g = pp[a, v] + pp[v, b] - pp[a, b] - removed
                if g > best[0]:
                    best = (g, (k, m))
        if best[1] is None:
            return False
        k, m = best[1]
        v = order.pop(k)
        order.insert(m + 1, v)
        return True

    better = True
    while better:
        better = gain_move(order)
        for i in range(n - 1):
            for j in range(i + 2, n if i else n - 1):
                a, b, c, d = order[i], order[i + 1], order[j], order[(j + 1) % n]
                if pp[a, c] + pp[b, d] > pp[a, b] + pp[c, d] + 1e-6:
                    order[i + 1:j + 1] = order[i + 1:j + 1][::-1]
                    better = True
    return order


def assign(fv, pv):
    """which pack frame each game frame takes, from their motion (residuals): the pack's frames in time order, spread
    over the game's from the start and direction that match best. Matching each frame on its own can swap two that look
    alike, and the animation then jumps back and forth (it flickers); following the order cannot.
    mode: matched when the game's frames are the pack's (the smoke), sequence when the game drew its own (the water)"""
    m = np.einsum('ixy,jxy->ij', fv, pv)
    pp = np.einsum('ixy,jxy->ij', pv, pv)
    np.fill_diagonal(pp, -2)
    order = time_order(pp)
    n, nf = len(order), len(fv)
    best = None
    for step in (1, -1):
        for off in range(n):
            p = [order[(off + step * round(i * n / nf)) % n] for i in range(nf)]
            score = sum(m[i, p[i]] for i in range(nf))
            if best is None or score > best[0]:
                best = (score, p)
    return best[1], 'matched' if best[0] / nf >= 0.6 else 'sequence', m


def frame_images(pimg, pick, mode, m):
    """each game frame's pack image (RGBA arrays); in a matched animation a frame that matches far worse than the rest
    (the pack has another moment there: the Warlords smoke's frame 24) is the mean of its two neighbours instead,
    which for slow smoke or water is close to the missing frame; outliers: how many were replaced"""
    nf = len(pick)
    imgs = [np.asarray(pimg[pick[i]]) for i in range(nf)]
    if mode != 'matched' or nf < 3:
        return imgs, 0
    score = np.array([m[i, pick[i]] for i in range(nf)])
    bad = score < np.median(score) - 0.1
    out = list(imgs)
    for i in np.flatnonzero(bad):
        a, b = (i - 1) % nf, (i + 1) % nf
        if bad[a] or bad[b]:
            continue
        out[i] = ((imgs[a].astype(np.uint16) + imgs[b]) // 2).astype(np.uint8)
    return out, int(bad.sum())


def encode(img, out_path, opaque, texconv, tmp_dir):
    """img (RGBA uint8 array) to a DDS with all mips; an error text, or None"""
    with tempfile.TemporaryDirectory(dir=tmp_dir) as tmp:  # beside the output: an atlas is hundreds of MB as a PNG
        png = os.path.join(tmp, 'tex.png')
        Image.fromarray(img).save(png)
        fmt = 'BC1_UNORM' if opaque else 'BC3_UNORM'
        r = subprocess.run([texconv, '-nologo', '-y', '-f', fmt, '-m', '0', '-o', tmp, png], capture_output=True, text=True)
        made = os.path.join(tmp, 'tex.dds')
        if r.returncode or not os.path.exists(made):
            return 'texconv failed: ' + (r.stdout + r.stderr).strip()[-200:]
        shutil.move(made, out_path)
    return None


def convert_set(task):
    """an animation the game keeps one frame a texture: each texture takes its pack frame, in time order"""
    hashes, dump, pack, out, texconv, cands = task
    ports = [np.asarray(Image.open(os.path.join(dump, h + '.png')).convert('RGBA')) for h in hashes]
    H, W = ports[0].shape[:2]
    g = (max(16, W // 2), max(16, H // 2))
    grey = lambda im: np.asarray(im.convert('L').resize(g, Image.BOX), np.float32)
    pimg = [Image.open(os.path.join(pack, n)).convert('RGBA') for n in cands]
    pick, mode, m = assign(residuals(np.stack([grey(Image.fromarray(p)) for p in ports])),
                           residuals(np.stack([grey(p) for p in pimg])))
    fimg, outliers = frame_images(pimg, pick, mode, m)
    # one colour fit for the whole animation, so the frames keep one tone
    small = np.concatenate([np.asarray(Image.fromarray(fimg[i]).resize((W, H), Image.BOX), np.float32)
                            for i in range(len(ports))])
    fit = colour_fit(small, np.concatenate(ports).astype(np.float32))
    opaque = all(p[..., 3].min() >= 250 for p in ports)
    row = {'hash': hashes[0], 'mode': mode, 'size': f'{W}x{H}', 'frame': 'texture', 'frames': len(hashes),
           'scale': pimg[0].size[0] // W, 'weak_frames': outliers, 'colour': ' '.join(f'{s:.3f}*x{b:+.1f}' for s, b in fit)}
    for i, h in enumerate(hashes):
        hf = fimg[i].astype(np.float32)
        for c, (s, b) in enumerate(fit):
            hf[..., c] = np.clip(hf[..., c] * s + b, 0, 255)
        if opaque:
            hf[..., 3] = 255
        err = encode(hf.astype(np.uint8), os.path.join(out, h + '.dds'), opaque, texconv, out)
        if err:
            row['result'] = err
            return row
    row['result'] = 'converted, ' + ('BC1' if opaque else 'BC3')
    return row


def frame_sets(game, dump, min_len=4, min_sim=0.99):
    """runs of textures of one size, one after another in a game file, each nearly the one before: [hashes] in file
    order. Shorter or looser runs are left alone: neighbouring camera angles of a room also look alike."""
    from xbe_hash import FORMATS, file_textures, level0_size, texture_hash
    sets = []
    files = glob.glob(os.path.join(game, 'image', 'bgs', '*.bgs')) + glob.glob(os.path.join(game, 'image', 'bgs', '*.xpr'))
    for path in sorted(files):
        d = open(path, 'rb').read()
        run = []
        for off, w, h, f, lv in file_textures(d) + [(0, 0, 0, None, 0)]:
            hs, t = None, None
            if f in FORMATS:
                hs = f'{texture_hash(d[off:off + level0_size(f, w, h)]):016X}'
                png = os.path.join(dump, hs + '.png')
                if os.path.exists(png):
                    t = thumb(np.asarray(Image.open(png).convert('RGB')))
            if t is not None and run and (w, h) == run[0][1] and float((t * run[-1][2]).sum()) >= min_sim:
                run.append((hs, (w, h), t))
                continue
            if len(run) >= min_len:
                sets.append([x[0] for x in run])
            run = [(hs, (w, h), t)] if t is not None else []
    return sets


def convert(task):
    """one animation: atlases of one grid, in the game's file order; their frames in that order are the animation"""
    hashes, dump, pack, out, texconv, fw, fh, k, cands, cells_of = task
    ports = [np.asarray(Image.open(os.path.join(dump, h + '.png')).convert('RGBA')) for h in hashes]
    H, W = ports[0].shape[:2]
    g = (max(16, fw // 2), max(16, fh // 2))
    grey = lambda im: np.asarray(im.convert('L').resize(g, Image.BOX), np.float32)
    cells = [(a, x, y) for a, cs in enumerate(cells_of) for x, y in cs]
    fv = residuals(np.stack([grey(Image.fromarray(ports[a][y * fh:(y + 1) * fh, x * fw:(x + 1) * fw]))
                             for a, x, y in cells]))
    pimg = [Image.open(os.path.join(pack, n)).convert('RGBA') for n in cands]
    pick, mode, m = assign(fv, residuals(np.stack([grey(p) for p in pimg])))
    nf = len(cells)
    fimg, outliers = frame_images(pimg, pick, mode, m)
    ko = min(k, MAX_SIDE // W, MAX_SIDE // H)
    hds, masks = [], []
    for port in ports:
        hds.append(np.asarray(Image.fromarray(port).resize((W * ko, H * ko), Image.NEAREST)).copy())
        masks.append(np.zeros((H, W), bool))
    for i, (a, x, y) in enumerate(cells):
        f = fimg[i]
        if ko != k:
            f = np.asarray(Image.fromarray(f).resize((fw * ko, fh * ko), Image.LANCZOS))
        hds[a][y * fh * ko:(y + 1) * fh * ko, x * fw * ko:(x + 1) * fw * ko] = f
        masks[a][y * fh:(y + 1) * fh, x * fw:(x + 1) * fw] = True
    # one colour fit for the whole animation, so its atlases keep one tone
    small = [np.asarray(Image.fromarray(hd).resize((W, H), Image.BOX), np.float32) for hd in hds]
    fit = colour_fit(np.concatenate([sm[mk] for sm, mk in zip(small, masks)]),
                     np.concatenate([pt[mk] for pt, mk in zip(ports, masks)]).astype(np.float32))
    opaque = all(pt[..., 3].min() >= 250 for pt in ports)
    weak = outliers
    row = {'hash': ' '.join(hashes), 'mode': mode, 'size': f'{W}x{H}', 'frame': f'{fw}x{fh}', 'frames': nf,
           'scale': ko, 'weak_frames': weak, 'colour': ' '.join(f'{s:.3f}*x{b:+.1f}' for s, b in fit)}
    for h, hd, mk in zip(hashes, hds, masks):
        big = np.repeat(np.repeat(mk, ko, 0), ko, 1)
        hf = hd.astype(np.float32)
        for c, (s, b) in enumerate(fit):
            hf[..., c] = np.where(big, np.clip(hf[..., c] * s + b, 0, 255), hf[..., c])
        if opaque:
            hf[..., 3] = 255
        err = encode(hf.astype(np.uint8), os.path.join(out, h + '.dds'), opaque, texconv, out)
        if err:
            row['result'] = err
            return row
    row['result'] = 'converted, ' + ('BC1' if opaque else 'BC3')
    return row


def file_orders(game):
    """each game file's textures in order: [[hash, ...], ...]"""
    from xbe_hash import FORMATS, file_textures, level0_size, texture_hash
    files = glob.glob(os.path.join(game, 'image', 'bgs', '*.bgs')) + glob.glob(os.path.join(game, 'image', 'bgs', '*.xpr'))
    out = []
    for path in sorted(files):
        d = open(path, 'rb').read()
        out.append([f'{texture_hash(d[o:o + level0_size(f, w, h)]):016X}' for o, w, h, f, lv in file_textures(d)
                    if f in FORMATS])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dump')
    ap.add_argument('pack')
    ap.add_argument('out')
    ap.add_argument('--texconv', default=shutil.which('texconv') or '')
    ap.add_argument('--jobs', type=int, default=3)
    ap.add_argument('--min-score', type=float, default=0.92)
    ap.add_argument('--only', nargs='*')
    ap.add_argument('--redo', action='store_true', help='convert again atlases already in out')
    ap.add_argument('--game', help='the game folder (with image/): also converts the animations kept one frame a '
                                   'texture, replacing what convert_texture_pack.py made of them one by one')
    args = ap.parse_args()
    if not (args.texconv and os.path.exists(args.texconv)):
        sys.exit('texconv is needed: --texconv path/to/texconv.exe')
    names, sizes, thumbs = pack_index(args.pack, os.path.join(args.out, '.pack_thumbs.npz'))
    tn = normalized(thumbs)
    groups = defaultdict(list)
    for i, s in enumerate(sizes):
        groups[(int(s[0]), int(s[1]))].append(i)
    groups = {s: np.array(v) for s, v in groups.items() if len(v) >= 2}
    tasks, found = [], {}
    for f in sorted(os.listdir(args.dump)):
        if not f.lower().endswith('.png') or '.' in f[:-4]:
            continue
        h = f[:16].upper()
        if args.only and h not in [o.upper() for o in args.only]:
            continue
        if not args.redo and (os.path.exists(os.path.join(args.out, h + '.dds'))
                              or os.path.exists(os.path.join(args.out, h + '.png'))):
            continue
        a = np.asarray(Image.open(os.path.join(args.dump, f)).convert('RGBA'))
        if min(a.shape[:2]) < 128:
            continue
        rgb = np.ascontiguousarray(a[..., :3])
        grid = find_grid(rgb, groups, tn, args.min_score)
        if not grid:
            continue
        fw, fh, k, psize, score = grid
        cells = frames_of(rgb, fw, fh)
        if len(cells) < 2:
            continue
        t0 = thumb(rgb[:fh, :fw])
        idx = groups[psize]
        sc = (tn[idx] * t0).sum(axis=(-1, -2))
        cands = [names[i] for i, s in zip(idx, sc) if s >= args.min_score - 0.1]
        found[h] = (fw, fh, k, psize, cands, cells, score)
    # atlases one after another in a game file, with one grid, are one animation (the river: 4 atlases of 8 frames)
    groups_of, seen = [], set()
    for order in file_orders(args.game) if args.game else []:
        run = []
        for h in order + [None]:
            if h in found and h not in seen and (not run or found[h][:4] == found[run[-1]][:4]):
                run.append(h)
                continue
            if run:
                groups_of.append(run)
                seen.update(run)
            run = [h] if h in found and h not in seen else []
    groups_of += [[h] for h in found if h not in seen]
    for hs in groups_of:
        fw, fh, k, psize, cands, _, score = found[hs[0]]
        cells_of = [found[h][5] for h in hs]
        print(f"{' '.join(hs)}: {sum(map(len, cells_of))} frames of {fw}x{fh} in {len(hs)} atlas(es), pack "
              f"{psize[0]}x{psize[1]} ({len(cands)} candidates, {score:.3f})", flush=True)
        tasks.append((hs, args.dump, args.pack, args.out, args.texconv, fw, fh, k, cands, cells_of))
    set_tasks = []
    in_atlases = set(found)
    for hashes in frame_sets(args.game, args.dump) if args.game else []:
        if in_atlases & set(hashes):
            continue
        if args.only and not set(hashes) & {o.upper() for o in args.only}:
            continue
        t0 = thumb(np.asarray(Image.open(os.path.join(args.dump, hashes[0] + '.png')).convert('RGB')))
        sc = (tn * t0).sum(axis=(-1, -2))
        k = int(sc.argmax())
        if sc[k] < args.min_score:
            continue
        idx = groups.get((int(sizes[k][0]), int(sizes[k][1])))
        if idx is None:
            continue
        cands = [names[i] for i in idx if sc[i] >= args.min_score - 0.1]
        if len(cands) < 2:
            continue
        print(f'{hashes[0]}: animation of {len(hashes)} textures, pack {sizes[k][0]}x{sizes[k][1]} '
              f'({len(cands)} candidates, {sc[k]:.3f})', flush=True)
        set_tasks.append((hashes, args.dump, args.pack, args.out, args.texconv, cands))
    rows = []
    with ProcessPoolExecutor(args.jobs) as ex:
        for row in list(ex.map(convert_set, set_tasks)) + list(ex.map(convert, tasks)):
            rows.append(row)
            print(f"{row['hash']} {row['frames']} frames x{row['scale']} {row.get('mode', '')}: {row['result']}"
                  f"{', ' + str(row['weak_frames']) + ' frame(s) from neighbours' if row['weak_frames'] else ''}",
                  flush=True)
    with open(os.path.join(args.out, 'anim_report.csv'), 'w', newline='') as fh:
        wr = csv.DictWriter(fh, fieldnames=['hash', 'mode', 'size', 'frame', 'frames', 'scale', 'weak_frames', 'colour', 'result'])
        wr.writeheader()
        wr.writerows(rows)
    print(f'{sum(r["result"].startswith("converted") for r in rows)} atlases converted; report in anim_report.csv')


if __name__ == '__main__':
    sys.exit(main())
