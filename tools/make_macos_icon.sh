#!/bin/bash
# 元のアイコンから macOS 標準ツールだけで .icns を作る。
set -euo pipefail
if [[ $# -ne 2 ]]; then
	echo "Usage: $0 source.png output.icns" >&2
	exit 1
fi
icon_source=$1
icon_output=$2
icon_temp=$(mktemp -d)
trap 'rm -rf "$icon_temp"' EXIT
mkdir -p "$icon_temp/mxv2.iconset" "$(dirname "$icon_output")"
for size in 16 32 128 256 512; do
	sips -z "$size" "$size" "$icon_source" --out "$icon_temp/mxv2.iconset/icon_${size}x${size}.png" >/dev/null
	double=$((size * 2))
	sips -z "$double" "$double" "$icon_source" --out "$icon_temp/mxv2.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$icon_temp/mxv2.iconset" -o "$icon_output"
