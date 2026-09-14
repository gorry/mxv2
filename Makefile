# mxv2 - 手元でビルドするための Makefile
#
# 「Makefile があるなら make でビルドできたほうが楽」という、人のための入口。
# 中身は BUILD.md に書いてある cmake / gradlew の手順をそのまま呼んでいるだけ
# なので、**CI から使ってもエージェントが使っても構わない**（`make` /
# `make run` / `make test` で済むぶん、そのほうが短く書けることも多い）。
# エージェントが BUILD.md の手順を直に叩くことが多いのは、単にそちらの
# ほうが素の手順として覚えやすいからで、ここを避ける理由があるわけではない。
#
# ---------------------------------------------------------------------------
# 前提
# ---------------------------------------------------------------------------
# - Windows では GnuWin32（make.exe と coreutils: rm / mkdir / cp / test など）
#   があることを前提にする。**シェルは cmd.exe に固定してある**（後述）ので、
#   PATH に他のシェル（Git for Windows の sh.exe 等）が混ざっていても
#   挙動は変わらない。
# - レシピは cmd.exe でも POSIX シェルでも同じ結果になる書き方だけを使う：
#     ・コマンドを繋ぐのは `&&` だけ（`;` は使わない。cmd.exe は区切りとして
#       扱わない）
#     ・値はすべて make 変数 `$(...)` として展開してから渡す（レシピの中で
#       シェルの変数参照 `$VAR` や `%VAR%` は使わない。cmd と sh で書き方が
#       違うため）
#     ・ループや if はできるだけ make 側（$(foreach) / $(if) / ifeq）で
#       組み立ててから、展開済みの1行として渡す（cmd.exe の for 文と
#       POSIX の for 文は書き方が違うため）
#     ・空行を見せたいだけの `echo`（`echo.` など）は使わない——GNU Make の
#       Windows 版はコマンドを直接 CreateProcess しようとすることがあり、
#       シェルの組み込みコマンドしか無い書き方だと
#       「CreateProcess(NULL, echo., ...) failed」で落ちる。区切りには
#       代わりに `-----` のような実在の文字列を使う。
# - cmake / Visual Studio（MSVC）/ Android SDK・NDK / adb はあらかじめ
#   用意されていること。詳しくは BUILD.md を見ること。
# - Windows 向けは CMake の Visual Studio ジェネレータ（マルチコンフィグ）を
#   使う。vcvars の準備なしに MSVC を自動で見つけてくれるため、
#   `-A Win32` / `-A x64` の切り替えだけで win32 / win64 の両方を
#   同じやり方でビルドできる。
# - Android 向けは android/ の Gradle プロジェクト（gradlew）をそのまま呼ぶ。
#   ネイティブ側は同じ CMakeLists.txt を Gradle 経由で（内部で Ninja を
#   使って）ビルドする。
#
# 使い方の一覧は `make help`。
# ---------------------------------------------------------------------------

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /C
endif

# フォルダを作るコマンド。**cmd.exe には組み込みの mkdir があり、PATH の
# GnuWin32 の mkdir.exe より優先される**。組み込みは `-p` を知らず、
# `mkdir -p "x"` を「-p と x の 2 つを作れ」と解釈して **`-p` という名前の
# フォルダを作ってしまう**（2026-09-13 に踏んだ。2 回目は「-p は既にある」で
# 失敗する）。`mkdir.exe -p` と書いても cmd は組み込みに回す（内部コマンド名の
# 直後の `.` を区切りとみなす）。cmake は必ずあるので、その `-E make_directory`
# を使う（cmd でも sh でも同じ）。rm / cp は cmd の組み込みに無いので、
# そのままで GnuWin32 のものが動く。
MKDIR_P := cmake -E make_directory

# 引数なしの `make` は build。
#
# **明示しないと build にならない。** GNU Make の既定のゴールは
# 「ファイルの中で最初に現れた（.PHONY などの特殊ターゲットでない）ルール」で、
# このファイルではそれが `$(WIN_BUILD_DIR)/CMakeCache.txt`（configure）に
# なってしまう。実際、引数なしの `make` が configure だけで終わっていた。
.DEFAULT_GOAL := build

