#!/usr/bin/env python3
""".app の自己完結性と実際の SDL/MDX 演奏を、GUI 操作なしで検証する。"""
import argparse
import os
from pathlib import Path
import plistlib
import re
import shutil
import struct
import subprocess
import tempfile


def make_mdx(pdx_name=b""):
    # 再配布音源を使わない。1 チャンネルの短い FM テスト音と終端のみ。
    track = bytes([0xFD, 0, 0xFC, 3, 0xFB, 15, 0xB0, 47, 0xF1, 0])
    end = bytes([0xF1, 0])
    voice = bytes([0, 7, 15] + [1] * 4 + [32] * 4 + [31] * 4
                  + [0] * 8 + [15] * 4)
    offsets = [20 + len(track) + 8 * len(end), 20]
    offsets += [20 + len(track) + i * len(end) for i in range(8)]
    return (b"macOS smoke test\r\n\x1a" + pdx_name + b"\0" + struct.pack(">10H", *offsets)
            + track + end * 8 + voice)


def run(command, **kwargs):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=30, **kwargs)
    print(result.stdout)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    parser.add_argument("chunktest", type=Path)
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    chunktest = args.chunktest.resolve()
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    assert info["CFBundlePackageType"] == "APPL"
    assert "mdx" in info["CFBundleDocumentTypes"][0]["CFBundleTypeExtensions"]
    for resource in ("NOTICE", "LICENSE", "SDL2-LICENSE.txt", "mxv2.icns",
                     "assets/MPLUS1p-Regular.ttf", "assets/MPLUS1p-OFL.txt",
                     "assets/locale/ja-JP/message.ini", "assets/skin/Default/back.bmp"):
        assert (bundle / "Contents/Resources" / resource).is_file(), resource
    license_text = (bundle / "Contents/Resources/LICENSE").read_text(encoding="utf-8")
    for notice in ("Apache License", "Omar Cornut", "Adrien Descamps", "Alan Ott",
                   "Sun Microsystems", "Yosshin",
                   "SIL OPEN FONT LICENSE"):
        assert notice in license_text, "Missing bundled license notice: " + notice
    binary = bundle / "Contents/MacOS/mxv2"
    linked = run(["otool", "-L", str(binary)])
    for line in linked.splitlines()[1:]:
        library = line.strip().split(" (", 1)[0]
        assert library.startswith(("/usr/lib/", "/System/Library/")), library

    # 別の場所へコピーして起動し、ビルドツリーやカレントディレクトリへの
    # 偶然の依存も検出する。日本語・空白を含むパスで UTF-8 読込みも確認。
    with tempfile.TemporaryDirectory(prefix="mxv2-test-") as temporary:
        root = Path(temporary)
        relocated = root / "移植テスト mxv2.app"
        shutil.copytree(bundle, relocated)
        user = root / "ユーザー設定"
        user.mkdir()
        (user / "mxv2.ini").write_text(
            "[Network]\nUpdateCheck=0\n[Tutorial]\nDone=1\n", encoding="utf-8")
        mdx = root / "テスト音源.mdx"
        mdx.write_bytes(make_mdx())
        chunk_log = run([str(chunktest), str(mdx), "2"], cwd=root)
        assert "samples identical" in chunk_log
        assert int(re.search(r"peak=(\d+)", chunk_log).group(1)) > 0
        # 圧縮 MDX と壊れたヘッダーがエンジンへ渡ってクラッシュしないこと。
        for name, body, error in (
                ("packed", b"\x60\x26\x60\x32LZX 0.32" + bytes(20), "Error.MdxPacked"),
                ("truncated", bytes(4), "Error.MdxBody"),
                ("bad-offset", struct.pack(">10H", *([0xFFFF] * 10)), "Error.MdxBody")):
            invalid = root / f"{name}.mdx"
            invalid.write_bytes(b"macOS smoke test\r\n\x1a\0" + body)
            result = subprocess.run([str(chunktest), str(invalid), "1"], cwd=root,
                                    text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=10)
            assert result.returncode == 1 and error in result.stdout, result.stdout
        pdx_mdx = root / "pdx-test.mdx"
        pdx_mdx.write_bytes(make_mdx(b"test.pdx"))
        pdx = root / "test.pdx"
        pdx.write_bytes(bytes(96 * 8))
        assert "samples identical" in run([str(chunktest), str(pdx_mdx), "1"], cwd=root)
        for data, error in (
                (b"\x60\x26\x60\x4aLZX 0.42" + bytes(96 * 8), "Error.PdxPacked"),
                (bytes(8), "Error.PdxBody"),
                (struct.pack(">II", 0xFFFFFFF0, 32) + bytes(95 * 8), "Error.PdxBody")):
            pdx.write_bytes(data)
            result = subprocess.run([str(chunktest), str(pdx_mdx), "1"], cwd=root,
                                    text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=10)
            assert result.returncode == 1 and error in result.stdout, result.stdout
        pcm = root / "audio.raw"
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="disk",
                   SDL_DISKAUDIOFILE=str(pcm))
        log = run([str(relocated / "Contents/MacOS/mxv2"), "-userdir", str(user),
                   "-quit", str(mdx)], cwd=root, env=env)
        assert str(relocated / "Contents/Resources/assets") in log
        assert "title    : macOS smoke test" in log
        assert "audiodrv : disk" in log
        assert "ERROR:" not in log
        # disk audio は実際のデバイスと異なり、並列ビルド・テスト時の
        # スケジューリングで underrun が出る。素材等の警告は引き続き失敗。
        warnings = [line for line in log.splitlines() if "warning  :" in line]
        assert all("warning  : audio underrun " in line for line in warnings), warnings
        assert any(pcm.read_bytes()), "SDL audio output is silent"
        assert (user / "mxv2.ini").is_file()
    print("OK: relocated .app, Japanese paths, bundled assets, identical MDX PCM, SDL audio output, packed/invalid MDX and PDX rejection")


if __name__ == "__main__":
    main()
