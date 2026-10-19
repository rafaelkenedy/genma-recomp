import sys, struct, os
iso, cmd = sys.argv[1], sys.argv[2]
f = open(iso, 'rb')
MAGIC = b'MICROSOFT*XBOX*MEDIA'
base = None
for off in (0, 0x18300000, 0x0FD90000, 0x2080000):
    f.seek(off + 0x10000); 
    if f.read(20) == MAGIC: base = off; break
assert base is not None, 'no xdvdfs'
f.seek(base + 0x10000 + 20); root_sec, root_size = struct.unpack('<II', f.read(8))
def read_dir(sec, size):
    f.seek(base + sec*2048); data = f.read(size); out = []
    def walk(o):
        if o >= len(data) or data[o:o+2] == b'\xff\xff': return
        l, r, s, sz, attr, nl = struct.unpack_from('<HHIIBB', data, o)
        name = data[o+14:o+14+nl].decode('latin1')
        if l: walk(l*4)
        out.append((name, s, sz, attr))
        if r: walk(r*4)
    walk(0); return out
def ls(sec, size, prefix=''):
    for name, s, sz, attr in read_dir(sec, size):
        p = prefix + '/' + name
        if attr & 0x10: print('D', p); ls(s, sz, p)
        else: print('F %12d %s' % (sz, p))
def get(path, dest):
    sec, size = root_sec, root_size
    parts = path.strip('/').split('/')
    for i, part in enumerate(parts):
        for name, s, sz, attr in read_dir(sec, size):
            if name.lower() == part.lower(): sec, size = s, sz; break
        else: raise SystemExit('not found ' + part)
    f.seek(base + sec*2048); open(dest, 'wb').write(f.read(size))
print('base', hex(base), file=sys.stderr)
if cmd == 'ls': ls(root_sec, root_size)
elif cmd == 'get': get(sys.argv[3], sys.argv[4])
def extract(sec, size, dest):
    os.makedirs(dest, exist_ok=True)
    for name, s, sz, attr in read_dir(sec, size):
        p = os.path.join(dest, name)
        if attr & 0x10: extract(s, sz, p)
        else:
            f.seek(base + s*2048); left = sz
            with open(p, 'wb') as o:
                while left:
                    n = min(left, 1<<24); o.write(f.read(n)); left -= n
if cmd == 'extract': extract(root_sec, root_size, sys.argv[3]); print('done')
