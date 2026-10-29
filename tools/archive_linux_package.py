#!/usr/bin/env python3
"""Create the tested Linux tarball and its per-platform release metadata."""
import argparse
import gzip
import json
from pathlib import Path
import tarfile
from package_linux import sha


def archive(package, output):
    manifest = json.loads((package / 'manifest.json').read_text())
    version = manifest['version']
    if not version or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_' for c in version):
        raise ValueError('Unsafe package version')
    output.mkdir(parents=True, exist_ok=True)
    target = output / ('GenmaOnimusha-' + version + '-linux-x86_64.tar.gz')
    with target.open('wb') as raw, gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) as gz:
        with tarfile.open(fileobj=gz, mode='w', format=tarfile.PAX_FORMAT) as tar:
            for path in [package] + sorted(package.rglob('*')):
                info = tar.gettarinfo(str(path), str(Path(package.name) / path.relative_to(package)))
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ''
                # DrvFS can report 0777 for all files. Never ship a group/world-writable package.
                if not info.issym():
                    info.mode &= ~0o022
                info.pax_headers = {}
                if path.is_file() and not path.is_symlink():
                    with path.open('rb') as f:
                        tar.addfile(info, f)
                else:
                    tar.addfile(info)
    metadata = dict(schema=1, platform='linux-x86_64', maturity='experimental', version=version,
                    commit=manifest['commit'], file=target.name, size=target.stat().st_size, sha256=sha(target))
    (output / 'linux-manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(target, metadata['sha256'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    archive(args.package.resolve(), args.output.resolve())
