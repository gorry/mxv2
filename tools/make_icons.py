# mxv2 - アイコンを各プラットフォーム向けに書き出す
#
# 元画像は pic/icon_mxv2.png（1024x1024 の正方形、フルブリード）。
# pic/ は .gitignore で外してあるので、**ここで作った書き出しのほうを
# リポジトリに入れる**。元画像を描き直したときだけ、これを流し直す。
#
#   使い方: python tools/make_icons.py            （リポジトリの根で）
#           python tools/make_icons.py <元画像>
#
#   要るもの: Pillow (pip install pillow)
#
# 書き出すもの:
#   res/mxv2.ico                                Windows。実行ファイルへ埋める
#   android/.../mipmap-*/ic_launcher.png        Android。API 25 まではこれ
#   android/.../mipmap-*/ic_launcher_foreground.png
#   android/.../mipmap-anydpi-v26/ic_launcher.xml
#   android/.../values/ic_launcher_background.xml
#
# **Android のアダプティブアイコンについて**: 画布は 108dp だが、ランチャーが
# 見せるのは中央 72dp だけで、そこへ円・角丸・しずくなどの型が掛かる。
# 元画像は端まで絵があり、文字も横いっぱいなので、**絵は中央 72dp に収める**
# （＝画布の 2/3）。こうすると角丸の型ならほぼ全部、円の型でも四隅が落ちる
# だけで済む。余白には元画像の縁の色を敷く。

import os
import sys

from PIL import Image, ImageFilter

# --- 出力先 -----------------------------------------------------------------

ICO_PATH = os.path.join("res", "mxv2.ico")
ANDROID_RES = os.path.join("android", "app", "src", "main", "res")

# Windows のアイコンに入れる大きさ。エクスプローラは 16/32/48、
# 「特大アイコン」表示と Alt+Tab で 256 を使う。
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]

# Android の密度と、1dp あたりの画素数。
DENSITIES = [
    ("mdpi", 1.0),
    ("hdpi", 1.5),
    ("xhdpi", 2.0),
    ("xxhdpi", 3.0),
    ("xxxhdpi", 4.0),
]

# 昔ながらの（API 25 まで）アイコンの大きさ (dp)。
LEGACY_DP = 48

# アダプティブアイコンの画布と、その中で絵に使う大きさ (dp)。
ADAPTIVE_CANVAS_DP = 108
ADAPTIVE_ART_DP = 72


def edge_color(im):
    """縁の色。アダプティブアイコンの下地に敷く。"""
    rgb = im.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    total = [0, 0, 0]
    count = 0
    for x in range(0, w, 4):
        for y in (0, 1, h - 2, h - 1):
            p = px[x, y]
            total[0] += p[0]
            total[1] += p[1]
            total[2] += p[2]
            count += 1
    for y in range(0, h, 4):
        for x in (0, 1, w - 2, w - 1):
            p = px[x, y]
            total[0] += p[0]
            total[1] += p[1]
            total[2] += p[2]
            count += 1
    return tuple(v // count for v in total)


def downscale(im, n):
    """n x n へ縮める。**大きく縮めるときは軽く輪郭を立てる。**

    元画像は 1024 で、そこから 1/4 以下へ落とすと文字も鍵盤も潰れて
    「ざらついた四角」になる。アンシャープマスクを軽く掛けると、
    24px でも "MXV2" が読めるところまで戻る。掛けすぎると縁が白く
    縁取られるので percent は控えめにしてある。
    """
    out = im.resize((n, n), Image.LANCZOS)
    if n * 2 <= im.size[0]:
        out = out.filter(ImageFilter.UnsharpMask(radius=1.0, percent=80, threshold=0))
    return out


def ensure_dir(path):
    d = os.path.dirname(path)
    if d and not os.path.isdir(d):
        os.makedirs(d)


def write_text(path, text):
    ensure_dir(path)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", path)


def save_png(im, path):
    ensure_dir(path)
    im.save(path, "PNG", optimize=True)
    print("wrote", path, im.size)


def main():
    src_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("pic", "icon_mxv2.png")
    if not os.path.isfile(src_path):
        print("error: 元画像が無い: %s" % src_path)
        return 1

    src = Image.open(src_path).convert("RGBA")
    if src.size[0] != src.size[1]:
        print("warning: %dx%d は正方形ではない" % src.size)

    bg = edge_color(src)
    print("source %s %s / background #%02X%02X%02X" % (src_path, src.size, bg[0], bg[1], bg[2]))

    # ---- Windows -----------------------------------------------------------
    # Pillow の ICO 書き出しは sizes を渡すと自分で縮めてしまい、輪郭を
    # 立てる余地が無い。**いちばん大きいものを downscale で作り、残りは
    # append_images で 1 枚ずつ渡す。**
    ensure_dir(ICO_PATH)
    frames = [downscale(src, n) for n in sorted(ICO_SIZES)]
    frames[-1].save(ICO_PATH, format="ICO", append_images=frames[:-1],
                    sizes=[(n, n) for n in sorted(ICO_SIZES)])
    print("wrote", ICO_PATH, sorted(ICO_SIZES))

    # ---- Android -----------------------------------------------------------
    for name, scale in DENSITIES:
        # 昔ながらのアイコン。型は掛からないので元画像そのまま。
        n = int(round(LEGACY_DP * scale))
        save_png(downscale(src, n),
                 os.path.join(ANDROID_RES, "mipmap-" + name, "ic_launcher.png"))

        # アダプティブアイコンの前景。画布の中央 2/3 に絵を置く。
        canvas = int(round(ADAPTIVE_CANVAS_DP * scale))
        art = int(round(ADAPTIVE_ART_DP * scale))
        fg = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        off = (canvas - art) // 2
        fg.paste(downscale(src, art), (off, off))
        save_png(fg, os.path.join(ANDROID_RES, "mipmap-" + name, "ic_launcher_foreground.png"))

    adaptive = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
        '\t<background android:drawable="@color/ic_launcher_background" />\n'
        '\t<foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
        "</adaptive-icon>\n"
    )
    # android:roundIcon（API 25 以下の丸いランチャー向け）は持たない。
    # 26 以上はこのアダプティブアイコンが円の型も面倒を見る。
    write_text(os.path.join(ANDROID_RES, "mipmap-anydpi-v26", "ic_launcher.xml"), adaptive)

    write_text(
        os.path.join(ANDROID_RES, "values", "ic_launcher_background.xml"),
        '<?xml version="1.0" encoding="utf-8"?>\n'
        "<resources>\n"
        "\t<!-- 元画像の縁の色。tools/make_icons.py が書き出す。 -->\n"
        '\t<color name="ic_launcher_background">#%02X%02X%02X</color>\n'
        "</resources>\n" % bg,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
