# mxv2 のビルド手順

`third_party/` はリポジトリに含めていない。クローンしたあと、このドキュメントに
従って自分で用意する必要がある。

`sdlcastg/`（Chromecast へ送るライブラリ。<https://github.com/gorry/sdlcastg>）は
**git の submodule**。クローンするときは submodule も取ってくる
（`git clone --recursive`。TortoiseGit なら Clone の画面で「Recursive」にチェック）。
取らずにクローンしたときは、あとから `git submodule update --init`（TortoiseGit なら
右クリック → TortoiseGit → Submodule Update）。取っていなくても、キャストの機能が
外れるだけでビルドはできる（下の `MXV2_CAST`）。

## 1. 必要なもの

|                | 版        | 備考                                                            |
| -------------- | --------- | --------------------------------------------------------------- |
| CMake          | 3.20 以降 | 開発は 3.31.8 で行っている                                      |
| C++ コンパイラ | C++11     | Windows は Visual Studio 2022 (MSVC 19.44) で開発・検証している |

Windows / MSVC、Android に加え、この fork は macOS の `.app` ビルドに対応する。
Android は「6. Android 版のビルド」、macOS は下の「macOS 版のビルド」を見ること。
macOS の arm64/x86_64 はそれぞれビルドし、SDL2 を静的リンクする。

## 2. third_party/ を用意する

ライブラリを次のパスに展開する（SDL2 のソースは Android / macOS で使う）。

```
mxv2/
    third_party/
        SDL2-2.32.10/     SDL2 の VC 開発用パッケージ
        SDL2-2.32.10-src/ SDL2 のソース（Android / macOS）
        imgui/            Dear ImGui v1.92.4
        portable_mdx/     演奏モジュール
        mbedtls-3.6.7/    TLS（sdlcastg のときだけ）
        vcpkg_installed/  libvpx と opus と libyuv（sdlcastg のときだけ。vcpkg で作る）
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
Android / macOS は SDL2 のソースを使うので、VC パッケージの展開は不要。
その他のデスクトップ環境では `find_package(SDL2)` で探す。

### SDL2 2.32.10 のソース（Android / macOS）

Android では SDL2 をソースからビルドし、Java 側（`SDLActivity` など）も同じ
ソースツリーから読む。同じページの **`SDL2-2.32.10.zip`**（ソース配布）を
`third_party/SDL2-2.32.10-src/` へ展開する。`src/` と `android-project/` が
並んでいれば正しい。
macOS では下記の取得スクリプトで同じ版を用意できる。

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

`third_party/portable_mdx` は本家ではなく gorry/portable_mdx（96kHz 出力と、
OPM レジスタ書き込みの通知 `MXDRV_SetOpmWriteCallback` を足した fork）を
前提にしている。どちらも能力マクロ（`X68SOUND_SUPPORT_96KHZ` /
`MXDRV_SUPPORT_OPMWRITE_CALLBACK`）を `#ifdef` で見ているので本家でもビルドは
通るが、音色データ表示の PMD / AMD は通知が無いと最後に書かれたほうしか出ない。

MDX の演奏モジュール（MXDRV + X68Sound の移植）。
<https://github.com/gorry/portable_mdx> を `third_party/portable_mdx/` へ。

```sh
git clone --depth 1 https://github.com/gorry/portable_mdx.git third_party/portable_mdx
```

ビルドに使われるのは以下。

- `include/`（`mxdrv.h` など）
- `src/mdx_util.c`、`src/mxdrv/*.cpp`、`src/x68sound/*.cpp`
- `examples/simple_mdx_player/main.c`、`examples/simple_mdx2wav/main.c`
  （付属サンプル。土台の動作確認に使うのでこれも必要）

`examples/` には portable_mdx 自身が同梱する SDL2 2.0.7 が入っているが、
mxv2 はそちらを使わない。

`-DPORTABLE_MDX_DIR=<パス>` で別の場所を指定できる。

### mbedTLS 3.6.7（sdlcastg のときだけ）

Chromecast へ送るライブラリ `sdlcastg/`（submodule。<https://github.com/gorry/sdlcastg>）が、受信側との
TLS に使う。mxv2 の [キャスト…]（Chromecast へ送る）に要る。無ければその機能を
抜いてビルドする（下の `MXV2_CAST`）。

