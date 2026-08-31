# mxv2 のビルド手順

`third_party/` はリポジトリに含めていない。クローンしたあと、このドキュメントに
従って自分で用意する必要がある。

## 1. 必要なもの

| | 版 | 備考 |
|---|---|---|
| CMake | 3.20 以降 | 開発は 3.31.8 で行っている |
| C++ コンパイラ | C++11 | Windows は Visual Studio 2022 (MSVC 19.44) で開発・検証している |

**動作を確認しているのは Windows / MSVC と Android。** Android のビルドは
「6. Android 版のビルド」を見ること。それ以外のプラットフォーム向けの記述も
CMake は持っているが（SDL2 を `find_package` で探す）、まだ試していない。

## 2. third_party/ を用意する

ライブラリを次のパスに展開する（SDL2 のソースは Android のときだけ）。

```
mxv2/
    third_party/
        SDL2-2.32.10/     SDL2 の VC 開発用パッケージ
        SDL2-2.32.10-src/ SDL2 のソース（Android のときだけ）
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

### SDL2 2.32.10 のソース（Android のときだけ）

Android では SDL2 をソースからビルドし、Java 側（`SDLActivity` など）も同じ
ソースツリーから読む。同じページの **`SDL2-2.32.10.zip`**（ソース配布）を
`third_party/SDL2-2.32.10-src/` へ展開する。`src/` と `android-project/` が
並んでいれば正しい。

**上の VC 用パッケージとは別に置くこと。** あちらは Windows のビルドが使う。
場所を変えたいときは `-DSDL2_SRC_ROOT=<パス>`。

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

## 6. Android 版のビルド

`android/` に Gradle プロジェクトがある。ネイティブ側は Windows と同じ
`CMakeLists.txt` をそのまま呼ぶ（Android 固有の分岐は `if(ANDROID)` だけ）。

### 用意するもの

| | 版 | 開発に使っているもの |
|---|---|---|
| Android SDK | platform 34 以上 | `platforms/android-34` と build-tools |
| NDK | r28c (28.2.13676358) | `app/build.gradle` の `ndkVersion` と揃える |
| JDK | 17 以上 | OpenJDK 21.0.2 |
| Gradle | 8.7 | `android/gradlew` が拾ってくる |
| CMake | 3.22.1 | **SDK 同梱のもの**（SDL2 のソースが 3.31 では通らない） |

`third_party/SDL2-2.32.10-src/` が要る（上の「SDL2 2.32.10 のソース」）。

### local.properties

`android/local.properties` に場所を書く。**git には入れない**。

```
sdk.dir=D:/dev/android-sdk
ndk.path=D:/dev/android-ndk
```

`ndk.path` は NDK が SDK の外にあるときだけ。SDK の中（`sdk/ndk/<版>`）に
あれば `ndkVersion` から見つかるので要らない。**キー名は `ndk.dir` ではない**
（あちらは AGP 自身が読んでしまい、非推奨の警告が出る）。

### ビルドと実行

```sh
cd android
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n net.gorry.mxv2/.MainActivity
```

生成物は `app/build/outputs/apk/debug/app-debug.apk`。ABI は
`gradle.properties` の `mxv2.abiFilters`（既定は `arm64-v8a` だけ）。

- **ログは logcat へ出る。** mxv2 の `printf` はタグ `mxv2`、SDL 自身のログは
  `SDL` / `SDL/APP`。`adb logcat -s mxv2 SDL` で読める。
- **コマンドライン引数**はインテントの extra `args` で渡せる。

  ```sh
  adb shell am start -n net.gorry.mxv2/.MainActivity --esa args "assets:ArctanX/am_field.mdx"
  ```

- 設定と展開した素材は `/data/data/net.gorry.mxv2/files/` の下。debug ビルド
  なら `adb shell run-as net.gorry.mxv2 ls files/` で覗ける。作り直したい
  ときは `run-as net.gorry.mxv2 rm files/mxv2.ini`。

### 素材の届き方

apk の `assets/` は **`fopen` で開けず、列挙もできない**。そこで

1. Gradle の `prepareMxv2Assets` が「実行ファイルの隣」と同じ姿
   （`assets/` と `NOTICE` / `LICENSE`）を組み立てて apk に入れる。
   同梱 MDX（`third_party/GUSA-CDg/ArctanX`）があれば一緒に入る。
2. `generateMxv2AssetIndex` が索引 `assetindex.txt`（crc32・サイズ・パス）を作る。
3. 起動時に `src/androidassets.cpp` が索引を見て、**変わったものだけ**内部
   ストレージへ展開する。

素材を差し替えたら `assembleDebug` し直せば、次の起動で展開もやり直される。
索引は `prepareMxv2Assets`（Sync）が並べた木から作るので、素材を減らしたときも
索引に残らない。

### ユーザーフォルダ（font.ttf やスキンを置く場所）

Android のユーザーフォルダは

```
/sdcard/Android/data/net.gorry.mxv2/files/
```

**外部のアプリ専用領域**で、USB でパソコンから見えるし `adb push` でも入る。
権限は要らず、アンインストールで消える。起動ログの `userdir :` の行にも出る。

```
adb push <好きなフォント>.ttf /sdcard/Android/data/net.gorry.mxv2/files/font.ttf
```

Windows と同じく、ルート直下の `font.ttf` はユーザーぶんが同梱フォントより
優先される。**その字が無いときに同梱フォントへ落ちることはしない**ので、
日本語を出すなら日本語の入ったフォントを置くこと（欧文だけのフォントを
置くと、日本語が `?` になる）。

上の 3 で展開した同梱素材は、これとは別の**内部**ストレージ
（`/data/data/net.gorry.mxv2/files/bundled/`）にある。あちらは読むだけの
場所なので、外から見えなくてよい。

## 7. アイコン

アイコンの元画像は `pic/icon_mxv2.png`（1024x1024）。**`pic/` は
`.gitignore` で外してある**ので、リポジトリに入っているのは書き出したほう。
元の絵を描き直したときだけ、次を流し直して書き出しを差し替える。

```
pip install pillow
python tools/make_icons.py
```

書き出す先:

| 場所 | 用途 |
|---|---|
| `res/mxv2.ico` | Windows。`res/mxv2.rc` から実行ファイルへ埋める |
| `android/app/src/main/res/mipmap-*/ic_launcher.png` | Android の昔ながらのアイコン (API 25 まで) |
| `android/app/src/main/res/mipmap-*/ic_launcher_foreground.png` | アダプティブアイコンの前景 |
| `android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml` | アダプティブアイコンの定義 |
| `android/app/src/main/res/values/ic_launcher_background.xml` | その下地の色（元画像の縁の色） |

**Windows はリソースを 1 つ置くだけでよい。** SDL2 はウィンドウクラスを
作るとき、ヒントが無ければ `EnumResourceNames(RT_GROUP_ICON)` で
**いちばん最初のアイコン**を拾う。エクスプローラの決め方と同じなので、
`SDL_SetWindowIcon` を呼ばなくてもエクスプローラ・ウィンドウ・タスクバーの
すべてに効く。`res/mxv2.rc` にアイコンを足すときは、必ず今のものより
**後ろの ID** にすること。

**Android のアダプティブアイコンは中央 2/3 に絵を置いてある。** 画布は
108dp だがランチャーが見せるのは中央 72dp だけで、そこに円や角丸の型が
掛かる。元画像は端まで絵があり文字も横いっぱいなので、画布いっぱいに
広げると文字が切れる。2/3 に収めると、角丸の型ならほぼ全部、円の型でも
四隅（上の状態表示と下の鍵盤）が落ちるだけで済む。

## 8. ライセンスについて

mxv2 は Apache License Version 2.0（`LICENSE`）。

`third_party/` の 3 つはいずれも無改変で置くだけなので、それぞれのライセンスに
従う。**ビルドしたバイナリを配布するときは `NOTICE` を読むこと。** 何が同梱され
どのライセンスが適用されるか（SDL2 は zlib、Dear ImGui は MIT、portable_mdx は
由来ごとに 3 系統、同梱フォントは OFL 1.1）をそこにまとめてある。
