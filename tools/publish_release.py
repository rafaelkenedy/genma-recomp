#!/usr/bin/env python3
"""Verify both platform artifacts before a single, retry-safe GitHub release publication."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def verify(directory, tag, commit):
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:[.-][A-Za-z0-9.-]+)?', tag):
        raise ValueError('Invalid release tag')
    if not re.fullmatch(r'[a-f0-9]{40}', commit):
        raise ValueError('Invalid commit')
    result = {}
    for metadata_file, platform, suffix in [('windows-manifest.json', 'win64', '-win64.zip'),
                                           ('linux-manifest.json', 'linux-x86_64', '-linux-x86_64.tar.gz')]:
        metadata = json.loads((directory / metadata_file).read_text(encoding='utf-8-sig'))
        expected = 'GenmaOnimusha-' + tag + suffix
        if metadata.get('schema') != 1 or metadata.get('platform') != platform or metadata.get('version') != tag or metadata.get('commit') != commit or metadata.get('file') != expected:
            raise ValueError('Mixed/missing platform version or commit: ' + metadata_file)
        path = directory / expected
        if path.is_symlink() or not path.is_file() or path.stat().st_size != metadata.get('size') or sha(path) != metadata.get('sha256'):
            raise ValueError('Artifact size/SHA-256 mismatch: ' + expected)
        result[platform] = metadata
    return result


def gh(*args, missing_ok=False):
    result = subprocess.run(['gh', *args], capture_output=True, text=True)
    if result.returncode:
        if missing_ok and 'HTTP 404' in result.stderr:
            return None
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout) if result.stdout.strip().startswith('{') else result.stdout


def publish(directory, tag, commit, repository):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Invalid repository')
    metadata = verify(directory, tag, commit)
    combined = dict(schema=1, version=tag, commit=commit, platforms=metadata)
    (directory / 'release-manifest.json').write_text(json.dumps(combined, indent=2, sort_keys=True) + '\n')
    files = [directory / value['file'] for value in metadata.values()] + [directory / 'release-manifest.json']
    (directory / 'SHA256SUMS').write_text(''.join(sha(path) + '  ' + path.name + '\n' for path in files))
    files.append(directory / 'SHA256SUMS')
    endpoint = f'repos/{repository}/releases/tags/{tag}'
    release = gh('api', endpoint, missing_ok=True)
    if release is None:
        gh('release', 'create', tag, '--repo', repository, '--target', commit, '--title', tag,
           '--generate-notes', '--notes', 'Windows x86-64 and experimental native Linux x86-64. Linux updates are manual; see the included README.', '--draft')
        release = gh('api', endpoint)
    existing = {asset['name']: asset for asset in release.get('assets', [])}
    pending = []
    for path in files:
        asset = existing.get(path.name)
        if asset:
            if asset.get('size') != path.stat().st_size or asset.get('digest') != 'sha256:' + sha(path):
                raise ValueError('Existing release asset differs; refusing overwrite: ' + path.name)
        elif not release.get('draft'):
            raise ValueError('Published release is incomplete; refusing an implicit repair: ' + path.name)
        else:
            pending.append(path)
    if pending:
        gh('release', 'upload', tag, *(str(p) for p in pending), '--repo', repository)
    # Do not expose a half-uploaded release to the Windows updater.
    uploaded = gh('api', endpoint)
    assets = {asset['name']: asset for asset in uploaded.get('assets', [])}
    for path in files:
        asset = assets.get(path.name, {})
        if asset.get('size') != path.stat().st_size or asset.get('digest') != 'sha256:' + sha(path):
            raise ValueError('Uploaded asset verification failed: ' + path.name)
    if uploaded.get('draft'):
        gh('release', 'edit', tag, '--repo', repository, '--draft=false')
    print('Verified release:', tag)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    publish(args.directory.resolve(), args.tag, args.commit, os.environ['GH_REPO'])
