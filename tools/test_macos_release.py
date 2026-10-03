#!/usr/bin/env python3
"""秘密鍵や実際の Apple/GitHub API を使わず、公開防止と失敗時の後始末を検証。"""
import base64
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
from urllib.parse import parse_qs, urlsplit

import macos_draft_release as release

SHA = 'a' * 40
TAG = 'macos-test'
TOOLS = Path(__file__).resolve().parent


class IsolatedEnvironmentTests(unittest.TestCase):
    def setUp(self):
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        # runner の本物の summary を引き継ぐと合成 Draft 情報が書かれてしまう。
        os.environ.pop('GITHUB_STEP_SUMMARY', None)
        self.test_stdout = io.StringIO()
        capture = contextlib.redirect_stdout(self.test_stdout)
        capture.__enter__()
        self.addCleanup(capture.__exit__, None, None, None)


class ReleaseApiMock:
    def __init__(self):
        self.release = dict(id=42, tag_name=TAG, name='mxv2 macOS ' + TAG, draft=True,
                            target_commitish=SHA, html_url='https://example.test/draft', assets=[],
                            upload_url='https://uploads.github.com/repos/test/repo/releases/42/assets{?name,label}')
        self.payload = None
        self.uploads = []
        self.deleted = []
        self.list_calls = 0
        self.publish_on_get = False
        self.upload_state = 'uploaded'

    def __call__(self, *arguments):
        if arguments[0] != 'api':
            raise AssertionError('Release operations must use API IDs, not tag-based CLI commands')
        endpoint = arguments[1]
        method = arguments[arguments.index('--method') + 1] if '--method' in arguments else 'GET'
        headers = [arguments[index + 1] for index, argument in enumerate(arguments) if argument == '--header']
        if endpoint == 'repos/{owner}/{repo}/releases?per_page=100':
            self.list_calls += 1
            # 作成直後も一覧には反映されない状態を再現する。
            return '[[]]'
        if endpoint == 'repos/{owner}/{repo}/releases' and method == 'POST':
            if 'Content-Type: application/json' not in headers:
                raise AssertionError('Release creation must send a JSON body')
            self.payload = json.loads(Path(arguments[arguments.index('--input') + 1]).read_text())
            return json.dumps(self.release)
        if endpoint == 'repos/{owner}/{repo}/releases/42' and method == 'GET':
            if self.publish_on_get:
                self.release['draft'] = False
            return json.dumps(self.release)
        if endpoint.startswith('repos/{owner}/{repo}/releases/assets/') and method == 'DELETE':
            identifier = int(endpoint.rsplit('/', 1)[1])
            self.deleted.append(identifier)
            self.release['assets'] = [asset for asset in self.release['assets'] if asset['id'] != identifier]
            return ''
        if endpoint.startswith('https://uploads.github.com/') and method == 'POST':
            self.assert_upload_path(endpoint)
            path = Path(arguments[arguments.index('--input') + 1])
            if 'Content-Length: ' + str(path.stat().st_size) not in headers:
                raise AssertionError('Asset upload must specify its binary byte length')
            content_type = arguments[arguments.index('--header') + 1]
            self.uploads.append((path.name, content_type))
            asset = dict(id=1000 + len(self.uploads), name=path.name,
                         size=path.stat().st_size, state=self.upload_state)
            self.release['assets'].append(asset)
            return json.dumps(asset)
        raise AssertionError('Unexpected API request: ' + repr(arguments))

    def assert_upload_path(self, endpoint):
        parts = urlsplit(endpoint)
        if parts.path != '/repos/test/repo/releases/42/assets' or not parse_qs(parts.query).get('name'):
            raise AssertionError('Asset was not uploaded to the returned Release ID')


