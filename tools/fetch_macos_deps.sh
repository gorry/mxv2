#!/bin/bash
# ローカルビルドと CI が同じ版を使う。取得物は Git 管理外の third_party/。
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
mkdir -p third_party
deps_temp=$(mktemp -d)
trap 'rm -rf "$deps_temp"' EXIT

fetch_git() {
	local name=$1 url=$2 revision=$3
	local destination="third_party/$name"
	if [[ -d "$destination" ]]; then
		if [[ $(git -C "$destination" rev-parse HEAD 2>/dev/null || true) != "$revision" ]]; then
			echo "$destination already exists with a different revision; move it aside or prepare dependencies manually." >&2
			exit 1
		fi
		return
	fi
	git init -q "$deps_temp/$name"
	git -C "$deps_temp/$name" fetch --depth 1 "$url" "$revision"
	git -C "$deps_temp/$name" checkout -q --detach FETCH_HEAD
	mv "$deps_temp/$name" "$destination"
}

fetch_git portable_mdx https://github.com/gorry/portable_mdx.git 4a1e0f66f9bcbacc39ea70d9cc838924c9f0d641
fetch_git imgui https://github.com/ocornut/imgui.git 9a5d5c45f54b1301ea471622eddede70384243af

if [[ ! -d third_party/SDL2-2.32.10-src ]]; then
	sdl_archive="$deps_temp/SDL2-2.32.10.tar.gz"
	sdl_url=https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
	if command -v ax >/dev/null 2>&1; then
		ax "$sdl_url" -f -o "$sdl_archive"
	else
		curl --fail --location --retry 3 --output "$sdl_archive" "$sdl_url"
	fi
	printf '%s  %s\n' 5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165 "$sdl_archive" | shasum -a 256 -c -
	tar -xzf "$sdl_archive" -C "$deps_temp"
	mv "$deps_temp/SDL2-2.32.10" third_party/SDL2-2.32.10-src
fi
echo 'macOS dependencies are ready (SDL2 2.32.10, ImGui 1.92.4, pinned gorry/portable_mdx).'
