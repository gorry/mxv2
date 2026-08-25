# mxv2 のビルド手順

`third_party/` はリポジトリに含めていない。クローンしたあと、このドキュメントに
従って自分で用意する必要がある。

## 1. 必要なもの

| | 版 | 備考 |
|---|---|---|
| CMake | 3.20 以降 | 開発は 3.31.8 で行っている |
| C++ コンパイラ | C++11 | Windows は Visual Studio 2022 (MSVC 19.44) で開発・検証している |

**動作を確認しているのは Windows / MSVC のみ。** CMake は他のプラットフォーム
向けの記述も持っているが（SDL2 を `find_package` で探す）、まだ試していない。

## 2. third_party/ を用意する

3 つのライブラリを、次のパスに展開する。

```
mxv2/
    third_party/
        SDL2-2.32.10/     SDL2 の VC 開発用パッケージ
        imgui/            Dear ImGui v1.92.4
        portable_mdx/     演奏モジュール
```

いずれも**無改変で置く**。mxv2 側からは参照するだけなので、更新するときは
フォルダごと差し替えればよい。

### SDL2 2.32.10

<https://github.com/libsdl-org/SDL/releases/tag/release-2.32.10> から
**`SDL2-devel-2.32.10-VC.zip`** を取得し、展開して `third_party/SDL2-2.32.10/`
にする。`cmake/` `docs/` `include/` `lib/` が並んでいれば正しい。
ビルドに使われるのは以下。

- `include/`
- `lib/x64/SDL2.lib` `lib/x64/SDL2main.lib` `lib/x64/SDL2.dll`
  （32bit ビルドでは `lib/x86/`）

`SDL2.dll` はビルド後に実行ファイルの隣へ自動でコピーされる。

**フォルダ名にバージョンが入っている**ので、別の版を使うときは
`-DSDL2_ROOT=<パス>` で場所を指定する。ただし **2.0.18 以降が必要**
（Dear ImGui の SDL_Renderer バックエンドが `SDL_RenderGeometry` を使う）。
Windows 以外では `find_package(SDL2)` で探すので、この展開は不要。

### Dear ImGui v1.92.4

<https://github.com/ocornut/imgui> の **v1.92.4** タグを
`third_party/imgui/` へ。

```sh
git clone --depth 1 --branch v1.92.4 https://github.com/ocornut/imgui.git third_party/imgui
```

ビルドに使われるのはコアと SDL2 用バックエンドだけ。

- `imgui.cpp` `imgui_draw.cpp` `imgui_tables.cpp` `imgui_widgets.cpp`
- `backends/imgui_impl_sdl2.cpp` `backends/imgui_impl_sdlrenderer2.cpp`
- `imstb_truetype.h`

**最後の `imstb_truetype.h` に注意。** これは設定ウィンドウのためではなく、
`src/textrender.cpp` がファイラーと曲名の文字を焼くために直接 include している。
**設定ウィンドウを使わなくても Dear ImGui は必須。**

版を下げると通らない。`ImGuiStyle::FontScaleDpi` とグリフの動的追加を使って
いるため、**1.92 以降が必要**。

### portable_mdx

MDX の演奏モジュール（MXDRV + X68Sound の移植）。
<https://github.com/yosshin4004/portable_mdx> を `third_party/portable_mdx/` へ。

```sh
git clone --depth 1 https://github.com/yosshin4004/portable_mdx.git third_party/portable_mdx
```

ビルドに使われるのは以下。

- `include/`（`mxdrv.h` など）
- `src/mdx_util.c`、`src/mxdrv/*.cpp`、`src/x68sound/*.cpp`
- `examples/simple_mdx_player/main.c`、`examples/simple_mdx2wav/main.c`
  （付属サンプル。土台の動作確認に使うのでこれも必要）

`examples/` には portable_mdx 自身が同梱する SDL2 2.0.7 が入っているが、
mxv2 はそちらを使わない。

`-DPORTABLE_MDX_DIR=<パス>` で別の場所を指定できる。

### Get Ultimate Sound Amusement with G.

MDX のサンプルファイル。バイナリパッケージのみに含み、リポジトリには含まない。
個別のファイルとしては以下から取得できるが、再配布には許可を得る必要がある。

- 作品全体 https://x.haun.org/dtm/gusa_indexg.html
- 個別のMDXファイル https://x.haun.org/dtm/
- third_party/GUSA-CDg/ArctanX に展開する。

## 3. ビルド

`mxv2/` で実行する。

