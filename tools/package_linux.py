#!/usr/bin/env python3
"""Assemble the native Linux release from an explicit allowlist, never from local game output."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def package(args):
    if args.output.is_symlink():
        raise ValueError('Package output must not be a symlink')
    root = args.output.resolve()
    # Replace only this target's staging directory, never a caller-supplied parent/build root.
    if root.name != 'GenmaOnimusha' or root.parent.name != 'release':
        raise ValueError('Output must be release/GenmaOnimusha')
    if root.is_symlink():
        raise ValueError('Package output must not be a symlink')
    if root.exists():
        shutil.rmtree(root)
    for folder in ('sdk/bin', 'sdk/lib', 'sdk/include', 'sdk/config'):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for src, dest in ((args.launcher, root / 'GenmaOnimusha'),
                      (args.recompiler, root / 'sdk/bin/xbe_recomp'),
                      (args.runtime, root / 'sdk/lib/libGenmaOnimusha_runtime.a'),
                      (args.sdl, root / 'sdk/lib/libSDL3.a'),
                      (args.symbols, root / 'sdk/include/image_symbols.h'),
                      (args.project / 'config/game.toml', root / 'sdk/config/game.toml')):
        shutil.copy2(src, dest)
    for header in (args.sdk / 'runtime').rglob('*.h'):
        dest = root / 'sdk/include' / header.relative_to(args.sdk / 'runtime')
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(header, dest)
    shutil.copytree(args.sdk / 'xdk', root / 'sdk/xdk')
    shutil.copytree(args.dependencies / 'toolchain', root / 'toolchain', symlinks=True)
    shutil.copytree(args.dependencies / 'shader-tools', root / 'shader-tools', symlinks=True)
    # Bundle the loader, not GPU drivers. Its SONAME is also used when linking the locally generated game.
    loaders = list((root / 'toolchain').glob('**/libvulkan.so.1.*'))
    if len(loaders) != 1:
        raise RuntimeError('Expected one pinned Vulkan loader')
    shutil.copy2(loaders[0], root / 'sdk/lib/libvulkan.so.1')
    compile_args = ['-std=gnu++20', '-O2', '-march=x86-64-v2', '-DNDEBUG', '-fno-strict-aliasing',
                    '-Wno-unused-label', '-Wno-parentheses-equality', '-Wno-unused-variable']
    link_args = ['-fuse-ld=lld', '-static-libstdc++', '-static-libgcc', '-pthread', '-ldl', '-lm']
    for name, flags in (('compile.args', compile_args), ('link.args', link_args)):
        (root / 'sdk' / name).write_text('\n'.join(flags) + '\n')
    shutil.copy2(args.dependencies / 'dependencies.lock.json', root / 'dependencies.lock.json')
    for file in ('LICENSE', 'THIRD_PARTY.txt'):
        shutil.copy2(args.sdk / file, root / file)
    shutil.copy2(args.sdk / 'LICENSE', root / 'sdk/LICENSE')
    shutil.copy2(args.sdk / 'THIRD_PARTY.txt', root / 'sdk/THIRD_PARTY.txt')
    shutil.copy2(args.project / 'docs/linux-release.md', root / 'README.md')
    identity = hashlib.sha256((args.version + args.commit + sha(args.runtime) + sha(root / 'dependencies.lock.json') +
                               '\n'.join(compile_args + link_args)).encode()).hexdigest()
    (root / 'sdk/build-id.txt').write_text(identity + '\n')
    manifest = dict(schema=1, name='GenmaOnimusha', platform='linux-x86_64', maturity='experimental',
                    version=args.version, commit=args.commit, build_id=identity, baseline='glibc 2.35 / x86-64-v2',
                    runtime_sha256=sha(args.runtime), dependencies_sha256=sha(root / 'dependencies.lock.json'))
    manifest['files'] = {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()}
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    subprocess.run([str(root / 'GenmaOnimusha'), '--check-install'], check=True)
    print('Linux package staged:', root)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'launcher', 'recompiler', 'runtime', 'sdl', 'symbols', 'sdk', 'project', 'dependencies'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('version', 'commit'):
        parser.add_argument('--' + name, required=True)
    package(parser.parse_args())