# ---------------------------------------------------------------------------
# スイッチ（環境変数 / make の代入で指定する）
# ---------------------------------------------------------------------------
# TARGET  : ビルド対象。win32 / win64 / android / android-arm32 / android-arm64
#           省略時は win64（Windows 以外での既定は未検証。BUILD.md のとおり
#           Windows と Android しか動作確認していない）。
# BUILD   : debug / release。省略時は debug。
# OPTION  : `run` に渡す追加のコマンドライン。省略時は空。
# PREFIX  : `install` / `uninstall` の置き場所（TARGET が win32 / win64 の
#           ときだけ使う）。**この Makefile 独自の追加スイッチ**——mxv2 の
#           CMakeLists.txt には install() が無く、Windows 版は「フォルダを
#           コピーするだけ」の移植版なので、その置き先をここで持たせてある。
#           省略時は `dist/<TARGET>-<BUILD>/`。
TARGET ?= win64
BUILD ?= debug
OPTION ?=
PREFIX ?= dist/$(TARGET)-$(BUILD)

VALID_TARGETS := win32 win64 android android-arm32 android-arm64
VALID_BUILDS := debug release

# `make help` だけを頼まれたときは、TARGET / BUILD が変な値でも
# 止めずにヘルプを見せる。
SKIP_VALIDATION :=
ifneq ($(MAKECMDGOALS),)
ifeq ($(filter-out help,$(MAKECMDGOALS)),)
SKIP_VALIDATION := 1
endif
endif

# ---------------------------------------------------------------------------
# TARGET から導く値
# ---------------------------------------------------------------------------
ifeq ($(TARGET),win32)
PLATFORM := windows
VS_ARCH := Win32
else ifeq ($(TARGET),win64)
PLATFORM := windows
VS_ARCH := x64
else ifeq ($(TARGET),android)
PLATFORM := android
ABI_OVERRIDE :=
else ifeq ($(TARGET),android-arm32)
PLATFORM := android
ABI_OVERRIDE := armeabi-v7a
else ifeq ($(TARGET),android-arm64)
PLATFORM := android
ABI_OVERRIDE := arm64-v8a
else
# 未知の TARGET。`make help` だけは通したいのでここでは止めず、
# 実際に何かを頼まれたときに下の検証で止める。
PLATFORM := windows
VS_ARCH := x64
endif

# ---------------------------------------------------------------------------
# BUILD から導く値
# ---------------------------------------------------------------------------
ifeq ($(BUILD),debug)
CONFIG := Debug
else ifeq ($(BUILD),release)
CONFIG := Release
else
CONFIG := Debug
endif

ifndef SKIP_VALIDATION
ifeq ($(filter $(TARGET),$(VALID_TARGETS)),)
$(error Invalid TARGET '$(TARGET)'. Valid values: $(VALID_TARGETS))
endif
ifeq ($(filter $(BUILD),$(VALID_BUILDS)),)
$(error Invalid BUILD '$(BUILD)'. Valid values: $(VALID_BUILDS))
endif
endif

# ---------------------------------------------------------------------------
# Windows 側（CMake + Visual Studio ジェネレータ）
# ---------------------------------------------------------------------------
# ビルドディレクトリは TARGET ごとに分ける（win32 と win64 を同時に持てる
# ように）。Visual Studio ジェネレータはマルチコンフィグなので、Debug /
# Release の切り替えに configure をやり直す必要はない
# （`cmake --build ... --config <Debug|Release>` で選ぶだけ）。
WIN_BUILD_DIR := build/$(TARGET)
WIN_OUT_DIR := $(WIN_BUILD_DIR)/$(CONFIG)
EXE_MAIN := $(WIN_OUT_DIR)/mxv2.exe
EXE_CHUNKTEST := $(WIN_OUT_DIR)/mxv2_chunktest.exe