<https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7> の
**`mbedtls-3.6.7.tar.bz2`**（生成済みのファイルが入ったリリース版。git の
クローンではない）を `third_party/mbedtls-3.6.7/` へ展開する。
同じページの `mbedtls-3.6.7-sha256sum.txt` で照合できる
（`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`）。

```sh
tar --force-local -xjf mbedtls-3.6.7.tar.bz2 -C third_party
```

### libvpx / opus / libyuv（sdlcastg のときだけ）

sdlcastg が映像（VP8）と音声（Opus）のエンコードと、画面の縮小・色の変換
（libyuv）に使う。libvpx は独自の configure で作るため MSVC や NDK から直には
ビルドしにくいので、**vcpkg で作る**。入れ先は `third_party/vcpkg_installed/`
（vcpkg 本体の installed は汚さない）。2026-09-25 の時点で libvpx 1.16.0 /
opus 1.5.2 / libyuv 1916（libyuv に付いてくる libjpeg-turbo は使わない）。

vcpkg の**クラシックモード**（`--classic`）で入れる。マニフェスト（vcpkg.json）
で入れると、入れ先に 1 つの triplet しか置けず、Android 向けを入れた時点で
Windows 向けが消される。

```sh
# Android 向けを作るときだけ NDK の場所を渡す（/ 区切りで）
export ANDROID_NDK_HOME=D:/dev/android-ndk

vcpkg install --classic --x-install-root=third_party/vcpkg_installed \
    --overlay-triplets=sdlcastg/triplets --overlay-ports=sdlcastg/ports \
    libvpx:x64-windows-static-md opus:x64-windows-static-md libyuv:x64-windows-static-md \
    libvpx:arm64-android opus:arm64-android libyuv:arm64-android \
    libvpx:arm-neon-android opus:arm-neon-android libyuv:arm-neon-android
```

- Windows 向けは静的ライブラリ（CRT は動的。mxv2 と同じ `/MD`）。
- `sdlcastg/triplets/` は Android 向けの triplet。vcpkg 標準のものとの違いは、
  **API 21**（mxv2 の minSdk。標準は 28）、C++ の実行時ライブラリが静的
  （mxv2 と同じ `c++_static`）、NDK のツールチェーンを明示していること
  （標準のままだとこの版の vcpkg では NDK が見つからなかった）。
- `sdlcastg/ports/libvpx/` は vcpkg の libvpx の移植を写して直したもの。
  標準のものは Android を `generic-gnu`（SIMD なしの C だけ）で作るので、
  ARM は libvpx の Android 用のターゲットで作って **NEON を使う**ようにした
  （Pixel 7a で測ったら、エンコードの中身がすべて SIMD なしの C で動いていた）。直したところには `sdlcastg:` と
  書いてある。vcpkg を更新して libvpx の版が上がったら、写し直すこと。

初回は msys2 や nasm も取ってきて、全部で 10 分ほどかかる。

sdlcastg は単独でもビルドできる（見本のコマンド `castplay` などを含む。詳しくは
`sdlcastg/BUILD.md`）。単独のときの依存の既定の置き場所は `sdlcastg/third_party/`
なので、mxv2 の中で試すときは mxv2 の `third_party/` を渡す。

```sh
cmake -S sdlcastg -B build/sdlcastg-win64 -A x64 \
    -DSDLCASTG_MBEDTLS_DIR=$PWD/third_party/mbedtls-3.6.7 \
    -DSDLCASTG_VCPKG_INSTALLED_DIR=$PWD/third_party/vcpkg_installed \
    -DSDLCASTG_SDL2_ROOT=$PWD/third_party/SDL2-2.32.10
cmake --build build/sdlcastg-win64 --config Release
build/sdlcastg-win64/Release/castplay.exe --list
```

mxv2 に組み込むかは CMake の `MXV2_CAST`（`AUTO` / `ON` / `OFF`）。既定の `AUTO` は、
submodule の `sdlcastg/`、mbedTLS、そのプラットフォームの vcpkg の成果物がそろっていれば組み込む
（configure のログに `mxv2: Chromecast への送信を組み込む` と出る）。組み込まないと
メニューの [キャスト…] が出ないだけで、ほかは変わらない。
Android で CMake に引数を足すときは Gradle に `-Pmxv2.cmakeArgs="-DMXV2_CAST=OFF"`
のように渡す（`mxv2.cmakeArgs` は CMake へ足す引数。空白区切り）。

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

