cmake_minimum_required(VERSION 3.20)
# Big Sur 以降。指定済みの deployment target は尊重する。
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
	set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(mxv2 LANGUAGES C CXX)

if(APPLE)
	# SDL2 の .m と mxv2 の .mm を別々の言語として扱う。
	enable_language(OBJC OBJCXX)
	set(CMAKE_OBJCXX_STANDARD 11)
	set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

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
if(ANDROID OR APPLE)

set(SDL2_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL2-2.32.10-src" CACHE PATH
	"SDL2 のソースのルートディレクトリ (Android / macOS で使う)")

if(NOT EXISTS "${SDL2_SRC_ROOT}/CMakeLists.txt")
	message(FATAL_ERROR "SDL2 のソースが見つかりません: ${SDL2_SRC_ROOT}")
endif()

# Android は APK 用の共有版、macOS は自己完結した .app 用の静的版。
if(APPLE)
	# .app だけをコピーして起動できるよう、SDL2 は静的リンクする。
	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
else()
	set(SDL_SHARED ON CACHE BOOL "" FORCE)
	set(SDL_STATIC OFF CACHE BOOL "" FORCE)
endif()
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
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
	src/appsetup.cpp
	src/cmdline.cpp
	src/keybind.cpp
	src/playctl.cpp
	src/assetpath.cpp
	src/bitmap.cpp
	src/bmpfile.cpp
	src/colors.cpp
	src/dirlister.cpp
	src/drawscreen.cpp
	src/filer.cpp
	src/fileutil.cpp
	src/gamepad.cpp
	src/httpget.cpp
	src/ini.cpp
	src/kinsoku.cpp
	src/mdxsong.cpp
	src/message.cpp
	src/mouse.cpp
	src/nowplaying.cpp
	src/openintent.cpp
	src/orientlock.cpp
	src/outputlatency.cpp
	src/pip.cpp
	src/player.cpp
	src/safaccess.cpp
	src/screen.cpp
	src/settings.cpp
	src/settingsui.cpp
	src/settingsui_settings.cpp
	src/settingsui_colors.cpp
	src/settingsui_filesystems.cpp
	src/settingsui_bookmarks.cpp
	src/settingsui_pdx.cpp
	src/settingsui_folder.cpp
	src/settingsui_menu.cpp
	src/settingsui_info.cpp
	src/settingsui_update.cpp
	src/settingsui_cast.cpp
	src/cast.cpp
	src/cast.h
	src/singleinstance.cpp
	src/skin.cpp
	src/songloader.cpp
	src/statuswatch.cpp
	src/text.cpp
	src/textlayer.cpp
	src/textrender.cpp
	src/tutorial.cpp
	src/updatecheck.cpp
	src/vfs.cpp
	src/visualizer.cpp
	src/appsetup.h
	src/assetpath.h
	src/cmdline.h
	src/keybind.h
	src/playctl.h
	src/bitmap.h
	src/bmpfile.h
	src/colors.h
	src/cp932table.h
	src/dirlister.h
	src/dispqueue.h
	src/drawscreen.h
	src/filer.h
	src/fileutil.h
	src/gamepad.h
	src/httpget.h
	src/ini.h
	src/kinsoku.h
	src/mdxsong.h
	src/message.h
	src/mouse.h
	src/mxdrvptr.h
	src/nowplaying.h
	src/openintent.h
	src/orientlock.h
	src/pip.h
	src/safaccess.h
	src/player.h
	src/screen.h
	src/settings.h
	src/settingsui.h
	src/settingsui_internal.h
	src/singleinstance.h
	src/skin.h
	src/songloader.h
	src/statuswatch.h
	src/text.h
	src/textlayer.h
	src/textrender.h
	src/tutorial.h
	src/updatecheck.h
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
# デバッグ版の目印。Windows は実行ファイル名（mxv2_<suffix>.exe）、Android は
# applicationId の末尾（Gradle が付ける）に使う。
mxv2_profile_get(AppId DebugSuffix MXV2_APPID_DEBUG_SUFFIX)
# [URL] … バージョン情報から開く GitHub のページと NOTICE。
mxv2_profile_get(URL Project MXV2_URL_PROJECT)
mxv2_profile_get(URL Notice MXV2_URL_NOTICE)
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
	target_link_libraries(mxv2 PRIVATE ole32 shell32 cfgmgr32)
	# 更新チェックの HTTPS (src/httpget.cpp)
	target_link_libraries(mxv2 PRIVATE winhttp)
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
	# デバッグ構成の実行ファイルは mxv2_<DebugSuffix>.exe（Profile.ini の [AppId]
	# DebugSuffix）。配布物の mxv2.exe と見分けるため（2026-09-16、ユーザーの指示）。
	set_target_properties(mxv2 PROPERTIES OUTPUT_NAME_DEBUG "mxv2_${MXV2_APPID_DEBUG_SUFFIX}")