CMAKE_CONFIGURE := cmake -B $(WIN_BUILD_DIR) -S . -A $(VS_ARCH)

# まだ configure していなければ行う。一度 configure していれば
# （このディレクトリが生きているかぎり）`cmake --build` だけで済む。
$(WIN_BUILD_DIR)/CMakeCache.txt:
	$(CMAKE_CONFIGURE)

# `make configure` と頼まれたときは、**済んでいてもやり直す**。
# CMakeLists.txt を触ったあとに作り直したいときのための入口なので、
# 「もう最新です」と言われても困る（cmake の再 configure は速いし、
# 何度やっても同じ結果になる）。
.PHONY: configure-windows
configure-windows:
	$(CMAKE_CONFIGURE)

.PHONY: build-windows
build-windows: $(WIN_BUILD_DIR)/CMakeCache.txt
	cmake --build $(WIN_BUILD_DIR) --config $(CONFIG) --parallel

.PHONY: clean-windows
clean-windows:
	rm -rf $(WIN_BUILD_DIR)

.PHONY: run-windows
run-windows: build-windows
	"$(EXE_MAIN)" $(OPTION)

# testdata/*.mdx を1本ずつ mxv2_chunktest に通す。Windows のファイル名は
# 大小を区別しないので、`*.mdx` だけで X68K_THEME-X68K_THEME.MDX も拾える
# （`*.mdx` と `*.MDX` を両方書くと同じファイルを二重に数えてしまう）。
# どれか1本でも失敗すれば `&&` の連鎖がそこで止まり、make も失敗で終わる。
TESTDATA_MDX := $(wildcard testdata/*.mdx)
TESTDATA_CMDS := $(foreach f,$(TESTDATA_MDX),"$(EXE_CHUNKTEST)" "$(f)" &&)

.PHONY: test-windows
test-windows: build-windows
	$(TESTDATA_CMDS) cd .

# install / uninstall は mxv2 独自の約束（CMakeLists.txt に install() は無い）。
# 「実行ファイル + SDL2.dll + assets + NOTICE + LICENSE + README.md」という、
# そのままコピーして配れる移植版の姿を PREFIX の下に作るだけ。開発用のツール
# （mxv2_chunktest.exe など）や .pdb は対象に含めない。
.PHONY: install-windows
install-windows: build-windows
	$(MKDIR_P) "$(PREFIX)"
	cp "$(EXE_MAIN)" "$(PREFIX)/mxv2.exe"
	cp "$(WIN_OUT_DIR)/SDL2.dll" "$(PREFIX)/SDL2.dll"
	rm -rf "$(PREFIX)/assets"
	cp -r "$(WIN_OUT_DIR)/assets" "$(PREFIX)/assets"
	cp NOTICE "$(PREFIX)/NOTICE"
	cp LICENSE "$(PREFIX)/LICENSE"
	cp README.md "$(PREFIX)/README.md"
	@echo Installed to $(PREFIX)

.PHONY: uninstall-windows
uninstall-windows:
	rm -rf "$(PREFIX)/mxv2.exe" "$(PREFIX)/SDL2.dll" "$(PREFIX)/assets" "$(PREFIX)/NOTICE" "$(PREFIX)/LICENSE" "$(PREFIX)/README.md"
	@echo Removed mxv2 files from $(PREFIX)

# ---------------------------------------------------------------------------
# Android 側（android/ の Gradle プロジェクトへ丸投げする）
# ---------------------------------------------------------------------------
ifeq ($(OS),Windows_NT)
GRADLEW := android/gradlew.bat
else
GRADLEW := android/gradlew
endif

# **必ず引用符で囲んで使うこと。** cmd.exe はコマンド名の中の引用符無しの
# `/` を「スイッチの区切り」と誤解することがあり、
# `android/gradlew.bat -p android ...` をそのまま渡すと cmd が
# `android` という別のコマンド（Android SDK の廃止された android.bat）を
# 探しに行ってしまう（実機で踏んだ）。`"$(GRADLEW)"` のように必ず囲む。

# ABI は gradle.properties の mxv2.abiFilters を -P で上書きする。
# TARGET=android のときは上書きしない（既定の arm64-v8a,armeabi-v7a の
# ままの FAT apk になる）。
GRADLE_ABI_ARG := $(if $(ABI_OVERRIDE),-Pmxv2.abiFilters=$(ABI_OVERRIDE))

APK_DIR := android/app/build/outputs/apk/$(BUILD)
APK_PATH := $(APK_DIR)/app-$(BUILD).apk

# Android には configure の段がない（Gradle がビルドのたびに自分で構成し、
# ネイティブ側の CMake も Gradle が呼ぶ）。`make configure` を TARGET=android
# で叩いたときに「そんなターゲットは無い」と言われないよう、何もしない入口を
# 用意しておく。
.PHONY: configure-android
configure-android:
	@echo mxv2: Android has no separate configure step; Gradle configures on every build.

.PHONY: build-android
build-android:
	"$(GRADLEW)" -p android $(GRADLE_ABI_ARG) assemble$(CONFIG)

.PHONY: clean-android
clean-android:
	"$(GRADLEW)" -p android clean

# release は android/keystore.properties が無いと signingConfig が付かず
# unsigned apk になるので adb install できない（Gradle 自身も installRelease
# タスクを作らない）。BUILD.md の「Android のリリース署名」で鍵を用意すれば
# BUILD=release でも install/run できる。
ifeq ($(BUILD),release)
CAN_INSTALL_ANDROID := $(if $(wildcard android/keystore.properties),1,)
else
CAN_INSTALL_ANDROID := 1
endif

.PHONY: install-android
ifeq ($(CAN_INSTALL_ANDROID),1)
install-android: build-android
	adb install -r "$(APK_PATH)"
	@echo Installed $(APK_PATH)
else
install-android:
	@echo ERROR: TARGET=$(TARGET) BUILD=release cannot be installed.
	@echo   android/keystore.properties was not found, so the release build is
	@echo   unsigned and Android refuses to install it via adb.
	@echo   Use BUILD=debug for install/run, or set up release signing first
	@echo   (see BUILD.md, "Android release signing").
	@exit 1
endif

.PHONY: uninstall-android
uninstall-android:
	"$(GRADLEW)" -p android uninstall$(CONFIG)

.PHONY: run-android
run-android: install-android
	adb shell am force-stop net.gorry.mxv2
	adb shell am start -n net.gorry.mxv2/.MainActivity $(if $(OPTION),--esa args "$(OPTION)")

# Android 側にはまだ自動テストが無い（BUILD.md のとおり）。ここは Gradle の
# （中身が空の）ユニットテストタスクを実行するだけの、将来テストを足すため
# の置き場所。mxv2 のデコードそのものを検証したいなら
# `make test TARGET=win64` の mxv2_chunktest を使うこと（プラットフォーム
# 非依存のロジックなので Windows 側で検証すれば十分）。
.PHONY: test-android
test-android: build-android
	@echo mxv2: no automated Android tests exist yet; running Gradle's unit-test task as a placeholder.
	"$(GRADLEW)" -p android test$(CONFIG)UnitTest

# ---------------------------------------------------------------------------
# リリース用のアーカイブ（make arc）
# ---------------------------------------------------------------------------
# Release/ の下に、配れる形のものを 1 つ作る。名前は
#   <Profile.ini の [Title] ShortText>_<TARGET>_<Profile.ini の [Version] Text>
# で、win32 / win64 なら .zip（実行ファイル + SDL2.dll + assets + NOTICE +
# LICENSE + README.md を、同じ名前のフォルダに入れたもの）、android なら .apk。
# **win64 だけ、スキンエディタの実行ファイル一式と README_SkinEditor.md も
# 同じフォルダに入れる**（ユーザーの指示。スキンエディタは Windows 専用で、
# dotnet が作る apphost は x64 なので win32 には入れない）。
# **BUILD の値に関わらず release でビルドする。**
# zip 化は CMake 内蔵の tar（下の ARC_ZIP_CMD）。
#
# Profile.ini の値は sed で引く（[Section] から次の [ までの範囲で Key= を
# 探す）。sed は GnuWin32 にもある。値が取れなければ止める。
PROFILE_INI := Profile.ini
profile_get = $(strip $(shell sed -n "/^\[$(1)\]/,/^\[/{s/^$(2)=//p;}" $(PROFILE_INI)))
# `=`（再帰展開）にしてあるので、arc を頼まれたときだけ sed が走る。
PROFILE_SHORT = $(call profile_get,Title,ShortText)
PROFILE_VERSION = $(call profile_get,Version,Text)

ARC_DIR := Release
ARC_NAME = $(PROFILE_SHORT)_$(TARGET)_$(PROFILE_VERSION)
ARC_STAGE = $(ARC_DIR)/stage/$(ARC_NAME)
WIN_REL_OUT := $(WIN_BUILD_DIR)/Release

.PHONY: check-profile
check-profile:
	$(if $(PROFILE_SHORT),,$(error Profile.ini: [Title] ShortText could not be read))
	$(if $(PROFILE_VERSION),,$(error Profile.ini: [Version] Text could not be read))
	@echo mxv2: archive name = $(ARC_NAME)

# zip 化は Info-ZIP の zip（GnuWin32 の zip パッケージ。PATH にあること）。
# **-D でフォルダのエントリを入れない**。古い 7-Zip（LhaForge 内蔵の 9.22 など）は
# MS-DOS のディレクトリ属性 (0x10) の無いフォルダのエントリを**ファイルとして
# 取り出す**ので、その下のファイルが "can not open output file" で全部失敗する
# （2026-09-13 にユーザーが踏んだ）。PowerShell 5.1 の Compress-Archive（属性
# 無し）も CMake 内蔵の tar --format=zip（Unix の属性だけ）も駄目だった。
# フォルダのエントリが無ければ、どの展開ツールも親フォルダを自分で作る。
ARC_ZIP_CMD = cmake -E chdir "$(ARC_DIR)/stage" zip -r -D -q "../$(ARC_NAME).zip" "$(ARC_NAME)"

# 素材は build/ 側のコピーではなく、CMakeLists.txt の POST_BUILD と同じ手順で
# **ソースから組み立て直す**（ソースの assets/ + 空の assets/mdx/ + 同梱曲
# third_party/GUSA-CDg/ArctanX があれば assets/mdx/ArctanX/）。build/ 側には
# 名前を変える前のスキンなどの残骸が残ることがあるため。ユーザーが試験用に
# 置く font.ttf（.gitignore 済み）は配布物に入れない。
BUNDLED_MDX_DIR := third_party/GUSA-CDg/ArctanX
ARC_BUNDLED_MDX_CMD = $(if $(wildcard $(BUNDLED_MDX_DIR)),cp -r "$(BUNDLED_MDX_DIR)" "$(ARC_STAGE)/assets/mdx/ArctanX",@echo mxv2: $(BUNDLED_MDX_DIR) not found - no bundled songs)

.PHONY: arc-windows
arc-windows: check-profile $(WIN_BUILD_DIR)/CMakeCache.txt
	cmake --build $(WIN_BUILD_DIR) --config Release --parallel
	rm -rf "$(ARC_STAGE)" "$(ARC_DIR)/$(ARC_NAME).zip"
	$(MKDIR_P) "$(ARC_STAGE)"
	cp "$(WIN_REL_OUT)/mxv2.exe" "$(ARC_STAGE)/mxv2.exe"
	cp "$(WIN_REL_OUT)/SDL2.dll" "$(ARC_STAGE)/SDL2.dll"
	cp -r assets "$(ARC_STAGE)/assets"
	rm -f "$(ARC_STAGE)/assets/font.ttf" $(patsubst assets/%,"$(ARC_STAGE)/assets/%",$(wildcard assets/skin/*/font.ttf))
	$(MKDIR_P) "$(ARC_STAGE)/assets/mdx"
	$(ARC_BUNDLED_MDX_CMD)
	cp NOTICE "$(ARC_STAGE)/NOTICE"
	cp LICENSE "$(ARC_STAGE)/LICENSE"
	cp README.md "$(ARC_STAGE)/README.md"
	$(ARC_SKINEDITOR_BUILD_CMD)
	$(ARC_SKINEDITOR_COPY_CMDS)
	$(ARC_ZIP_CMD)
	rm -rf "$(ARC_DIR)/stage"
	@echo Created $(ARC_DIR)/$(ARC_NAME).zip