|                                              |                                                                                        |
| -------------------------------------------- | -------------------------------------------------------------------------------------- |
| `mxv2.exe`                                   | 本体（Debug 構成では `mxv2_debug.exe`。末尾は `Profile.ini` の `[AppId] DebugSuffix`） |
| `mxv2_chunktest.exe`                         | 検証ツール（後述）                                                                     |
| `simple_mdx_player.exe` `simple_mdx2wav.exe` | portable_mdx 付属サンプル                                                              |
| `SDL2.dll`                                   | 自動コピー                                                                             |
| `assets/`                                    | 素材一式を自動コピー（文言の `assets/locale/` を含む）                                 |
| `assets/mdx`                                 | 自動作成                                                                               |
| `assets/mdx/ArctanX`                         | third_party/GUSA-CDg/ArctanX から自動コピー、なければ無視                              |

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
    -quit           演奏し終えたら終了する (デバッグ用)
    -tutorial       チュートリアルを表示する (デバッグ用。見終えても Done は書かない)
```

`mxv2 -h` で全オプションとキー割り当てが出る。

## 5. うまくいかないとき

| 症状                                                              | 原因                                                                                                                                                   |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `portable_mdx が見つかりません: <パス>`                           | `third_party/portable_mdx/` が無い。`-DPORTABLE_MDX_DIR=` で指定してもよい                                                                             |
| `Cannot find source file: .../third_party/imgui/imgui.cpp`        | `third_party/imgui/` が無い                                                                                                                            |
| configure は通るが `SDL.h` が開けない / `SDL2.lib` が見つからない | `third_party/SDL2-2.32.10/` が無い、または VC 開発用パッケージでない（ソース配布には `lib/` が無い）。存在チェックをしていないのでここまで進んでしまう |
| リンクは通るが起動直後に落ちる                                    | `SDL2.dll` の版が違う。`build/` を捨ててビルドし直す                                                                                                   |
| 素材が見つからないと言われる                                      | `assets/` が実行ファイルの隣に無い。`-assets <dir>` で場所を渡せる                                                                                     |
| 画面の文字が `Menu.Open` のようなキー名になる                     | `assets/locale/` が無い。ログに `message not found:` が出る。ロケール名が違うだけなら英語で出る（`Locale ... was not found`）                          |
| 設定を変えても次の起動で戻る                                      | ユーザーフォルダに書けていない。起動ログの `userdir :` の行を見る                                                                                      |

## 配布物を作る（make arc）

```sh
make arc                  # win64 の zip
make arc TARGET=android   # android の apk
make arc-all              # 両方
```

`Release/` の下に、`<ShortText>_<TARGET>_<Version>`（`Profile.ini` の
[Title] ShortText と [Version] Text）の名前で作る。例:
`mxv2_win64_2026.0820.1.zip` / `mxv2_android_2026.0820.1.apk`。
BUILD の値に関わらず release でビルドする。

- zip の中身は同じ名前のフォルダに `mxv2.exe` / `SDL2.dll` / `assets/` /
  `NOTICE` / `LICENSE` / `README.md`。**win64 だけ**、スキンエディタの実行ファイル
  一式（`SkinEditor.exe` / `.dll` / `.deps.json` / `.runtimeconfig.json`。Release で
  ビルドし直す）と `README_SkinEditor.md` も同じフォルダに入る（動かすには .NET 8 の
  Windows Desktop Runtime が要る。win32 には入れない）。`assets/` はソース側から取り、試験用の `font.ttf`
  （.gitignore 済み）は入れない。zip は Info-ZIP の `zip`（GnuWin32 の zip
  パッケージなど。PATH に要る）で、`-D` でフォルダのエントリを入れずに作る。
  PowerShell の Compress-Archive や CMake 内蔵の tar は、古い 7-Zip
  （LhaForge 内蔵の 9.22 など）がフォルダのエントリをファイルとして取り出して
  展開に失敗する zip を作るので使わない。
- apk は `android/keystore.properties` があれば署名済み。無ければ
  `-unsigned` を付けた名前になり、そのままでは端末に入らない
  （下の「Android のリリース署名」）。
- `Release/` は .gitignore 済み。

## スキンエディタ（make *-skineditor）

`skineditor/` のスキンエディタ（C# / .NET 8 WinForms、Windows 専用）は
mxv2 本体とは別のアプリなので、`-skineditor` を後置したターゲットで扱う。
dotnet SDK 8 以降が PATH に要る。TARGET は見ない（BUILD と PREFIX / OPTION は
本体と共通）。

```sh
make build-skineditor                 # dotnet build（BUILD=debug / release）
make test-skineditor                  # skineditor/SkinEditor.Tests の xUnit
make run-skineditor OPTION=...        # ここをカレントにして起動（開発フォルダモード）
make install-skineditor PREFIX=...    # mxv2.exe の隣へ置く（ユーザーフォルダモード）
make uninstall-skineditor PREFIX=...
make clean-skineditor                 # bin/ obj/ と、ここへコピーされた実行ファイルを消す
```

- ビルドのたびに csproj がこのフォルダ（CMakeLists.txt の隣）へ実行ファイル
  一式をコピーする（exe を直接起動したときに開発フォルダモードになるため。
  .gitignore 済み）。
- install は `SkinEditor.exe` / `.dll` / `.deps.json` / `.runtimeconfig.json` と
  `README_SkinEditor.md` を PREFIX へ置く。フレームワーク依存なので、動かす
  機械には .NET 8 の Windows Desktop Runtime が要る。
- 配布物（make arc）には TARGET=win64 のときだけ同梱される（上の「配布物を作る」）。

## アプリの名前・版・著作権（Profile.ini）

`mxv2/Profile.ini` が唯一の置き場（著作者専用）。ビルド時に写される:

- CMake: `src/appprofile.h.in` → `build/<構成>/generated/appprofile.h`
  （`cmdline.cpp` の `kAppName` / `kAppVersion` / `kAppCopyright`）と、Windows では
  `res/mxv2.rc.in` → `generated/mxv2.rc`（アイコンと VERSIONINFO。exe の
  プロパティに出る）。Profile.ini を変えると configure が自動で走る。
- スキンエディタ: `skineditor/SkinEditor/SkinEditor.csproj` が同じファイルを
  読み、`[Title] Text-SkinEditor` / `[Version] Text` / `[Copyright] Text-SkinEditor`
  をアセンブリ属性（AssemblyTitle / Version / Copyright）に写す。ビルドした日も
  `AssemblyMetadata("BuildDate")` として入る。実行時は `Model/AppProfile.cs` が
  属性を読み、スキン一覧の [バージョン情報…] に出す。
- Gradle: `android/app/build.gradle` が同じファイルを読み、`namespace`
  （[AppId] Namespace）・`applicationId`（[AppId] Android）・`versionName`
  （[Version] Text）・`versionCode`（[Version] Number）・`app_name`
  （[Title] ShortText。ホーム画面の名前）に写す。[AppId] Windows は
  いまは使い道が無く、`appprofile.h` に定義が出るだけ。
  **[AppId] DebugSuffix はデバッグ版の目印**で、Windows の Debug 構成の実行ファイルは
  `mxv2_<DebugSuffix>.exe`（`mxv2_debug.exe`）、Android の debug ビルドの
  `applicationId` は `<AppId Android>.<DebugSuffix>`（`net.gorry.mxv2.debug`）、ホーム画面の
  名前も `<ShortText> <DebugSuffix>`（`mxv2 debug`）になる。
  配布版と並べて入れられ、Android ではユーザーフォルダと SAF の権限も別になる
  （`namespace` は変えないので Java のパッケージは `net.gorry.mxv2` のまま）。`res/values/strings.xml` に
  `app_name` は置かない（重複で止まる）。

## 6. Android 版のビルド

`android/` に Gradle プロジェクトがある。ネイティブ側は Windows と同じ
`CMakeLists.txt` をそのまま呼ぶ（Android 固有の分岐は `if(ANDROID)` だけ）。

### 用意するもの

|             | 版                   | 開発に使っているもの                                    |
| ----------- | -------------------- | ------------------------------------------------------- |
| Android SDK | platform 34 以上     | `platforms/android-34` と build-tools                   |
| NDK         | r28c (28.2.13676358) | `app/build.gradle` の `ndkVersion` と揃える             |
| JDK         | 17 以上              | OpenJDK 21.0.2                                          |
| Gradle      | 8.7                  | `android/gradlew` が拾ってくる                          |
| CMake       | 3.22.1               | **SDK 同梱のもの**（SDL2 のソースが 3.31 では通らない） |

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
adb shell am start -n net.gorry.mxv2.debug/net.gorry.mxv2.MainActivity
```