endif()
if(MSVC)
	target_compile_definitions(mxv2 PRIVATE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(mxv2 PRIVATE /W3)
endif()

# ---------------------------------------------------------------------------
# Chromecast への送信（memo/cast.md。メニューの [表示] → [キャスト…]）
#   sdlcastg/（git の submodule。https://github.com/gorry/sdlcastg）を取り込む。mbedTLS と、
#   vcpkg で作る libvpx / opus / libyuv が要る（BUILD.md）。置き場所は mxv2 の third_party/ を渡す。
#   submodule を取ってこずに clone した（sdlcastg/ が空の）ときは、AUTO なら組み込まない。
#   MXV2_CAST は AUTO（既定。それらが置いてあれば組み込む）/ ON / OFF。
#   組み込まないときはメニューに出ない（src/cast.cpp が空の実装になる）。
# ---------------------------------------------------------------------------
set(MXV2_CAST "AUTO" CACHE STRING "Chromecast への送信（sdlcastg）を組み込む: AUTO / ON / OFF")
set_property(CACHE MXV2_CAST PROPERTY STRINGS AUTO ON OFF)
set(_mxv2_cast OFF)
if(MXV2_CAST STREQUAL "AUTO")
	# sdlcastg/CMakeLists.txt と同じ triplet の決め方。
	if(WIN32)
		set(_mxv2_cast_triplet "x64-windows-static-md")
	elseif(ANDROID_ABI STREQUAL "arm64-v8a")
		set(_mxv2_cast_triplet "arm64-android")
	elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
		set(_mxv2_cast_triplet "arm-neon-android")
	else()
		set(_mxv2_cast_triplet "none")
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdlcastg/CMakeLists.txt" AND
	   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls-3.6.7/CMakeLists.txt" AND
	   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcpkg_installed/${_mxv2_cast_triplet}/include/vpx")
		set(_mxv2_cast ON)
	endif()
elseif(MXV2_CAST)
	set(_mxv2_cast ON)
endif()
if(_mxv2_cast AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdlcastg/CMakeLists.txt")
	message(FATAL_ERROR "mxv2: sdlcastg/ が空です。submodule を取ってきてください"
	        "（git submodule update --init、TortoiseGit なら Submodule Update。BUILD.md）")
endif()
if(_mxv2_cast)
	message(STATUS "mxv2: Chromecast への送信を組み込む（sdlcastg）")
	# 依存の置き場所は mxv2 の third_party/（sdlcastg の既定は sdlcastg/third_party/）。
	# SDL2 は mxv2 の sdl2 ターゲットをそのまま使う。見本は作らない（既定で OFF）。
	set(SDLCASTG_MBEDTLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls-3.6.7")
	set(SDLCASTG_VCPKG_INSTALLED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vcpkg_installed")
	add_subdirectory(sdlcastg sdlcastg-build)
	target_link_libraries(mxv2 PRIVATE sdlcastg_sdl)
	target_compile_definitions(mxv2 PRIVATE MXV2_CAST)
else()
	message(STATUS "mxv2: Chromecast への送信は組み込まない（MXV2_CAST=${MXV2_CAST}）")
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

# ---------------------------------------------------------------------------
# mxv2_benchmark（tools/benchmark.cpp）
#   演奏時間の測定 (MXDRV_MeasurePlayTime2) とシーク (MXDRV_PlayAt) の負荷、
#   および空回し中に x68sound のコマンドバッファへ積まれる OPM 書き込みの量を
#   測る。Makefile の build-benchmark / run-benchmark から呼ぶ。SDL 不要。
#
#   portable_mdx の内部ヘッダ（コマンドバッファの水位を覗くため）を読むので、
#   src/ に加えて mxdrv / x68sound のソースディレクトリも include に足す。
# ---------------------------------------------------------------------------
# この検証ツールだけはコマンドバッファ拡張 API が必須。
# 拡張の無い portable_mdx でもプレーヤーと波形検証はビルドできる。
file(STRINGS "${PORTABLE_MDX_DIR}/include/mxdrv.h" _mxv2_buffer_api
	REGEX "^#define[ \t]+MXDRV_SUPPORT_ADJUST_X68SOUND_COMMAND_BUFFER_SIZE")
if(_mxv2_buffer_api)
add_executable(mxv2_benchmark
	tools/benchmark.cpp
	src/fileutil.cpp
	src/ini.cpp
	src/message.cpp
	src/text.cpp
	src/mdxsong.cpp
	src/vfs.cpp
	src/safaccess.cpp
)
target_include_directories(mxv2_benchmark PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${PORTABLE_MDX_DIR}/src/mxdrv"
	"${PORTABLE_MDX_DIR}/src/x68sound"
)
target_link_libraries(mxv2_benchmark PRIVATE portable_mdx)
if(MSVC)
	target_compile_definitions(mxv2_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
else()
	message(STATUS "mxv2: portable_mdx に拡張 API が無いため benchmark を省略")
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

# macOS では素材と NOTICE を Contents/Resources にまとめる。
if(APPLE)
	set(MXV2_RESOURCE_DIR "$<TARGET_BUNDLE_CONTENT_DIR:mxv2>/Resources")
else()
	set(MXV2_RESOURCE_DIR "$<TARGET_FILE_DIR:mxv2>")
endif()

# 素材ビットマップとスキンを同梱素材の根の assets/ へ置く
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/assets" "${MXV2_RESOURCE_DIR}/assets"
)

# 曲の置き場所 assets/mdx（ファイラーの "Assets>" が根に据えるフォルダ）。
# 空でも作る。無いとファイラーが空の一覧になるだけだが、置き場所として
# 見せておきたい。
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${MXV2_RESOURCE_DIR}/assets/mdx"
)

# 同梱の MDX（ArctanX 氏の許諾済み。詳細は NOTICE と BUILD.md）。
# リポジトリには含めないので、**無ければ何もしない**。
set(BUNDLED_MDX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/GUSA-CDg/ArctanX" CACHE PATH
	"同梱する MDX の置き場所（無ければコピーしない）")
if(EXISTS "${BUNDLED_MDX_DIR}")
	add_custom_command(TARGET mxv2 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${BUNDLED_MDX_DIR}" "${MXV2_RESOURCE_DIR}/assets/mdx/ArctanX"
	)
endif()

# LICENSE と NOTICE と README.md も実行ファイルの隣へ。NOTICE は「バージョン
# 情報」が実行時に読んで表示するので、配布物にも必ず入れること。README.md は
# 配布物の説明書（2026-09-13 に追加。バイナリパッケージでも同じ場所に置く）。
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/NOTICE" "${MXV2_RESOURCE_DIR}/NOTICE"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${MXV2_RESOURCE_DIR}/LICENSE"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${MXV2_RESOURCE_DIR}/README.md"
)

endif()

if(APPLE)
	include(cmake/macos.cmake)
endif()
