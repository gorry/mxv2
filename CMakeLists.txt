cmake_minimum_required(VERSION 3.20)
project(mxv2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 文字コードは全ソース UTF-8 に統一する。
# MSVC は既定でナロー文字列リテラルをシステム ANSI (CP932) に再エンコードして
# しまうため、/utf-8 を明示しないと SDL_ttf に渡す文字列が壊れる。
# ---------------------------------------------------------------------------
if(MSVC)
	add_compile_options(/utf-8)
else()
	add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ---------------------------------------------------------------------------
# portable_mdx (演奏モジュール)
#   原典は読み取り専用扱いとし、ここから参照するだけで一切改変しない。
# ---------------------------------------------------------------------------
set(PORTABLE_MDX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/portable_mdx" CACHE PATH
	"portable_mdx のルートディレクトリ")

if(NOT EXISTS "${PORTABLE_MDX_DIR}/include/mxdrv.h")
	message(FATAL_ERROR "portable_mdx が見つかりません: ${PORTABLE_MDX_DIR}")
endif()

add_library(portable_mdx STATIC
	"${PORTABLE_MDX_DIR}/src/mdx_util.c"
	"${PORTABLE_MDX_DIR}/src/mxdrv/mxdrv.cpp"
	"${PORTABLE_MDX_DIR}/src/mxdrv/mxdrv_context.cpp"
	"${PORTABLE_MDX_DIR}/src/mxdrv/sound_iocs.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound_adpcm.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound_context.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound_lfo.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound_op.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound_opm.cpp"
	"${PORTABLE_MDX_DIR}/src/x68sound/x68sound_pcm8.cpp"
)
target_include_directories(portable_mdx PUBLIC "${PORTABLE_MDX_DIR}/include")

if(MSVC)
	# 原典コードは今の警告基準では大量に警告が出るため、ここだけ緩める。
	target_compile_options(portable_mdx PRIVATE /W0 /wd4996)
	target_compile_definitions(portable_mdx PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# SDL2
#   third_party/SDL2-2.32.10 (公式の VC 開発用パッケージ) を使う。
#   portable_mdx 同梱の 2.0.7 では Dear ImGui の SDL_Renderer バックエンドが
#   要求する SDL_RenderGeometry (2.0.18 以降) が無いため差し替えた。
#   SDL2_ROOT を指定すれば別の SDL2 に切り替えられる。
#
#   Android には配布バイナリが無いので、ソースからビルドする。Windows のビルドが
#   使う third_party/SDL2-2.32.10 は VC 用パッケージで include と lib しか無いため、
#   ソースは別フォルダ (SDL2_SRC_ROOT) に置いて両者を混ぜない。
# ---------------------------------------------------------------------------
if(ANDROID)

set(SDL2_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.32.10-src" CACHE PATH
	"SDL2 のソースのルートディレクトリ (Android で使う)")

if(NOT EXISTS "${SDL2_SRC_ROOT}/CMakeLists.txt")
	message(FATAL_ERROR "SDL2 のソースが見つかりません: ${SDL2_SRC_ROOT}")
endif()

# APK に入れるのは libSDL2.so だけでよい。静的版とテスト類は作らない。
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_SRC_ROOT}" sdl2-build)

add_library(sdl2 INTERFACE)
target_link_libraries(sdl2 INTERFACE SDL2::SDL2 SDL2::SDL2main)

else()

set(SDL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.32.10" CACHE PATH
	"SDL2 のルートディレクトリ")

add_library(sdl2 INTERFACE)
target_include_directories(sdl2 INTERFACE "${SDL2_ROOT}/include")
if(WIN32)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(SDL2_LIB_DIR "${SDL2_ROOT}/lib/x64")
	else()
		set(SDL2_LIB_DIR "${SDL2_ROOT}/lib/x86")
	endif()
	target_link_libraries(sdl2 INTERFACE
		"${SDL2_LIB_DIR}/SDL2.lib"
		"${SDL2_LIB_DIR}/SDL2main.lib"
	)
else()
	find_package(SDL2 REQUIRED)
	target_link_libraries(sdl2 INTERFACE SDL2::SDL2 SDL2::SDL2main)
endif()

endif()

# ---------------------------------------------------------------------------
# Dear ImGui (設定 UI)
#   third_party/imgui に v1.92.4 のコアと SDL2 / SDL_Renderer バックエンドだけを
#   置いてある。原典同様、こちらからは改変しない。
# ---------------------------------------------------------------------------
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

add_library(imgui STATIC
	"${IMGUI_DIR}/imgui.cpp"
	"${IMGUI_DIR}/imgui_draw.cpp"
	"${IMGUI_DIR}/imgui_tables.cpp"
	"${IMGUI_DIR}/imgui_widgets.cpp"
	"${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
	"${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp"
)
target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_link_libraries(imgui PUBLIC sdl2)
if(MSVC)
	target_compile_options(imgui PRIVATE /W0)
	target_compile_definitions(imgui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---------------------------------------------------------------------------
# Phase 0: 土台検証用。portable_mdx 付属サンプルをそのままビルドする。
#   ここが鳴らなければ以降の全てが成立しないため、最初に確認する。
#   Android は APK に共有ライブラリしか入らないので作らない。
# ---------------------------------------------------------------------------
if(NOT ANDROID)

add_executable(simple_mdx_player
	"${PORTABLE_MDX_DIR}/examples/simple_mdx_player/main.c"
)
target_link_libraries(simple_mdx_player PRIVATE portable_mdx sdl2)
if(MSVC)
	target_compile_definitions(simple_mdx_player PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# WAV 書き出し版。音が出ているかを機械的に検証するのに使う（SDL 不要）。
add_executable(simple_mdx2wav
	"${PORTABLE_MDX_DIR}/examples/simple_mdx2wav/main.c"
)
target_link_libraries(simple_mdx2wav PRIVATE portable_mdx)
if(MSVC)
	target_compile_definitions(simple_mdx2wav PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

endif()

# ---------------------------------------------------------------------------
# mxv2 本体
# ---------------------------------------------------------------------------
set(MXV2_SOURCES
	src/main.cpp
	src/assetpath.cpp
	src/bitmap.cpp
	src/bmpfile.cpp
	src/colors.cpp
	src/dirlister.cpp
	src/drawscreen.cpp
	src/filer.cpp
	src/fileutil.cpp
	src/ini.cpp
	src/kinsoku.cpp
	src/mdxsong.cpp
	src/message.cpp
	src/mouse.cpp
	src/nowplaying.cpp
	src/orientlock.cpp
	src/player.cpp
	src/safaccess.cpp
	src/screen.cpp
	src/settings.cpp
	src/settingsui.cpp
	src/skin.cpp
	src/songloader.cpp
	src/statuswatch.cpp
	src/text.cpp
	src/textlayer.cpp
	src/textrender.cpp
	src/tutorial.cpp
	src/vfs.cpp
	src/visualizer.cpp
	src/assetpath.h
	src/bitmap.h
	src/bmpfile.h
	src/colors.h
	src/cp932table.h
	src/dirlister.h
	src/dispqueue.h
	src/drawscreen.h
	src/filer.h
	src/fileutil.h
	src/ini.h
	src/kinsoku.h
	src/mdxsong.h
	src/message.h
	src/mouse.h
	src/mxdrvptr.h
	src/nowplaying.h
	src/orientlock.h
	src/safaccess.h
	src/player.h
	src/screen.h
	src/settings.h
	src/settingsui.h
	src/skin.h
	src/songloader.h
	src/statuswatch.h
	src/text.h
	src/textlayer.h
	src/textrender.h
	src/tutorial.h
	src/vfs.h
	src/visualizer.h
)

# apk の assets を内部ストレージへ展開する係。Android でしか意味を持たない
# ので、他のプラットフォームでは足さない。
if(ANDROID)
	list(APPEND MXV2_SOURCES src/androidassets.cpp src/androidassets.h)
endif()

# ---- Profile.ini（アプリの名前・版・著作権。著作者専用） --------------------
# 値はここにだけあり、ビルド時に src/appprofile.h.in → appprofile.h（main.cpp
# の kApp*）と res/mxv2.rc.in → mxv2.rc（Windows の VERSIONINFO）へ写す。
# Android の versionName などは android/app/build.gradle が同じファイルを読む。
# Profile.ini を変えたら configure し直す（CMAKE_CONFIGURE_DEPENDS）。
set(MXV2_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/Profile.ini")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MXV2_PROFILE}")
file(READ "${MXV2_PROFILE}" _mxv2_profile_text)
# 行に割る。";" は CMake のリストの区切りなので先に守る（コメント行に出る）。
string(REPLACE ";" "\\;" _mxv2_profile_text "${_mxv2_profile_text}")
string(REGEX REPLACE "\r?\n" ";" _mxv2_profile_lines "${_mxv2_profile_text}")
function(mxv2_profile_get section key outvar)
	set(cur "")
	set(found "")
	foreach(line IN LISTS _mxv2_profile_lines)
		if(line MATCHES "^[ \t]*\\[([^]]+)\\]")
			set(cur "${CMAKE_MATCH_1}")
		elseif(line MATCHES "^[ \t]*[;#]")
			# コメント
		elseif(cur STREQUAL "${section}" AND line MATCHES "^[ \t]*${key}[ \t]*=(.*)$")
			string(STRIP "${CMAKE_MATCH_1}" found)
		endif()
	endforeach()
	if(found STREQUAL "")
		message(FATAL_ERROR "Profile.ini: [${section}] ${key} がありません")
	endif()
	set(${outvar} "${found}" PARENT_SCOPE)
endfunction()
mxv2_profile_get(Title Text MXV2_TITLE_TEXT)
mxv2_profile_get(Title ShortText MXV2_TITLE_SHORT_TEXT)
mxv2_profile_get(Version Text MXV2_VERSION_TEXT)
mxv2_profile_get(Version Number MXV2_VERSION_NUMBER)
mxv2_profile_get(Copyright Text MXV2_COPYRIGHT_TEXT)
mxv2_profile_get(Copyright Name MXV2_COPYRIGHT_NAME)
# [AppId] は Android の namespace / applicationId が主（Gradle が読む）。
# Windows のぶんは使い道が無いが、定義としてヘッダに出しておく。
mxv2_profile_get(AppId Namespace MXV2_APPID_NAMESPACE)
mxv2_profile_get(AppId Android MXV2_APPID_ANDROID)
mxv2_profile_get(AppId Windows MXV2_APPID_WINDOWS)
# VERSIONINFO 用の "a,b,c,d"。[Version] Text を "." で割り、先頭の 0 を落とし
# （0820 → 820）、数字でなければ 0、4 つに満たなければ 0 で埋める。
string(REPLACE "." ";" _mxv2_ver_parts "${MXV2_VERSION_TEXT}")
set(_mxv2_ver_nums "")
foreach(p IN LISTS _mxv2_ver_parts)
	string(REGEX REPLACE "^0+([0-9])" "\\1" p "${p}")
	if(NOT p MATCHES "^[0-9]+$")
		set(p 0)
	endif()
	list(APPEND _mxv2_ver_nums "${p}")
endforeach()
list(LENGTH _mxv2_ver_nums _mxv2_ver_n)
while(_mxv2_ver_n LESS 4)
	list(APPEND _mxv2_ver_nums 0)
	list(LENGTH _mxv2_ver_nums _mxv2_ver_n)
endwhile()
list(SUBLIST _mxv2_ver_nums 0 4 _mxv2_ver_nums)
list(JOIN _mxv2_ver_nums "," MXV2_RC_VERSION_COMMA)
message(STATUS "Profile.ini: ${MXV2_TITLE_TEXT} / ${MXV2_VERSION_TEXT} (${MXV2_RC_VERSION_COMMA}) / ${MXV2_COPYRIGHT_TEXT}")

set(MXV2_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(src/appprofile.h.in "${MXV2_GENERATED_DIR}/appprofile.h" @ONLY)

# 実行ファイルに埋めるアイコンと VERSIONINFO。SDL2 はウィンドウクラスの
# アイコンとして「最初のアイコンリソース」を拾うので、これだけで
# エクスプローラにもウィンドウにもタスクバーにも効く（res/mxv2.rc.in から
# 生成する。アイコンの場所は絶対パスで埋める）。
# Android のアイコンは android/app/src/main/res/mipmap-* 側。
if(WIN32)
	set(MXV2_RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
	configure_file(res/mxv2.rc.in "${MXV2_GENERATED_DIR}/mxv2.rc" @ONLY)
	list(APPEND MXV2_SOURCES "${MXV2_GENERATED_DIR}/mxv2.rc")
endif()

# Android では実行ファイルではなく共有ライブラリを作る。SDLActivity が
# getLibraries() の最後の名前 (= mxv2) を dlopen し、SDL_main を呼ぶ。
if(ANDROID)
	add_library(mxv2 SHARED ${MXV2_SOURCES})
else()
	add_executable(mxv2 ${MXV2_SOURCES})
endif()

target_include_directories(mxv2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${MXV2_GENERATED_DIR}")
target_link_libraries(mxv2 PRIVATE portable_mdx sdl2 imgui)
if(WIN32)
	# OS の「フォルダを探す」ダイアログ (IFileOpenDialog)
	target_link_libraries(mxv2 PRIVATE ole32 shell32)
endif()
if(ANDROID)
	# printf を logcat へ流す (__android_log_write)
	target_link_libraries(mxv2 PRIVATE log)
endif()

# Windows では GUI アプリとしてリンクする。コンソールアプリのままだと、
# エクスプローラから起動したときに黒いコンソールウィンドウが一緒に出る。
# main() は SDL2main が用意する WinMain から呼ばれるので、書き方は変わらない。
# ログを見たいときは -console を付ける（src/main.cpp の SetupConsole）。
if(WIN32)
	set_target_properties(mxv2 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(MSVC)
	target_compile_definitions(mxv2 PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(mxv2 PRIVATE /W3)
endif()

# ---------------------------------------------------------------------------
# 検証用ツール
#   mxv2 のデコード分割が波形を変えないことを確認する。SDL 不要。
# ---------------------------------------------------------------------------
if(NOT ANDROID)

add_executable(mxv2_chunktest
	tools/chunktest.cpp
	src/fileutil.cpp
	src/ini.cpp
	src/message.cpp
	src/text.cpp
	src/mdxsong.cpp
	src/vfs.cpp
	# vfs.cpp が saf: のマウントを作れるようにするため。Android 以外では
	# 「作れない」と返すだけの空の実装になる（SDL も JNI も要らない）。
	src/safaccess.cpp
)
target_include_directories(mxv2_chunktest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mxv2_chunktest PRIVATE portable_mdx)
if(MSVC)
	target_compile_definitions(mxv2_chunktest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

endif()

# SDL2.dll を実行ファイルの隣に置く
if(WIN32)
	foreach(_target simple_mdx_player mxv2)
		add_custom_command(TARGET ${_target} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${SDL2_LIB_DIR}/SDL2.dll" "$<TARGET_FILE_DIR:${_target}>"
		)
	endforeach()
endif()

# ここから下は「実行ファイルの隣」に素材を並べる話なので、Android では行わない。
# APK の assets の届け方は Phase C で別に用意する。
if(NOT ANDROID)

# 素材ビットマップとスキンを実行ファイルの隣の assets/ へ置く
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:mxv2>/assets"
)

# 曲の置き場所 assets/mdx（ファイラーの "Assets>" が根に据えるフォルダ）。
# 空でも作る。無いとファイラーが空の一覧になるだけだが、置き場所として
# 見せておきたい。
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mxv2>/assets/mdx"
)

# 同梱の MDX（ArctanX 氏の許諾済み。詳細は NOTICE と BUILD.md）。
# リポジトリには含めないので、**無ければ何もしない**。
set(BUNDLED_MDX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GUSA-CDg/ArctanX" CACHE PATH
	"同梱する MDX の置き場所（無ければコピーしない）")
if(EXISTS "${BUNDLED_MDX_DIR}")
	add_custom_command(TARGET mxv2 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${BUNDLED_MDX_DIR}" "$<TARGET_FILE_DIR:mxv2>/assets/mdx/ArctanX"
	)
endif()

# LICENSE と NOTICE と README.md も実行ファイルの隣へ。NOTICE は「バージョン
# 情報」が実行時に読んで表示するので、配布物にも必ず入れること。README.md は
# 配布物の説明書（2026-09-13 に追加。バイナリパッケージでも同じ場所に置く）。
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/NOTICE" "$<TARGET_FILE_DIR:mxv2>/NOTICE"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:mxv2>/LICENSE"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/README.md" "$<TARGET_FILE_DIR:mxv2>/README.md"
)

endif()