debug ビルドの `applicationId` は `net.gorry.mxv2.debug`（上の [AppId] DebugSuffix）
なので、`am start` にはパッケージ名とクラスの完全名を分けて渡す（release なら
`net.gorry.mxv2/.MainActivity`）。`make run TARGET=android BUILD=<debug|release>` は
これを自動で組み立てる。

生成物は `app/build/outputs/apk/debug/app-debug.apk`。ABI は
`gradle.properties` の `mxv2.abiFilters`（既定は `arm64-v8a,armeabi-v7a`。
32bit の実機で確認するために後者を足した。**32bit ビルドでは
third_party/portable_mdx が「常に真の比較」の警告を出すが、third_party は
無改変で置く方針なのでそのままにしてある**）。

- **ログは logcat へ出る。** mxv2 の `printf` はタグ `mxv2`、SDL 自身のログは
  `SDL` / `SDL/APP`。`adb logcat -s mxv2 SDL` で読める。
- **コマンドライン引数**はインテントの extra `args` で渡せる。

  ```sh
  adb shell am start -n net.gorry.mxv2.debug/net.gorry.mxv2.MainActivity --esa args "assets:ArctanX/am_field.mdx"
  ```

- 設定と展開した素材は `/data/data/net.gorry.mxv2/files/` の下。debug ビルド
  なら `adb shell run-as net.gorry.mxv2 ls files/` で覗ける。作り直したい
  ときは `run-as net.gorry.mxv2 rm files/mxv2.ini`。
