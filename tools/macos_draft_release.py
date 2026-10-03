#!/usr/bin/env python3
"""公証版を同じコミットの Draft Release にだけ添付する。gh は env から認証。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlencode, urlsplit


def gh(*arguments):
    return subprocess.check_output(['gh', *arguments], text=True)


def matches_tag(release, tag):
    if release.get('tag_name') == tag:
        return True
    # GitHub の未タグ Draft はプレースホルダー名で返る場合もある。
    return (release.get('draft') is True
            and release.get('tag_name', '').startswith('untagged-')
            and release.get('name') == 'mxv2 macOS ' + tag)


def validate_draft(release, tag, sha):
    if release.get('draft') is not True:
        raise ValueError('Release is already published; it will not be changed')
    if not matches_tag(release, tag):
        raise ValueError('Draft Release does not match the requested tag')
    if release.get('target_commitish') != sha:
        raise ValueError('Existing draft targets a different commit; choose a new tag')


def check_target(tag, sha):
    if tag.startswith('-') or subprocess.run(
            ['git', 'check-ref-format', 'refs/tags/' + tag],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        raise ValueError('Invalid release tag')
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise ValueError('Expected the full build commit SHA')
    tag_commit = subprocess.run(
        ['git', 'rev-parse', '--verify', 'refs/tags/' + tag + '^{commit}'],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if tag_commit.returncode == 0 and tag_commit.stdout.strip() != sha:
        raise ValueError('Existing tag points to a different commit; choose a new tag')
    pages = json.loads(gh('api', 'repos/{owner}/{repo}/releases?per_page=100',
                          '--paginate', '--slurp'))
    matches = [item for page in pages for item in page if matches_tag(item, tag)]
    if len(matches) > 1:
        raise ValueError('Multiple releases match this tag; resolve duplicate drafts first')
    release = matches[0] if matches else None
    if release:
        # 再実行は本ワークフローが作成した同じ SHA の Draft だけを更新する。
        validate_draft(release, tag, sha)
    return release


def release_id(release):
    identifier = release.get('id')
    if type(identifier) is not int or identifier <= 0:
        raise ValueError('GitHub did not return a valid Release ID')
    return identifier


def get_draft(identifier, tag, sha):
    release = json.loads(gh('api', f'repos/{{owner}}/{{repo}}/releases/{identifier}'))
    if release_id(release) != identifier:
        raise ValueError('GitHub returned a different Release ID')
    validate_draft(release, tag, sha)
    return release


def upload_assets(identifier, tag, sha, assets):
    for filename in assets:
        # 作成直後の一覧・タグ照合に依存せず、同じ ID の Draft を確認する。
        release = get_draft(identifier, tag, sha)
        path = Path(filename)
        upload_url = release['upload_url'].split('{', 1)[0]
        parts = urlsplit(upload_url)
        if (parts.scheme != 'https' or parts.netloc != 'uploads.github.com'
                or not parts.path.endswith(f'/releases/{identifier}/assets')
                or parts.query or parts.fragment):
            raise ValueError('Unexpected GitHub release asset upload URL')
        # Draft の同名添付だけを入れ替える。タグ名による gh release upload は使わない。
        for asset in release.get('assets', []):
            if asset['name'] == path.name:
                asset_id = release_id(asset)
                gh('api', f'repos/{{owner}}/{{repo}}/releases/assets/{asset_id}', '--method', 'DELETE')
        content_type = 'application/zip' if path.suffix == '.zip' else 'text/plain'
        result = json.loads(gh('api', upload_url + '?' + urlencode({'name': path.name}),
                               '--method', 'POST', '--header', 'Content-Type: ' + content_type,
                               '--header', 'Content-Length: ' + str(path.stat().st_size),
                               '--input', str(path)))
        if (result.get('name') != path.name or result.get('state') != 'uploaded'
                or result.get('size') != path.stat().st_size):
            raise ValueError('Release asset upload was incomplete: ' + path.name)
    return get_draft(identifier, tag, sha)


def checked_assets(directory):
    assets = []
    for arch in ('arm64', 'x86_64'):
        name = f'mxv2-macos-{arch}-notarized.zip'
        archive = directory / name
        checksum = directory / (name + '.sha256')
        if not archive.is_file() or not checksum.is_file():
            raise ValueError('Both architectures and their checksums are required')
        fields = checksum.read_text().strip().split()
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if fields != [digest, name]:
            raise ValueError('Checksum mismatch: ' + name)
        assets.extend((str(archive), str(checksum)))
    return assets


def create_draft(tag, sha, directory):
    # 全成果物の検証を終えてから GitHub を変更する。
    assets = checked_assets(directory)
    release = check_target(tag, sha)
    if not release:
        notes = (
            f'macOS の Developer ID 署名・公証済みビルド。\n\n'
            f'ソースコミット: `{sha}`\n\n'
            '- Apple Silicon: `mxv2-macos-arm64-notarized.zip`\n'
            '- Intel: `mxv2-macos-x86_64-notarized.zip`\n\n'
            'macOS 11 以降。公証チケットをアプリに添付し、署名・公証・'
            'Gatekeeper の検証を通過したものです。SHA256 ファイルも添付しています。\n\n'
            'Draft のため、内容を確認してから手動で公開してください。\n'
        )
        with tempfile.TemporaryDirectory(prefix='mxv2-release-') as temporary:
            body = Path(temporary) / 'release.json'
            body.write_text(json.dumps(dict(tag_name=tag, target_commitish=sha, draft=True,
                                            name='mxv2 macOS ' + tag, body=notes)), encoding='utf-8')
            # URL だけを返す gh release create ではなく、作成応答の ID を保持する。
            release = json.loads(gh('api', 'repos/{owner}/{repo}/releases',
                                    '--method', 'POST', '--header', 'Content-Type: application/json',
                                    '--input', str(body)))
    validate_draft(release, tag, sha)
    identifier = release_id(release)
    print('Draft Release ID: ' + str(identifier))
    release = upload_assets(identifier, tag, sha, assets)
    print('Updated Draft Release: ' + release['html_url'])
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as stream:
            stream.write(f'Draft Release: {release["html_url"]}\n\nCommit: `{sha}`\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tag', required=True)
    parser.add_argument('--sha', required=True)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--assets', type=Path)
    args = parser.parse_args()
    try:
        if args.check_only:
            check_target(args.tag, args.sha)
            print('Release tag and draft target are valid')
        elif args.assets:
            create_draft(args.tag, args.sha, args.assets)
        else:
            parser.error('--assets is required when creating a draft')
    except (ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None


if __name__ == '__main__':
    main()
