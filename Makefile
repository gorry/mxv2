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
# 「実行ファイル + SDL2.dll + assets + NOTICE + LICENSE」という、そのまま
# コピーして配れる移植版の姿を PREFIX の下に作るだけ。開発用のツール
# （mxv2_chunktest.exe など）や .pdb は対象に含めない。
.PHONY: install-windows
install-windows: build-windows
	mkdir -p "$(PREFIX)"
	cp "$(EXE_MAIN)" "$(PREFIX)/mxv2.exe"
	cp "$(WIN_OUT_DIR)/SDL2.dll" "$(PREFIX)/SDL2.dll"
	rm -rf "$(PREFIX)/assets"
	cp -r "$(WIN_OUT_DIR)/assets" "$(PREFIX)/assets"
	cp NOTICE "$(PREFIX)/NOTICE"
	cp LICENSE "$(PREFIX)/LICENSE"
	@echo Installed to $(PREFIX)

.PHONY: uninstall-windows
uninstall-windows:
	rm -rf "$(PREFIX)/mxv2.exe" "$(PREFIX)/SDL2.dll" "$(PREFIX)/assets" "$(PREFIX)/NOTICE" "$(PREFIX)/LICENSE"
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
# 共通のターゲット（TARGET に応じて上のどちらかへ振り分けるだけ）
# ---------------------------------------------------------------------------
.PHONY: all configure build install uninstall clean run test help

all: build

ifeq ($(PLATFORM),windows)
configure: configure-windows
build: build-windows
install: install-windows
uninstall: uninstall-windows
clean: clean-windows
run: run-windows
test: test-windows
else
configure: configure-android
build: build-android
install: install-android
uninstall: uninstall-android
clean: clean-android
run: run-android
test: test-android
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
	@echo   help       show this
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
	@echo -----------------------------------------------------------------
	@echo Current selection: TARGET=$(TARGET) BUILD=$(BUILD)
	@echo Notes:
	@echo   - Windows targets use the CMake Visual Studio generator (no need
	@echo     to run this from a "Developer Command Prompt"; it locates MSVC
	@echo     on its own). Requires cmake and Visual Studio to be installed.
	@echo   - Android targets shell out to android/gradlew. Requires the
	@echo     Android SDK/NDK and android/local.properties to be set up
	@echo     first (see BUILD.md); adb must be on PATH for install/run.
	@echo   - Android BUILD=release installs/runs only if android/keystore.
	@echo     properties is set up (see BUILD.md, "Android release signing").
	@echo     Without it, assembleRelease still builds an unsigned apk, but
	@echo     install/run refuse it (Android itself refuses to install an
	@echo     unsigned apk).