# release apk。android/keystore.properties があれば署名済みの app-release.apk、
# 無ければ app-release-unsigned.apk しかできないので、名前に -unsigned を
# 付けて区別する（そのままでは端末に入らない。BUILD.md の署名の節）。
ifneq ($(wildcard android/keystore.properties),)
APK_REL_SRC := android/app/build/outputs/apk/release/app-release.apk
APK_REL_SUFFIX :=
else
APK_REL_SRC := android/app/build/outputs/apk/release/app-release-unsigned.apk
APK_REL_SUFFIX := -unsigned
endif

.PHONY: arc-android
arc-android: check-profile
	"$(GRADLEW)" -p android $(GRADLE_ABI_ARG) assembleRelease
	$(MKDIR_P) "$(ARC_DIR)"
	cp "$(APK_REL_SRC)" "$(ARC_DIR)/$(ARC_NAME)$(APK_REL_SUFFIX).apk"
	@echo Created $(ARC_DIR)/$(ARC_NAME)$(APK_REL_SUFFIX).apk

# win64 と android の両方をまとめて作る。
.PHONY: arc-all
arc-all:
	$(MAKE) arc TARGET=win64
	$(MAKE) arc TARGET=android

# ---------------------------------------------------------------------------
# スキンエディタ（skineditor/ の C# / .NET 8 WinForms アプリ。Windows 専用）
# ---------------------------------------------------------------------------
# mxv2 本体とは別のアプリなので、共通ターゲットには混ぜず、`-skineditor` を
# 後置した名前で呼ぶ（`make build-skineditor` など）。TARGET は見ない
# （dotnet が作る実行ファイルはビルドした機械の .NET に合わせた
# AnyCPU + apphost）。BUILD（debug / release）と PREFIX / OPTION は本体と
# 共通。dotnet SDK 8 以降が PATH にあること。
#
# - build   : dotnet build。csproj の CopySkinEditorToMxv2Root が、ビルドの
#             たびに実行ファイル一式をこのフォルダ（CMakeLists.txt の隣）へも
#             コピーする（開発フォルダモードで exe を直接起動できるように）。
# - install : mxv2 本体と同じ PREFIX へ、実行に要るファイルと
#             README_SkinEditor.md を置く。mxv2.exe の隣に置けば
#             ユーザーフォルダモードで動く。フレームワーク依存なので、実行する
#             機械には .NET 8 の Windows Desktop Runtime が要る（.pdb は入れない）。
# - run     : 開発フォルダ（ここ）をカレントにして起動するので、開発フォルダ
#             モードになる。OPTION で -userdir や起動フォルダを渡せる。
# - test    : skineditor/SkinEditor.Tests の xUnit を dotnet test で走らせる。
# - clean   : 両プロジェクトの bin/ obj/ と、上でこのフォルダへコピーした
#             実行ファイル一式を消す。
SKINEDITOR_DIR := skineditor/SkinEditor
SKINEDITOR_TESTS_DIR := skineditor/SkinEditor.Tests
SKINEDITOR_PROJ := $(SKINEDITOR_DIR)/SkinEditor.csproj
SKINEDITOR_TESTS_PROJ := $(SKINEDITOR_TESTS_DIR)/SkinEditor.Tests.csproj
SKINEDITOR_OUT := $(SKINEDITOR_DIR)/bin/$(CONFIG)/net8.0-windows
SKINEDITOR_EXE := $(SKINEDITOR_OUT)/SkinEditor.exe
# 配布に要るファイル（.pdb は開発用なので含めない）。csproj がこのフォルダへ
# コピーするものも同じ並び（+ .pdb）で、.gitignore に載せてある。
SKINEDITOR_FILES := SkinEditor.exe SkinEditor.dll SkinEditor.deps.json SkinEditor.runtimeconfig.json
SKINEDITOR_ROOT_COPIES := $(SKINEDITOR_FILES) SkinEditor.pdb