- **バックグラウンドでも演奏を続ける。** 演奏しているあいだは前面サービス
  (`PlaybackService`) が通知を出す。初回起動時に通知の許可を尋ねる
  （Android 13 以降）。断っても演奏はできるが通知は出ない。あとから
  `adb shell pm grant net.gorry.mxv2 android.permission.POST_NOTIFICATIONS`
  でも与えられる。動きの確認に使ったもの:

  ```sh
  adb shell dumpsys activity services net.gorry.mxv2   # 前面サービス
  adb shell dumpsys notification --noredact            # 通知の中身
  adb shell dumpsys audio | grep net.gorry.mxv2        # 鳴っているか
  adb shell input keyevent KEYCODE_MEDIA_NEXT          # 通知と同じ操作
  ```

### Android のリリース署名

`assembleRelease` は署名情報が無くても通るが、その場合の apk は
**unsigned**（`app-release-unsigned.apk`）で、Android は unsigned apk の
`adb install` を拒否する。配布用に署名した release apk が欲しいときは、
鍵を用意して次のファイルを置く。

1. **鍵（キーストア）ファイルをどこかに置く。** 好きな場所でよい
   （例: `android/release.keystore`）。無ければ `keytool` で作れる。

   ```sh
   keytool -genkeypair -v -keystore android/release.keystore -alias mxv2 -keyalg RSA -keysize 2048 -validity 10000
   ```

   このファイルは**絶対に git に入れないこと**（`*.keystore` / `*.jks` は
   `.gitignore` 済み）。無くすと同じ apk に上書き更新できなくなる
   （別の鍵の apk は Android 側が「署名が違う」と言って `adb install -r` を
   拒否する）ので、鍵ファイルとパスワードは別途バックアップしておくこと。

