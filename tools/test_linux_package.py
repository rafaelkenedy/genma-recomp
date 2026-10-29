#!/usr/bin/env python3
"""Asset-free package verification: relocation, bundled compilation, whole-runtime link and XDG isolation."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from package_linux import sha


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, text=True, capture_output=True, **kwargs).stdout


def verify(package):
    print('Checking package allowlist, symlinks and file hashes...', flush=True)
    metadata = json.loads((package / 'manifest.json').read_text())
    assert metadata['platform'] == 'linux-x86_64'
    allowed = {'GenmaOnimusha', 'sdk', 'toolchain', 'shader-tools', 'manifest.json', 'dependencies.lock.json',
               'README.md', 'LICENSE', 'THIRD_PARTY.txt'}
    assert {p.name for p in package.iterdir()} <= allowed
    for path in package.rglob('*'):
        if path.is_symlink():
            assert path.resolve().is_relative_to(package.resolve()), ('External symlink', path)
        if path.is_file():
            assert path.suffix.lower() not in {'.xbe', '.iso', '.exe', '.bmp', '.raw'}, path
    for relative, expected in metadata['files'].items():
        assert sha(package / relative) == expected, relative
    compiler = package / 'toolchain/bin/clang++'
    print('Auditing private compiler/DXC dependencies...', flush=True)
    # A developer machine can mask a missing private DSO, so audit DT_NEEDED as well as running tools.
    private = list((package / 'toolchain/host-lib').glob('*.so*')) + list((package / 'shader-tools/lib').glob('*.so*'))
    provided = {path.name for path in private}
    system = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1', 'ld-linux-x86-64.so.2'}
    binaries = [package / 'toolchain/usr/lib/llvm-14/bin/clang', package / 'shader-tools/libexec/dxc']
    for path in binaries + private:
        needed = re.findall(r'Shared library: \[([^\]]+)\]', run(['readelf', '-d', path]))
        assert set(needed) <= provided | system, (path, set(needed) - provided - system)
    with tempfile.TemporaryDirectory(prefix='genma package ') as temp:
        base = Path(temp)
        # Exercise the toolchain at a new path containing spaces without copying the large immutable sysroot.
        relocated = base / 'relocated tools'
        shutil.copytree(package / 'toolchain/bin', relocated / 'bin', symlinks=True)
        for child in (package / 'toolchain').iterdir():
            if child.name != 'bin':
                (relocated / child.name).symlink_to(child.resolve(), target_is_directory=child.is_dir())
        test = base / 'exception ABI.cpp'
        test.write_text('#include <stdexcept>\n#include <filesystem>\nint main(){try{throw std::runtime_error("ok");}catch(const std::exception& e){return std::string(e.what())=="ok"?0:1;}}\n')
        executable = base / 'fixture'
        run([relocated / 'bin/clang++', '-std=c++20', '-fuse-ld=lld', '-static-libstdc++', '-static-libgcc', test, '-o', executable])
        run([executable])
        print('Relocated compiler and C++ exception ABI passed; linking synthetic runtime...', flush=True)
        # All original guest functions are inert synthetic definitions; no image/code is read to build this fixture.
        runtime = package / 'sdk/lib/libGenmaOnimusha_runtime.a'
        undefined = set(re.findall(r'\bU\s+((?:__imp__)?sub_[0-9A-Fa-f]+)', run(['nm', '-u', runtime])))
        defined = set(re.findall(r'\b[TW]\s+((?:__imp__)?sub_[0-9A-Fa-f]+)', run(['nm', '--defined-only', runtime])))
        source = '#include "cpu/x86_ops.h"\n'
        source += 'const X86FuncMapping X86FuncMappings[]={{0,nullptr}};\nconst uint32_t X86Continuations[]={0};\n'
        source += 'const uint32_t X86CodeBase=0x10000,X86CodeEnd=0x10001,X86ImageBase=0x10000,X86ImageEnd=0x11000,X86EntryPoint=0x10000,X86KernelThunk=0;\n'
        source += ''.join('X86_FUNC('+name+'){ctx.eax=0;}\n' for name in sorted(undefined-defined))
        fixture = base / 'synthetic guest.cpp'; fixture.write_text(source)
        run([compiler, '-std=c++20', '-march=x86-64-v2', '-I'+str(package / 'sdk/include'), fixture,
             '-Wl,--whole-archive', runtime, '-Wl,--no-whole-archive', package / 'sdk/lib/libSDL3.a',
             '-L'+str(package / 'sdk/lib'), '-l:libvulkan.so.1', '-pthread', '-ldl', '-lm',
             '-fuse-ld=lld', '-static-libstdc++', '-static-libgcc', '-o', base / 'runtime-link-fixture'])
        print('Whole-runtime link passed; testing complete read-only relocation...', flush=True)
        # An independent, read-only copy catches accidental writes into the release directory.
        moved = base / 'read-only release Ω' / package.name
        shutil.copytree(package, moved, symlinks=True)
        for path in moved.rglob('*'):
            if not path.is_symlink():
                path.chmod(path.stat().st_mode & ~0o222)
        moved.chmod(0o555)
        env = os.environ.copy()
        for kind in ('CONFIG', 'DATA', 'CACHE'):
            env['XDG_'+kind+'_HOME'] = str(base / ('user '+kind.lower()))
        try:
            run([moved / 'GenmaOnimusha', '--check-install'], env=env)
            paths = run([moved / 'GenmaOnimusha', '--print-paths'], env=env)
            assert str(base / 'user data') in paths and str(package) not in paths
            result = subprocess.run([str(moved / 'GenmaOnimusha')], env=env, capture_output=True, text=True)
            assert result.returncode != 0 and 'Supply your copy' in result.stderr
            assert not (moved / 'work').exists() and not (moved / 'game').exists()
        finally:
            moved.chmod(0o755)
            for path in moved.rglob('*'):
                if path.is_dir() and not path.is_symlink():
                    path.chmod(0o755)
        # Detect hidden reliance on the developer's build tree or on LD_LIBRARY_PATH for launcher dependencies.
        libraries = run(['ldd', moved / 'GenmaOnimusha'])
        assert 'not found' not in libraries
        symbols = run(['readelf', '--version-info', moved / 'GenmaOnimusha'])
        versions = [tuple(map(int, value.split('.'))) for value in re.findall(r'GLIBC_([0-9.]+)', symbols)]
        assert all(version <= (2, 35) for version in versions), versions
    print('PASS: hashes/no game assets, relocated bundled compiler, C++ exceptions, whole-runtime synthetic link, XDG paths and launcher glibc baseline')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    args = parser.parse_args()
    try:
        verify(args.package.resolve())
    except subprocess.CalledProcessError as error:
        print(error.stdout, error.stderr)
        raise
