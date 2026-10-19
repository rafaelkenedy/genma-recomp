"""Presses buttons and/or captures the running game's next frame.

usage: python tools/shot.py [BUTTONS [seconds held]] [--wait S] [--name NAME]
  BUTTONS as in src/common/autopilot.cpp, e.g. START, A, DOWN, LS+A
  --wait S  seconds between the press and the capture (default 1)
Writes captures/shots/NAME.png (default: a counter) and prints its path.
"""
import os, sys, time, glob, shutil
from pathlib import Path
from PIL import Image

OUT = Path(__file__).resolve().parent.parent / 'out' / 'build' / 'release'
args = sys.argv[1:]
wait, name, keep = 1.0, None, '--keep' in args
args = [a for a in args if a != '--keep']
if '--wait' in args:
    i = args.index('--wait'); wait = float(args[i + 1]); del args[i:i + 2]
if '--name' in args:
    i = args.index('--name'); name = args[i + 1]; del args[i:i + 2]
if args:
    (OUT / 'cmd').write_text(' '.join(args))
    time.sleep(wait)
caps = OUT / 'captures'
before = set(glob.glob(str(caps / '[0-9]*')))
(OUT / 'capture').write_text('')
deadline = time.time() + 15
new = None
while time.time() < deadline:
    now = set(glob.glob(str(caps / '[0-9]*'))) - before
    done = [d for d in now if os.path.exists(os.path.join(d, 'final.bmp'))]
    if done:
        new = max(done); break
    time.sleep(0.2)
if not new:
    sys.exit('no capture: is the game running, or is it stuck?')
time.sleep(0.3)
shots = caps / 'shots'; shots.mkdir(exist_ok=True)
if not name:
    name = '%03d' % len(list(shots.glob('*.png')))
dst = shots / (name + '.png')
Image.open(os.path.join(new, 'final.bmp')).save(dst)
if keep: print(new)
else: shutil.rmtree(new, ignore_errors=True)
print(dst)