2. **`android/keystore.properties` を作り、鍵の在り処と諸情報を書く。**
   `local.properties` と同じ扱いで、**git には入れない**
   （`.gitignore` 済み）。

   ```
   storeFile=release.keystore
   storePassword=<上の keytool で入力したパスワード>
   keyAlias=mxv2
   ```

   `storeFile` は `android/` からの相対パス（上の例のとおり）か絶対パスの
   どちらでもよい。**`keyPassword` は省略してよい**（省略時は
   `storePassword` と同じ値を使う）。上の `keytool` コマンドが作るのは
   既定の形式である **PKCS12** のキーストアで、この形式はキーストアと
   鍵とで別々のパスワードを持てない（`keytool` 自身もパスワードを
   一度しか尋ねてこない）ため。JKS 形式などで実際に別のパスワードを
   付けている場合だけ `keyPassword=<鍵のパスワード>` を足すこと。

このファイルがあれば `assembleRelease` はそのまま署名済みの
`app-release.apk` を作るようになり、`adb install` できるようになる
（無ければ今までどおり unsigned のまま）。Makefile を使っているなら
`make build TARGET=android BUILD=release` でビルド、
`make install TARGET=android BUILD=release` や
`make run TARGET=android BUILD=release` はこのファイルがあるときだけ
実際にインストールする（無いときは理由を出して止まる）。

### 素材の届き方

apk の `assets/` は **`fopen` で開けず、列挙もできない**。そこで

1. Gradle の `prepareMxv2Assets` が「実行ファイルの隣」と同じ姿
   （`assets/` と `NOTICE` / `LICENSE` / `README.md`）を組み立てて apk に入れる。
   同梱 MDX（`third_party/GUSA-CDg/ArctanX`）があれば一緒に入る。
2. `generateMxv2AssetIndex` が索引 `assetindex.txt`（crc32・サイズ・パス）を作る。
3. 起動時に `src/androidassets.cpp` が索引を見て、**変わったものだけ**内部
   ストレージへ展開する。

素材を差し替えたら `assembleDebug` し直せば、次の起動で展開もやり直される。
索引は `prepareMxv2Assets`（Sync）が並べた木から作るので、素材を減らしたときも
索引に残らない。

### ユーザーフォルダ（font.ttf やスキン、言語を置く場所）

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

**効くのはキャンバスの文字（ファイラー・曲名・画面の文字）だけ**で、
**設定などのダイアログは必ず同梱の `MPLUS1p-Regular.ttf` を使う**。
ダイアログの字は行の高さや mm の換算と噛み合っているので、
差し替えられるとラベルが切れたり寸法が狂ったりするため。

**言語もここに足せる。** `locale/<ロケール名>/message.ini` を置くと、次の
起動から設定ウィンドウの [言語] に出る（同梱と同じ名前ならその言語に重なり、
新しい名前なら新しい言語として並ぶ）。詳しくは `memo/readme.md` の
「メッセージカタログ」。

```
adb push message.ini /sdcard/Android/data/net.gorry.mxv2/files/locale/fr-FR/message.ini
```

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

| 場所                                                           | 用途                                            |
| -------------------------------------------------------------- | ----------------------------------------------- |
| `res/mxv2.ico`                                                 | Windows。`res/mxv2.rc` から実行ファイルへ埋める |
| `android/app/src/main/res/mipmap-*/ic_launcher.png`            | Android の昔ながらのアイコン (API 25 まで)      |
| `android/app/src/main/res/mipmap-*/ic_launcher_foreground.png` | アダプティブアイコンの前景                      |
| `android/app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`   | アダプティブアイコンの定義                      |
| `android/app/src/main/res/values/ic_launcher_background.xml`   | その下地の色（元画像の縁の色）                  |

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

## macOS 版のビルド

macOS 11 以降、Apple Silicon / Intel に対応する。Xcode Command Line Tools
（`xcode-select --install`）、CMake 3.20 以降、Ninja、Python 3 を用意する。
Homebrew を使う場合は `brew install cmake ninja python`。
SDL2、ImGui、演奏エンジンは `third_party/` に置き、SDL2 はソースから静的リンクする。
システムの AppKit と libcurl を使うため、実行先に Homebrew のライブラリは不要。

```sh
# プロジェクトルートで実行してください。
bash tools/fetch_macos_deps.sh

# Apple Silicon用。Intel は arm64 を x86_64 に置き換えてください。
cmake -S . -B build/macos-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DMXV2_CAST=OFF \
  -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build build/macos-arm64 --parallel
ctest --test-dir build/macos-arm64 --output-on-failure
open build/macos-arm64/mxv2.app
```