.PHONY: build-skineditor
build-skineditor:
	dotnet build "$(SKINEDITOR_PROJ)" -c $(CONFIG) -nologo

.PHONY: clean-skineditor
clean-skineditor:
	rm -rf "$(SKINEDITOR_DIR)/bin" "$(SKINEDITOR_DIR)/obj" "$(SKINEDITOR_TESTS_DIR)/bin" "$(SKINEDITOR_TESTS_DIR)/obj"
	rm -f $(SKINEDITOR_ROOT_COPIES)

.PHONY: test-skineditor
test-skineditor:
	dotnet test "$(SKINEDITOR_TESTS_PROJ)" -c $(CONFIG) -nologo

.PHONY: run-skineditor
run-skineditor: build-skineditor
	"$(SKINEDITOR_EXE)" $(OPTION)

# make arc（win64 のときだけ）で配布物に入れるぶん。arc は release 固定なので
# BUILD に関わらず Release でビルドし、その出力を ARC_STAGE へ写す。
# ARC_STAGE は再帰展開（`=`）なので、こちらも `=` にしておく。
SKINEDITOR_REL_OUT := $(SKINEDITOR_DIR)/bin/Release/net8.0-windows
ifeq ($(TARGET),win64)
ARC_SKINEDITOR_BUILD_CMD = dotnet build "$(SKINEDITOR_PROJ)" -c Release -nologo
ARC_SKINEDITOR_COPY_CMDS = $(foreach f,$(SKINEDITOR_FILES),cp "$(SKINEDITOR_REL_OUT)/$(f)" "$(ARC_STAGE)/$(f)" &&) cp README_SkinEditor.md "$(ARC_STAGE)/README_SkinEditor.md"
else
ARC_SKINEDITOR_BUILD_CMD = @echo mxv2: skin editor is bundled only for TARGET=win64 - skipped
ARC_SKINEDITOR_COPY_CMDS = cd .
endif