class DraftReleaseTests(IsolatedEnvironmentTests):
    def assets(self, root):
        for arch in ('arm64', 'x86_64'):
            name = f'mxv2-macos-{arch}-notarized.zip'
            data = arch.encode()
            (root / name).write_bytes(data)
            (root / (name + '.sha256')).write_text(hashlib.sha256(data).hexdigest() + '  ' + name + '\n')

    def target(self, item, tag_sha=None):
        responses = [subprocess.CompletedProcess([], 0),
                     subprocess.CompletedProcess([], 0 if tag_sha else 1, stdout=(tag_sha or '') + '\n')]
        with patch.object(release.subprocess, 'run', side_effect=responses), \
                patch.object(release, 'gh', return_value=json.dumps([[item]])):
            return release.check_target(TAG, SHA)

    def test_published_release_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'already published'):
            self.target(dict(tag_name=TAG, draft=False, target_commitish=SHA))

    def test_different_draft_commit_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'different commit'):
            self.target(dict(tag_name=TAG, draft=True, target_commitish='b' * 40))

    def test_different_tag_commit_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Existing tag'):
            self.target(dict(tag_name=TAG, draft=True, target_commitish=SHA), 'b' * 40)

    def test_existing_matching_draft_is_allowed(self):
        item = dict(tag_name=TAG, draft=True, target_commitish=SHA)
        self.assertEqual(self.target(item, SHA), item)

    def test_missing_architecture_prevents_any_github_call(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(release, 'gh') as gh:
            with self.assertRaisesRegex(ValueError, 'Both architectures'):
                release.create_draft(TAG, SHA, Path(temporary))
            gh.assert_not_called()

    def test_tampered_package_prevents_any_github_call(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(release, 'gh') as gh:
            root = Path(temporary)
            self.assets(root)
            (root / 'mxv2-macos-arm64-notarized.zip').write_bytes(b'tampered')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                release.create_draft(TAG, SHA, root)
            gh.assert_not_called()

    def test_new_release_is_created_as_draft_at_exact_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            api = ReleaseApiMock()
            with patch.object(release, 'check_target', return_value=None), \
                    patch.object(release, 'gh', side_effect=api):
                release.create_draft(TAG, SHA, root)
            self.assertTrue(api.payload['draft'])
            self.assertEqual(api.payload['target_commitish'], SHA)
            self.assertEqual(api.payload['tag_name'], TAG)
            self.assertEqual(len(api.uploads), 4)
            self.assertEqual(api.uploads[0][1], 'Content-Type: application/zip')
            self.assertEqual(api.uploads[1][1], 'Content-Type: text/plain')

    def test_untagged_draft_and_delayed_list_use_creation_id(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            api = ReleaseApiMock()
            api.release['tag_name'] = 'untagged-7f3db5af5636476db038'
            api.release['html_url'] = 'https://example.test/releases/tag/' + api.release['tag_name']
            responses = [subprocess.CompletedProcess([], 0), subprocess.CompletedProcess([], 1, stdout='')]
            with patch.object(release.subprocess, 'run', side_effect=responses), \
                    patch.object(release, 'gh', side_effect=api):
                release.create_draft(TAG, SHA, root)
            self.assertEqual(api.list_calls, 1, 'Creation must not depend on listing the new draft')
            self.assertEqual(len(api.uploads), 4)

    def test_existing_untagged_draft_matches_requested_title(self):
        api = ReleaseApiMock()
        api.release['tag_name'] = 'untagged-7f3db5af5636476db038'
        self.assertEqual(self.target(api.release), api.release)

    def test_existing_draft_replaces_only_matching_asset_by_id(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            api = ReleaseApiMock()
            api.release['assets'] = [dict(id=7, name='mxv2-macos-arm64-notarized.zip'),
                                     dict(id=8, name='other-platform.zip')]
            with patch.object(release, 'check_target', return_value=api.release), \
                    patch.object(release, 'gh', side_effect=api):
                release.create_draft(TAG, SHA, root)
            self.assertIsNone(api.payload, 'Existing draft must not be duplicated')
            self.assertEqual(api.deleted, [7])
            self.assertIn('other-platform.zip', [asset['name'] for asset in api.release['assets']])

    def test_draft_published_during_run_is_rejected_before_upload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            api = ReleaseApiMock()
            api.publish_on_get = True
            with patch.object(release, 'check_target', return_value=None), \
                    patch.object(release, 'gh', side_effect=api):
                with self.assertRaisesRegex(ValueError, 'already published'):
                    release.create_draft(TAG, SHA, root)
            self.assertEqual(api.uploads, [])
            self.assertEqual(api.deleted, [])

    def test_incomplete_upload_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            api = ReleaseApiMock()
            api.upload_state = 'starter'
            with patch.object(release, 'check_target', return_value=api.release), \
                    patch.object(release, 'gh', side_effect=api):
                with self.assertRaisesRegex(ValueError, 'incomplete'):
                    release.create_draft(TAG, SHA, root)

    def test_unexpected_upload_host_prevents_sending_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            api = ReleaseApiMock()
            api.release['upload_url'] = 'https://example.test/repos/test/repo/releases/42/assets'
            with patch.object(release, 'check_target', return_value=api.release), \
                    patch.object(release, 'gh', side_effect=api):
                with self.assertRaisesRegex(ValueError, 'Unexpected'):
                    release.create_draft(TAG, SHA, root)
            self.assertEqual(api.uploads, [])
            self.assertEqual(api.deleted, [])

    def test_summary_uses_only_explicit_test_file(self):
        self.assertNotIn('GITHUB_STEP_SUMMARY', os.environ)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.assets(root)
            summary = root / 'test-summary.md'
            api = ReleaseApiMock()
            with patch.dict(os.environ, GITHUB_STEP_SUMMARY=str(summary)), \
                    patch.object(release, 'check_target', return_value=api.release), \
                    patch.object(release, 'gh', side_effect=api):
                release.create_draft(TAG, SHA, root)
            self.assertIn('https://example.test/draft', summary.read_text())
            self.assertIn(SHA, summary.read_text())


# 外部コマンドを差し替えて実際のシェルを実行する。秘密情報は合成値のみ。
MOCK_COMMAND = r'''#!/usr/bin/env python3
import json, os, sys, zipfile
from pathlib import Path
command = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['MOCK_CALLS'], 'a') as out:
    out.write(command + ' ' + args[0] + '\n')
if command == 'security':
    if args[0] == 'create-keychain': Path(args[-1]).touch()
    elif args[0] == 'delete-keychain': Path(args[-1]).unlink(missing_ok=True)
    elif args[0] == 'list-keychains':
        search_list = Path(os.environ['MOCK_SEARCH_LIST'])
        if '-s' in args:
            search_list.write_text(json.dumps(args[args.index('-s') + 1:]))
        else:
            for keychain in json.loads(search_list.read_text()): print('    ' + json.dumps(keychain))
    elif args[0] == 'find-identity':
        print('1) ' + 'A' * 40 + ' "Developer ID Application: CI Test (ABCDEFGHIJ)"')
elif command == 'codesign' and '--sign' in args:
    keychain = args[args.index('--keychain') + 1]
    search_list = json.loads(Path(os.environ['MOCK_SEARCH_LIST']).read_text())
    if keychain not in search_list:
        print('error: The specified item could not be found in the keychain.', file=sys.stderr)
        sys.exit(1)
    sys.exit(int(os.environ.get('MOCK_CODESIGN_EXIT', '0')))
elif command == 'curl':
    Path(args[args.index('--output') + 1]).write_bytes(b'public test certificate')
elif command == 'xcrun':
    if args[:2] == ['notarytool', 'submit']:
        print(json.dumps({'id': '11111111-2222-3333-4444-555555555555',
                          'status': os.environ.get('MOCK_STATUS', 'Accepted')}))
        sys.exit(int(os.environ.get('MOCK_SUBMIT_EXIT', '0')))
    elif args[:2] == ['notarytool', 'log']:
        Path(args[3]).write_text('{"issues": ["synthetic failure"]}')
    elif args[:2] == ['stapler', 'staple']:
        Path(args[2], 'ticket').touch()
    elif args[:2] == ['stapler', 'validate']:
        if not Path(args[2], 'ticket').exists(): sys.exit(1)
elif command == 'ditto':
    app, output = Path(args[-2]), Path(args[-1])
    with zipfile.ZipFile(output, 'w') as archive:
        for file in app.rglob('*'):
            if file.is_file(): archive.write(file, file.relative_to(app.parent))
elif command == 'spctl':
    sys.exit(int(os.environ.get('MOCK_SPCTL_EXIT', '0')))
'''


class NotarizationTests(IsolatedEnvironmentTests):
    def check_flow(self, status='Accepted', submit_exit=0, spctl_exit=0, codesign_exit=0):
        with tempfile.TemporaryDirectory(prefix='mxv2-notary-test-') as temporary:
            root = Path(temporary)
            app = root / 'Test app.app'
            (app / 'Contents/MacOS').mkdir(parents=True)
            (app / 'Contents/MacOS/mxv2').write_bytes(b'test executable')
            mock_bin = root / 'bin'
            mock_bin.mkdir()
            mock = mock_bin / 'mock-command'
            # 現在の Python を使い、実機と CI の双方で同じモックを実行。
            mock.write_text(MOCK_COMMAND.replace('#!/usr/bin/env python3', '#!' + sys.executable, 1))
            mock.chmod(0o700)
            for name in ('security', 'curl', 'codesign', 'xcrun', 'ditto', 'spctl'):
                (mock_bin / name).symlink_to(mock)
            search_list = root / 'search-list.json'
            original_keychains = [str(root / 'Original signing.keychain-db'), str(root / 'login.keychain-db')]
            search_list.write_text(json.dumps(original_keychains))
            env = dict(os.environ, PATH=str(mock_bin) + os.pathsep + os.environ['PATH'],
                       RUNNER_TEMP=str(root), MOCK_CALLS=str(root / 'calls'),
                       MOCK_SEARCH_LIST=str(search_list), MOCK_CODESIGN_EXIT=str(codesign_exit),
                       MACOS_CERTIFICATE_BASE64=base64.b64encode(b'test p12').decode(),
                       MACOS_CERTIFICATE_PASSWORD='synthetic password', APPLE_ID='ci@example.test',
                       APPLE_APP_SPECIFIC_PASSWORD='synthetic app password', APPLE_TEAM_ID='ABCDEFGHIJ',
                       MOCK_STATUS=status, MOCK_SUBMIT_EXIT=str(submit_exit), MOCK_SPCTL_EXIT=str(spctl_exit))
            output = root / 'output'
            result = subprocess.run(['bash', str(TOOLS / 'notarize_macos.sh'), str(app), 'arm64', str(output)],
                                    env=env, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=15)
            self.assertEqual(list(root.glob('mxv2-signing.*')), [], 'Signing material must be removed')
            self.assertEqual(json.loads(search_list.read_text()), original_keychains,
                             'Original keychain search list must be restored')
            archive = output / 'mxv2-macos-arm64-notarized.zip'
            calls = (root / 'calls').read_text()
            if status == 'Accepted' and submit_exit == 0 and spctl_exit == 0 and codesign_exit == 0:
                self.assertEqual(result.returncode, 0, result.stdout)
                with zipfile.ZipFile(archive) as zipped:
                    self.assertIn('Test app.app/ticket', zipped.namelist())
                self.assertTrue(archive.with_name(archive.name + '.sha256').is_file())
                self.assertIn('spctl --assess', calls)
            else:
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(archive.exists(), 'Failed verification must not produce release assets')
                if status != 'Accepted' or submit_exit:
                    self.assertTrue((output / 'notary-log-arm64.json').is_file())
                    self.assertNotIn('xcrun stapler', calls)

    def test_success_packages_stapled_app_and_removes_signing_material(self):
        self.check_flow()

    def test_rejection_does_not_package_app_and_removes_signing_material(self):
        self.check_flow(status='Invalid')

    def test_timeout_does_not_package_app_and_removes_signing_material(self):
        self.check_flow(status='In Progress', submit_exit=1)

    def test_gatekeeper_failure_prevents_package(self):
        self.check_flow(spctl_exit=1)

    def test_signing_failure_restores_search_list_and_removes_keychain(self):
        self.check_flow(codesign_exit=1)


if __name__ == '__main__':
    unittest.main()
