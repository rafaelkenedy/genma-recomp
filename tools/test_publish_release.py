import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from publish_release import verify, publish, sha, gh


class Publication(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tag, self.commit = 'v1.2.3', 'a' * 40
        for platform, name, suffix in [('win64', 'windows', '-win64.zip'), ('linux-x86_64', 'linux', '-linux-x86_64.tar.gz')]:
            path = self.root / ('GenmaOnimusha-' + self.tag + suffix)
            path.write_bytes(b'synthetic package ' + platform.encode())
            metadata = dict(schema=1, platform=platform, version=self.tag, commit=self.commit,
                            file=path.name, size=path.stat().st_size, sha256=sha(path))
            (self.root / (name + '-manifest.json')).write_text(json.dumps(metadata))

    def test_mixed_commits_never_reach_github(self):
        path = self.root / 'linux-manifest.json'
        value = json.loads(path.read_text()); value['commit'] = 'b' * 40; path.write_text(json.dumps(value))
        with patch('publish_release.gh') as api, self.assertRaises(ValueError):
            publish(self.root, self.tag, self.commit, 'owner/repo')
        api.assert_not_called()

    def test_modified_archive_is_rejected(self):
        next(self.root.glob('*.zip')).write_bytes(b'different bytes')
        with self.assertRaises(ValueError):
            verify(self.root, self.tag, self.commit)

    def test_single_draft_publish_and_identical_retry(self):
        release = None
        calls = []
        def api(*args, **kwargs):
            nonlocal release
            calls.append(args)
            self.assertEqual(args[0], 'api')
            endpoint = args[1]
            method = args[args.index('--method') + 1] if '--method' in args else 'GET'
            if endpoint == 'graphql':
                return dict(data=dict(repository=dict(release=dict(databaseId=123) if release else None)))
            if endpoint.endswith('/generate-notes'):
                return dict(body='Generated changelog')
            if endpoint == 'repos/owner/repo/releases':
                self.assertEqual(method, 'POST')
                self.assertIsNone(release)
                self.assertIs(kwargs['payload']['draft'], True)
                release = dict(id=123, tag_name=self.tag, draft=True, assets=[])
            elif endpoint.startswith('https://uploads.github.com/repos/owner/repo/releases/123/assets?'):
                self.assertEqual(method, 'POST')
                self.assertTrue(release['draft'])
                path = Path(args[args.index('--input') + 1])
                release['assets'].append(dict(name=path.name, size=path.stat().st_size, digest='sha256:'+sha(path)))
            else:
                self.assertEqual(endpoint, 'repos/owner/repo/releases/123')
                if method == 'PATCH':
                    self.assertEqual(kwargs['payload'], dict(draft=False))
                    self.assertEqual(len(release['assets']), 4)
                    release['draft'] = False
            return copy.deepcopy(release)
        with patch('publish_release.gh', side_effect=api):
            publish(self.root, self.tag, self.commit, 'owner/repo')
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertEqual(sum(c[1] == 'repos/owner/repo/releases' for c in calls), 1)
        self.assertEqual(sum(c[1].startswith('https://uploads.') for c in calls), 4)
        self.assertEqual(sum('PATCH' in c for c in calls), 1)
        self.assertEqual(len(release['assets']), 4)
        self.assertTrue(any(c[:2] == ('api', 'repos/owner/repo/releases/123') for c in calls))
        self.assertEqual(sum(c[1] == 'graphql' for c in calls), 2)

    def test_existing_draft_is_resumed_without_recreating_it(self):
        release = dict(id=456, tag_name=self.tag, draft=True, assets=[])
        path = next(self.root.glob('*.zip'))
        release['assets'].append(dict(name=path.name, size=path.stat().st_size, digest='sha256:'+sha(path)))
        calls = []
        def api(*args, **kwargs):
            calls.append(args)
            self.assertEqual(args[0], 'api')
            if args[1] == 'graphql':
                return dict(data=dict(repository=dict(release=dict(databaseId=456))))
            if args[1].startswith('https://uploads.github.com/repos/owner/repo/releases/456/assets?'):
                path = Path(args[args.index('--input') + 1])
                release['assets'].append(dict(name=path.name, size=path.stat().st_size, digest='sha256:'+sha(path)))
            else:
                self.assertEqual(args[1], 'repos/owner/repo/releases/456')
                if 'PATCH' in args:
                    self.assertEqual(len(release['assets']), 4)
                    release['draft'] = False
            return copy.deepcopy(release)
        with patch('publish_release.gh', side_effect=api):
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertFalse(release['draft'])
        self.assertEqual(len(release['assets']), 4)
        self.assertEqual(sum(c[1].startswith('https://uploads.') for c in calls), 3)
        self.assertTrue(any(c[:2] == ('api', 'repos/owner/repo/releases/456') for c in calls))

    def test_existing_different_asset_is_not_replaced(self):
        path = next(self.root.glob('*.zip'))
        release = dict(id=123, tag_name=self.tag, draft=True, assets=[dict(name=path.name, size=1, digest='sha256:'+'0'*64)])
        lookup = dict(data=dict(repository=dict(release=dict(databaseId=123))))
        with patch('publish_release.gh', side_effect=[lookup, release]) as api, self.assertRaises(ValueError):
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertEqual(api.call_count, 2)

    def test_new_draft_does_not_need_to_be_visible_in_lookup(self):
        release = dict(id=789, tag_name=self.tag, draft=True, assets=[])
        calls = []
        def api(*args, **kwargs):
            calls.append(args)
            if args[1] == 'graphql':
                return dict(data=dict(repository=dict(release=None)))
            if args[1].endswith('/generate-notes'):
                return dict(body='Notes')
            if args[1] == 'repos/owner/repo/releases':
                return copy.deepcopy(release)
            if args[1].startswith('https://uploads.github.com/repos/owner/repo/releases/789/assets?'):
                path = Path(args[args.index('--input') + 1])
                release['assets'].append(dict(name=path.name, size=path.stat().st_size, digest='sha256:'+sha(path)))
            else:
                self.assertEqual(args[1], 'repos/owner/repo/releases/789')
                if 'PATCH' in args: release['draft'] = False
            return copy.deepcopy(release)
        with patch('publish_release.gh', side_effect=api):
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertFalse(release['draft'])
        self.assertEqual(sum(c[1] == 'graphql' for c in calls), 1)

    def test_graphql_error_is_not_treated_as_absent_release(self):
        with patch('publish_release.gh', return_value=dict(errors=[dict(message='Denied')])) as api:
            with self.assertRaisesRegex(RuntimeError, 'lookup failed'):
                publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertEqual(api.call_count, 1)

    def test_uploaded_hash_mismatch_never_publishes(self):
        release = dict(id=456, tag_name=self.tag, draft=True, assets=[])
        lookup = dict(data=dict(repository=dict(release=dict(databaseId=456))))
        # Four uploads succeed, but the ID lookup still reports no valid assets.
        with patch('publish_release.gh', side_effect=[lookup, release, {}, {}, {}, {}, release]) as api:
            with self.assertRaisesRegex(ValueError, 'Uploaded asset verification failed'):
                publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertFalse(any('PATCH' in c.args for c in api.call_args_list))

    def test_json_request_preserves_boolean_types(self):
        with patch('publish_release.subprocess.run') as run:
            run.return_value.returncode = 0
            run.return_value.stdout = '{"draft": false}'
            self.assertEqual(gh('api', 'endpoint', '--method', 'PATCH', payload=dict(draft=False)), dict(draft=False))
        self.assertEqual(json.loads(run.call_args.kwargs['input']), dict(draft=False))
        self.assertEqual(run.call_args.args[0][-2:], ['--input', '-'])


if __name__ == '__main__': unittest.main()