```sh
cmake -B build -S .
cmake --build build --config Release
```

生成物は `build/Release/`（MSBuild のときは構成ごとのサブフォルダ）。

| | |
|---|---|
| `mxv2.exe` | 本体 |
| `mxv2_chunktest.exe` | 検証ツール（後述） |
| `simple_mdx_player.exe` `simple_mdx2wav.exe` | portable_mdx 付属サンプル |
| `SDL2.dll` | 自動コピー |
| `assets/` | 素材一式を自動コピー（文言の `assets/locale/` を含む） |
| `assets/mdx` | 自動作成 |
| `assets/mdx/ArctanX` | third_party/GUSA-CDg/ArctanX から自動コピー、なければ無視 |

`assets/` は毎回のビルドでコピーされるが、**削除はされない**ので、
実験で置いたファイルは残る。おかしくなったら `build/` ごと捨てる。

設定はユーザーフォルダ（Windows なら `%APPDATA%\mxv2\`）の `mxv2.ini` に
保存される（初回起動時に作られる）。実行ファイルの隣は読むだけなので、
`assets/` を消しても設定と保存した配色は残る。逆に、消したいときは
ユーザーフォルダごと捨てる。`-userdir <dir>` で場所を変えられるので、
設定を汚さずに試すときはそれを使う。

### 場所を指定してビルドする例

```sh
cmake -B build -S . -DSDL2_ROOT=D:/lib/SDL2-2.30.0 -DPORTABLE_MDX_DIR=D:/lib/portable_mdx
```

**`SDL2_ROOT` と `PORTABLE_MDX_DIR` はキャッシュ変数**なので、一度 configure
したあとにフォルダを移動したときは、`CMakeLists.txt` の既定値を直すだけでは
足りない。上のように `-D` で指定し直すか、`build/` を捨ててやり直す。

## 4. 動作確認

`mxv2_chunktest` は、演奏を分割してデコードしても波形が変わらないことを
確認するツール。SDL を使わないので CI でも回せる。

```
mxv2_chunktest <mdxfile> [秒数]
    -> OK: 20 sec / 1920000 samples identical (peak=17279 rms=2570)
```

`identical` と出れば、portable_mdx の呼び出し方が壊れていない。

本体は MDX ファイルかフォルダを渡して起動する。

```
mxv2 [options] [<mdxfile> | <dir>]
    -skin <name>    スキン名 (assets:<name> で同梱ぶんを名指し)
    -zoom <percent> 表示倍率 % (100 でドット等倍)
    -userdir <dir>  設定とユーザー素材の場所
    -locale <name>  文言の言語 (assets/locale/<name>/message.ini)
    -noquit         演奏終了後も閉じない
```

`mxv2 -h` で全オプションとキー割り当てが出る。

## 5. うまくいかないとき

| 症状 | 原因 |
|---|---|
| `portable_mdx が見つかりません: <パス>` | `third_party/portable_mdx/` が無い。`-DPORTABLE_MDX_DIR=` で指定してもよい |
| `Cannot find source file: .../third_party/imgui/imgui.cpp` | `third_party/imgui/` が無い |
| configure は通るが `SDL.h` が開けない / `SDL2.lib` が見つからない | `third_party/SDL2-2.32.10/` が無い、または VC 開発用パッケージでない（ソース配布には `lib/` が無い）。存在チェックをしていないのでここまで進んでしまう |
| リンクは通るが起動直後に落ちる | `SDL2.dll` の版が違う。`build/` を捨ててビルドし直す |
| 素材が見つからないと言われる | `assets/` が実行ファイルの隣に無い。`-assets <dir>` で場所を渡せる |
| 画面の文字が `Menu.Open` のようなキー名になる | `assets/locale/` が無い。ログに `message not found:` が出る。ロケール名が違うだけなら英語で出る（`Locale ... was not found`） |
| 設定を変えても次の起動で戻る | ユーザーフォルダに書けていない。起動ログの `userdir :` の行を見る |

## 6. ライセンスについて

mxv2 は Apache License Version 2.0（`LICENSE`）。

`third_party/` の 3 つはいずれも無改変で置くだけなので、それぞれのライセンスに
従う。**ビルドしたバイナリを配布するときは `NOTICE` を読むこと。** 何が同梱され
どのライセンスが適用されるか（SDL2 は zlib、Dear ImGui は MIT、portable_mdx は
由来ごとに 3 系統、同梱フォントは OFL 1.1）をそこにまとめてある。
