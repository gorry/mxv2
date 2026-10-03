#!/bin/bash
# 署名用の秘密鍵は一時キーチェーンだけに置く。成果物には ZIP / SHA256 のみ。
set -euo pipefail
umask 077
if [[ $# -ne 3 ]]; then
	echo 'Usage: notarize_macos.sh app-path arm64|x86_64 output-directory' >&2
	exit 1
fi
app_path=$1
target_arch=$2
output_dir=$3
[[ -d "$app_path/Contents/MacOS" ]]
case "$target_arch" in arm64|x86_64) ;; *) echo 'Invalid architecture' >&2; exit 1 ;; esac
: "${MACOS_CERTIFICATE_BASE64:?Missing MACOS_CERTIFICATE_BASE64}"
: "${MACOS_CERTIFICATE_PASSWORD:?Missing MACOS_CERTIFICATE_PASSWORD}"
: "${APPLE_ID:?Missing APPLE_ID}"
: "${APPLE_APP_SPECIFIC_PASSWORD:?Missing APPLE_APP_SPECIFIC_PASSWORD}"
: "${APPLE_TEAM_ID:?Missing APPLE_TEAM_ID}"
mkdir -p "$output_dir"
signing_temp=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/mxv2-signing.XXXXXX")
signing_keychain="$signing_temp/signing.keychain-db"
cleanup() {
	# codesign の検索対象はユーザー設定なので、削除前に元の一覧へ戻す。
	if [[ -f "$signing_temp/previous-keychains.txt" ]]; then
		python3 - "$signing_temp/previous-keychains.txt" <<'PY' || echo 'Warning: could not restore keychain search list' >&2
import shlex, subprocess, sys
from pathlib import Path
keychains = shlex.split(Path(sys.argv[1]).read_text())
subprocess.run(['security', 'list-keychains', '-d', 'user', '-s', *keychains], check=True)
PY
	fi
	security delete-keychain "$signing_keychain" >/dev/null 2>&1 || true
	rm -rf "$signing_temp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

security list-keychains -d user > "$signing_temp/previous-keychains.txt"
printf '%s' "$MACOS_CERTIFICATE_BASE64" | base64 --decode > "$signing_temp/certificate.p12"
keychain_password=$(openssl rand -hex 32)
security create-keychain -p "$keychain_password" "$signing_keychain"
security set-keychain-settings -lut 7200 "$signing_keychain"
security unlock-keychain -p "$keychain_password" "$signing_keychain"
# --keychain で identity を指定しても、証明書チェーンの構築には検索一覧が必要。
# 既存キーチェーンを保ちながら一時キーチェーンを先頭へ登録する。
python3 - "$signing_temp/previous-keychains.txt" "$signing_keychain" <<'PY'
import shlex, subprocess, sys
from pathlib import Path
keychains = shlex.split(Path(sys.argv[1]).read_text())
subprocess.run(['security', 'list-keychains', '-d', 'user', '-s', sys.argv[2], *keychains], check=True)
PY
echo 'Importing signing identity and configuring private-key access'
security import "$signing_temp/certificate.p12" -k "$signing_keychain" \
	-P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign -T /usr/bin/security
security set-key-partition-list -S apple-tool:,apple:,codesign: \
	-s -k "$keychain_password" "$signing_keychain" >/dev/null

# p12 に中間証明書が入っていなくても署名の信頼チェーンを構築する。
for certificate in DeveloperIDCA DeveloperIDG2CA; do
	curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
		--output "$signing_temp/$certificate.cer" \
		"https://www.apple.com/certificateauthority/$certificate.cer"
	security import "$signing_temp/$certificate.cer" -k "$signing_keychain"
done
echo 'Checking Developer ID Application identity'
security find-identity -v -p codesigning "$signing_keychain" > "$signing_temp/identities.txt"
signing_identity=$(python3 - "$signing_temp/identities.txt" <<'PY'
import os, re, sys
from pathlib import Path
team = os.environ['APPLE_TEAM_ID']
pattern = r'\b([0-9A-Fa-f]{40}) "Developer ID Application:.* \(' + re.escape(team) + r'\)"'
identities = re.findall(pattern, Path(sys.argv[1]).read_text())
if len(identities) != 1:
    raise SystemExit('Expected one valid Developer ID Application identity for APPLE_TEAM_ID')
print(identities[0])
PY
)
rm "$signing_temp/certificate.p12"
echo 'Signing app with Developer ID Application'
codesign --force --sign "$signing_identity" --keychain "$signing_keychain" \
	--options runtime --timestamp "$app_path"
codesign --verify --strict --verbose=2 "$app_path"

# 公証の認証情報も専用キーチェーンへ保存し、以降は profile 名を渡す。
xcrun notarytool store-credentials mxv2-notary --keychain "$signing_keychain" \
	--apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_SPECIFIC_PASSWORD"
unset MACOS_CERTIFICATE_BASE64 MACOS_CERTIFICATE_PASSWORD APPLE_ID APPLE_APP_SPECIFIC_PASSWORD
submission_zip="$signing_temp/submission.zip"
ditto -c -k --sequesterRsrc --keepParent "$app_path" "$submission_zip"
submission_result=0
xcrun notarytool submit "$submission_zip" --keychain "$signing_keychain" \
	--keychain-profile mxv2-notary --wait --timeout 60m --no-progress --output-format json \
	> "$output_dir/notary-submission-$target_arch.json" || submission_result=$?
python3 - "$output_dir/notary-submission-$target_arch.json" > "$signing_temp/result.txt" <<'PY'
import json, re, sys
from pathlib import Path
result = json.loads(Path(sys.argv[1]).read_text())
request_id = result.get('id', '')
if not re.fullmatch(r'[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}', request_id):
    raise SystemExit('Notarization did not return a submission ID; inspect diagnostics')
print(result.get('status', 'Unknown') + '\t' + request_id)
PY
IFS=$'\t' read -r submission_status submission_id < "$signing_temp/result.txt"
if [[ $submission_result -ne 0 || "$submission_status" != Accepted ]]; then
	xcrun notarytool log "$submission_id" "$output_dir/notary-log-$target_arch.json" \
		--keychain "$signing_keychain" --keychain-profile mxv2-notary || true
	echo "Notarization failed or timed out: $submission_status ($submission_id)" >&2
	exit 1
fi
xcrun stapler staple "$app_path"
xcrun stapler validate "$app_path"
codesign --verify --strict --verbose=2 "$app_path"
spctl --assess --type execute --verbose=2 "$app_path"

# ZIP 自体には staple できない。チケットを添付した .app から作り直す。
archive_name="mxv2-macos-$target_arch-notarized.zip"
ditto -c -k --sequesterRsrc --keepParent "$app_path" "$output_dir/$archive_name"
(
	cd "$output_dir"
	shasum -a 256 "$archive_name" > "$archive_name.sha256"
)
echo "Verified notarized package: $output_dir/$archive_name"