SKINEDITOR_INSTALL_CMDS := $(foreach f,$(SKINEDITOR_FILES),cp "$(SKINEDITOR_OUT)/$(f)" "$(PREFIX)/$(f)" &&)
SKINEDITOR_INSTALLED := $(foreach f,$(SKINEDITOR_FILES) README_SkinEditor.md,"$(PREFIX)/$(f)")

.PHONY: install-skineditor
install-skineditor: build-skineditor
	$(MKDIR_P) "$(PREFIX)"
	$(SKINEDITOR_INSTALL_CMDS) cp README_SkinEditor.md "$(PREFIX)/README_SkinEditor.md"
	@echo Installed SkinEditor to $(PREFIX)

.PHONY: uninstall-skineditor
uninstall-skineditor:
	rm -f $(SKINEDITOR_INSTALLED)
	@echo Removed SkinEditor files from $(PREFIX)

# ---------------------------------------------------------------------------
# 共通のターゲット（TARGET に応じて上のどちらかへ振り分けるだけ）
# ---------------------------------------------------------------------------
.PHONY: all configure build install uninstall clean run test arc help

all: build

ifeq ($(PLATFORM),windows)
configure: configure-windows
build: build-windows
install: install-windows
uninstall: uninstall-windows
clean: clean-windows
run: run-windows
test: test-windows
arc: arc-windows
else
configure: configure-android
build: build-android
install: install-android
uninstall: uninstall-android
clean: clean-android
run: run-android
test: test-android
arc: arc-android
endif

