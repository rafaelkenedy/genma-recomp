#!/usr/bin/env python3
"""Download hash-pinned Ubuntu sysroot/compiler and DXC without installing system packages."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request


def digest(path):
    with open(path, 'rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else _digest(source)


def _digest(source):
    h = hashlib.sha256()
    for data in iter(lambda: source.read(1024 * 1024), b''):
        h.update(data)
    return h.hexdigest()


def obtain(item, cache):
    path = cache / (item['sha256'] + ('.deb' if 'name' in item else '.tar.gz'))
    if not path.exists() or digest(path) != item['sha256']:
        temporary = path.with_suffix('.download')
        print('Downloading', item.get('name', 'DXC'), flush=True)
        with urllib.request.urlopen(item['url'], timeout=120) as response, temporary.open('wb') as output:
            shutil.copyfileobj(response, output)
        if digest(temporary) != item['sha256']:
            raise RuntimeError('SHA-256 mismatch: ' + item['url'])
        temporary.replace(path)
    return path


def executable(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(0o755)


def prepare(lock_path, output, cache):
    lock_bytes = lock_path.read_bytes()
    identity = hashlib.sha256(lock_bytes).hexdigest()
    if (output / 'ready.txt').exists() and (output / 'ready.txt').read_text().strip() == identity:
        print('Pinned Linux dependencies already prepared:', output)
        return
    if output.exists() and any(output.iterdir()):
        raise RuntimeError('Use an empty dependency output directory when changing the lock file')
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    lock = json.loads(lock_bytes.decode('utf-8-sig'))
    items = lock['packages'] + [lock['dxc']]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        files = list(pool.map(lambda item: obtain(item, cache), items))
    toolchain = output / 'toolchain'
    toolchain.mkdir(exist_ok=True)
    for path in files[:-1]:
        subprocess.run(['dpkg-deb', '-x', str(path), str(toolchain)], check=True)
    # Make Debian's absolute symlinks relocatable inside the private sysroot.
    for path in toolchain.rglob('*'):
        if path.is_symlink():
            target = os.readlink(path)
            if target.startswith('/'):
                path.unlink()
                path.symlink_to(os.path.relpath(toolchain / target.lstrip('/'), path.parent))
    # Compiler host dependencies are scoped to the compiler subprocess, never the game/GPU driver.
    host = toolchain / 'host-lib'
    host.mkdir()
    prefixes = ('libLLVM', 'libclang', 'libffi', 'libtinfo', 'libz.', 'libzstd', 'libxml2', 'libicu',
                'liblzma', 'libstdc++', 'libgcc_s', 'libedit', 'libbsd', 'libmd', 'libz3')
    for directory in (toolchain / 'usr/lib/x86_64-linux-gnu', toolchain / 'lib/x86_64-linux-gnu'):
        for path in directory.glob('*.so*'):
            if not path.name.startswith(prefixes):
                continue
            actual = path.resolve()
            if not actual.is_relative_to(toolchain.resolve()):
                raise RuntimeError('Dependency symlink escapes private sysroot: ' + str(path))
            destination = host / actual.name
            if not destination.exists():
                shutil.copy2(actual, destination)
            alias = host / path.name
            if alias.name != destination.name and not alias.exists():
                alias.symlink_to(destination.name)
    for name, driver in (('clang', 'clang'), ('clang++', 'clang++')):
        executable(toolchain / 'bin' / name, '''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export LD_LIBRARY_PATH="$root/host-lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$root/usr/lib/llvm-14/bin/DRIVER" --target=x86_64-linux-gnu --sysroot="$root" --gcc-toolchain="$root/usr" -B"$root/usr/lib/llvm-14/bin" "$@"
'''.replace('DRIVER', driver))
    dxc = output / 'shader-tools'
    dxc.mkdir()
    subprocess.run(['tar', '-xzf', str(files[-1]), '-C', str(dxc)], check=True)
    (dxc / 'libexec').mkdir()
    (dxc / 'bin/dxc').rename(dxc / 'libexec/dxc')
    executable(dxc / 'bin/dxc', '''#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export LD_LIBRARY_PATH="$root/lib:$root/../toolchain/host-lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$root/libexec/dxc" "$@"
''')
    shutil.copy2(lock_path, output / 'dependencies.lock.json')
    subprocess.run([str(toolchain / 'bin/clang++'), '--version'], check=True)
    subprocess.run([str(dxc / 'bin/dxc'), '--version'], check=True)
    (output / 'ready.txt').write_text(identity + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lock', type=Path, default=Path('config/linux-dependencies.lock.json'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=Path('out/downloads/linux'))
    args = parser.parse_args()
    prepare(args.lock.resolve(), args.output.resolve(), args.cache.resolve())
