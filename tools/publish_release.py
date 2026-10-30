#!/usr/bin/env python3
"""Verify both platform artifacts before a single, retry-safe GitHub release publication."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import urlencode


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


def gh(*args, missing_ok=False, payload=None):
    if payload is not None:
        args = (*args, '--input', '-')
    result = subprocess.run(['gh', *args], capture_output=True, text=True,
                            input=json.dumps(payload) if payload is not None else None)
    if result.returncode:
        if missing_ok and 'HTTP 404' in result.stderr:
            return None
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout) if result.stdout.strip().startswith('{') else result.stdout


def prepare_files(directory, tag, commit):
    metadata = verify(directory, tag, commit)
    combined = dict(schema=1, version=tag, commit=commit, platforms=metadata)
    (directory / 'release-manifest.json').write_text(json.dumps(combined, indent=2, sort_keys=True) + '\n')
    files = [directory / value['file'] for value in metadata.values()] + [directory / 'release-manifest.json']
    (directory / 'SHA256SUMS').write_text(''.join(sha(path) + '  ' + path.name + '\n' for path in files))
    files.append(directory / 'SHA256SUMS')
    return files


def find_release(repository, tag):
    # Actions tokens can create drafts that REST tag/list lookups omit.
    # Resolve the pending tag with GraphQL, as gh release does, then use its ID.
    owner, name = repository.split('/')
    result = gh('api', 'graphql', '-f', 'query=' + '''
        query($owner: String!, $name: String!, $tag: String!) {
          repository(owner: $owner, name: $name) {
            release(tagName: $tag) { databaseId }
          }
        }''', '-f', 'owner=' + owner, '-f', 'name=' + name, '-f', 'tag=' + tag)
    if result.get('errors'):
        raise RuntimeError('GraphQL release lookup failed')
    repo = result['data']['repository']
    if repo is None:
        raise ValueError('Repository unavailable during release lookup')
    release = repo['release']
    return gh('api', f'repos/{repository}/releases/{release["databaseId"]}') if release else None


def publish(directory, tag, commit, repository):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Invalid repository')
    files = prepare_files(directory, tag, commit)
    release = find_release(repository, tag)
    if release is None:
        notes = gh('api', f'repos/{repository}/releases/generate-notes', '--method', 'POST',
                   payload=dict(tag_name=tag, target_commitish=commit))
        release = gh('api', f'repos/{repository}/releases', '--method', 'POST', payload=dict(
            tag_name=tag, target_commitish=commit, name=tag, draft=True,
            body='Windows x86-64 and experimental native Linux x86-64. Linux updates are manual; see the included README.\n\n' + notes['body']))
    if release.get('tag_name') != tag:
        raise ValueError('Release tag differs from requested tag')
    endpoint = f'repos/{repository}/releases/{release["id"]}'
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
    for path in pending:
        url = f'https://uploads.github.com/{endpoint}/assets?' + urlencode(dict(name=path.name))
        gh('api', url, '--method', 'POST', '--header', 'Content-Type: application/octet-stream',
           '--input', str(path))
    # Do not expose a half-uploaded release to the Windows updater.
    uploaded = gh('api', endpoint)
    assets = {asset['name']: asset for asset in uploaded.get('assets', [])}
    for path in files:
        asset = assets.get(path.name, {})
        if asset.get('size') != path.stat().st_size or asset.get('digest') != 'sha256:' + sha(path):
            raise ValueError('Uploaded asset verification failed: ' + path.name)
    if uploaded.get('draft'):
        uploaded = gh('api', endpoint, '--method', 'PATCH', payload=dict(draft=False))
        if uploaded.get('draft'):
            raise ValueError('Release remained a draft after publication')
    print('Verified release:', tag)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    publish(args.directory.resolve(), args.tag, args.commit, os.environ['GH_REPO'])
