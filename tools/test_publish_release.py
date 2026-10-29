import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from publish_release import verify, publish, sha


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
            if args[0] == 'api':
                self.assertNotIn('/releases/tags/', args[1])
                return release
            if args[:2] == ('release', 'create'): release = dict(id=123, draft=True, assets=[])
            if args[:2] == ('release', 'upload'):
                for item in args[3:args.index('--repo')]:
                    path = Path(item); release['assets'].append(dict(name=path.name, size=path.stat().st_size, digest='sha256:'+sha(path)))
            if args[:2] == ('release', 'edit'): release['draft'] = False
            return ''
        with patch('publish_release.gh', side_effect=api):
            publish(self.root, self.tag, self.commit, 'owner/repo')
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertEqual(sum(c[:2] == ('release', 'create') for c in calls), 1)
        self.assertEqual(sum(c[:2] == ('release', 'upload') for c in calls), 1)
        self.assertEqual(sum(c[:2] == ('release', 'edit') for c in calls), 1)
        self.assertEqual(len(release['assets']), 4)
        self.assertTrue(any(c[:2] == ('api', 'repos/owner/repo/releases/123') for c in calls))
        self.assertTrue(any('--paginate' in c and '--jq' in c for c in calls))

    def test_existing_draft_is_resumed_without_recreating_it(self):
        release = dict(id=456, draft=True, assets=[])
        calls = []
        def api(*args, **kwargs):
            calls.append(args)
            if args[0] == 'api':
                self.assertNotIn('/releases/tags/', args[1])
                return release
            if args[:2] == ('release', 'create'):
                self.fail('An existing draft must be reused')
            if args[:2] == ('release', 'upload'):
                for item in args[3:args.index('--repo')]:
                    path = Path(item)
                    release['assets'].append(dict(name=path.name, size=path.stat().st_size, digest='sha256:'+sha(path)))
            if args[:2] == ('release', 'edit'): release['draft'] = False
            return ''
        with patch('publish_release.gh', side_effect=api):
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertFalse(release['draft'])
        self.assertEqual(len(release['assets']), 4)
        self.assertTrue(any(c[:2] == ('api', 'repos/owner/repo/releases/456') for c in calls))

    def test_existing_different_asset_is_not_replaced(self):
        path = next(self.root.glob('*.zip'))
        release = dict(id=123, draft=True, assets=[dict(name=path.name, size=1, digest='sha256:'+'0'*64)])
        with patch('publish_release.gh', return_value=release) as api, self.assertRaises(ValueError):
            publish(self.root, self.tag, self.commit, 'owner/repo')
        self.assertEqual(api.call_count, 1)


if __name__ == '__main__': unittest.main()