取得スクリプトは SDL2 **2.32.10**、
ImGui **v1.92.4** と gorry/portable_mdx を固定コミットで取得する。
`SDL2_SRC_ROOT` と `PORTABLE_MDX_DIR` は CMake で別の場所を指定できる。
手動で依存を用意する場合は従来の `third_party/` 配置も使える。
コマンドバッファ拡張 API の無い portable_mdx では `mxv2_benchmark` を省略するが、
プレーヤーと `mxv2_chunktest` はビルドする。

`.app/Contents/MacOS/` に実行ファイル、`Contents/Resources/` に素材、フォント、
NOTICE、ライセンス、README、アイコンが入る。素材パスは Cocoa の bundle から
解決し、カレントディレクトリには依存しない。設定と追加素材は
`~/Library/Application Support/mxv2/` に保存する。
macOS の bundle identifier は `app.kakikou.mxv2-mac`（`MXV2_MACOS_BUNDLE_ID` で変更可）。

macOS 配布用の `Contents/Resources/LICENSE` は `cmake/macos_license.cmake` で
生成する。本体の Apache 2.0 と NOTICE、SDL2 と内部の yuv2rgb / HIDAPI / fdlibm、
ImGui、portable_mdx、同梱フォントのライセンス・著作権表示を原文からまとめる。
通常ビルド・公証ビルドの Actions は、インストール後に全文を照合してから署名する。
リポジトリ直下の `LICENSE` は本体の Apache 2.0 のまま保持する。

`tools/test_macos.py` は別の場所へ `.app` をコピーし、日本語と空白を含むパスで
起動・素材読込み・設定保存・SDL の非無音 PCM 出力を確認する。
合成した短い FM 音で `mxv2_chunktest` の波形一致も確認する。
SDL の dummy video / disk audio を使う自動テストなので、実画面のクリック操作や
スピーカーでの実際の音の確認は手動で行う。

本移植では Chromecast は無効。Windows の SkinEditor、Android 固有の
SAF / PiP / メディア通知は macOS には移植しない。Bluetooth 出力先の自動検出と
OS の遅延計測は未対応で、SDL のバッファ長を基準に表示を遅らせる。
更新確認は従来どおり `Profile.ini` の本家の Releases を参照する。
LZX 圧縮 MDX / PDX は展開機能が無いため、読込み時にエラーとして拒否する。
圧縮・ヘッダー欠落・範囲外オフセットの拒否も自動テストに含める。

### GitHub Actions

`.github/workflows/build-macos.yml` は Apple Silicon (`macos-15`) と
Intel (`macos-15-intel`) のそれぞれで同じ依存取得・Release ビルド・自動テストを行う。
ブランチへの push、pull request、Actions の **Run workflow** で起動する。
完了後、Artifacts の `mxv2-macos-arm64` / `mxv2-macos-x86_64` を取得する。
中にある `mxv2-macos-<architecture>.zip` に `.app` が入る。

パッケージ化前にアドホック署名を施し、署名を検証してから `ditto` で ZIP を作る。
Developer ID の署名・Apple の公証は行わず、署名秘密鍵も必要としない。

### 手動の署名・公証版と Draft Release

`.github/workflows/release-macos-notarized.yml` は上記のビルドに加え公証付きのビルドを生成します。

必要なSecret:

| Secret                        | 内容                                                                        |
| ----------------------------- | --------------------------------------------------------------------------- |
| `MACOS_CERTIFICATE_BASE64`    | Developer ID Application の証明書と秘密鍵を含む `.p12` を Base64 にしたもの |
| `MACOS_CERTIFICATE_PASSWORD`  | `.p12` の書き出しパスワード                                                 |
| `APPLE_ID`                    | Apple Developer アカウントのメールアドレス                                  |
| `APPLE_APP_SPECIFIC_PASSWORD` | 公証申請用のアプリ用パスワード（通常のログインパスワードではない）          |
| `APPLE_TEAM_ID`               | 上記署名証明書に対応する Developer Team ID                                  |

成果物は `mxv2-macos-arm64-notarized.zip` / `mxv2-macos-x86_64-notarized.zip`