help:
	@echo mxv2 Makefile
	@echo -----------------------------------------------------------------
	@echo Targets:
	@echo   build      build the selected TARGET/BUILD (default)
	@echo   all        same as build
	@echo   configure  win32/win64: (re-)run cmake to create build/^<TARGET^>
	@echo              android: nothing to do (Gradle configures on each build)
	@echo   install    install the built target
	@echo              (win32/win64: copy into PREFIX; android: adb install,
	@echo              debug only - see notes below)
	@echo   uninstall  remove what install put there
	@echo   clean      remove build output for the selected TARGET
	@echo   run        run the built target, with OPTION appended
	@echo   test       win32/win64: run mxv2_chunktest against testdata/
	@echo              android: placeholder Gradle unit test (see notes)
	@echo   arc        release build, then put a distributable into Release/:
	@echo              win32/win64: ^<ShortText^>_^<TARGET^>_^<Version^>.zip
	@echo              android:     ^<ShortText^>_^<TARGET^>_^<Version^>.apk
	@echo              (ShortText / Version come from Profile.ini;
	@echo              win64 also bundles the skin editor + README_SkinEditor.md)
	@echo   arc-all    arc for TARGET=win64 and TARGET=android
	@echo   help       show this
	@echo Skin editor (skineditor/, C# .NET 8, Windows only; TARGET is ignored):
	@echo   build-skineditor      dotnet build (BUILD selects Debug/Release)
	@echo   clean-skineditor      remove bin/ obj/ and the copies in this folder
	@echo   test-skineditor       dotnet test (skineditor/SkinEditor.Tests)
	@echo   run-skineditor        start SkinEditor.exe here (dev-folder mode),
	@echo                         with OPTION appended
	@echo   install-skineditor    copy SkinEditor + README_SkinEditor.md into
	@echo                         PREFIX (next to mxv2.exe = user-folder mode)
	@echo   uninstall-skineditor  remove what install-skineditor put there
	@echo -----------------------------------------------------------------
	@echo Switches (set as VAR=value on the command line):
	@echo   TARGET   win32 / win64 / android / android-arm32 / android-arm64
	@echo            default: win64
	@echo   BUILD    debug / release
	@echo            default: debug
	@echo   OPTION   extra command line for `run`
	@echo            (mxv2.exe args on Windows; passed via --esa args on
	@echo            Android)  default: (empty)
	@echo   PREFIX   install/uninstall location for win32/win64 only
	@echo            default: dist/^<TARGET^>-^<BUILD^>
	@echo -----------------------------------------------------------------
	@echo Examples:
	@echo   make
	@echo   make build TARGET=win32 BUILD=release
	@echo   make run OPTION=-skin Default
	@echo   make test TARGET=win64
	@echo   make install TARGET=win64 BUILD=release PREFIX=C:/mxv2
	@echo   make build TARGET=android-arm64
	@echo   make run TARGET=android
	@echo   make test-skineditor
	@echo   make install-skineditor BUILD=release PREFIX=C:/mxv2
	@echo -----------------------------------------------------------------
	@echo Current selection: TARGET=$(TARGET) BUILD=$(BUILD)
	@echo Notes:
	@echo   - Windows targets use the CMake Visual Studio generator (no need
	@echo     to run this from a "Developer Command Prompt"; it locates MSVC
	@echo     on its own). Requires cmake and Visual Studio to be installed.
	@echo   - Android targets shell out to android/gradlew. Requires the
	@echo     Android SDK/NDK and android/local.properties to be set up
	@echo     first (see BUILD.md); adb must be on PATH for install/run.
	@echo   - The skin editor targets need the dotnet SDK (8 or later) on
	@echo     PATH; running the installed SkinEditor.exe needs the .NET 8
	@echo     Windows Desktop Runtime.
	@echo   - Android BUILD=release installs/runs only if android/keystore.
	@echo     properties is set up (see BUILD.md, "Android release signing").
	@echo     Without it, assembleRelease still builds an unsigned apk, but
	@echo     install/run refuse it (Android itself refuses to install an
	@echo     unsigned apk).
