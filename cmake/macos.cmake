# SDL2 は静的リンク、Cocoa / libcurl は macOS のシステムライブラリを使う。
find_library(MXV2_APPKIT AppKit REQUIRED)
# Homebrew の curl を拾うと実行先にもその dylib が必要になる。
# ビルドに使う macOS SDK のシステム libcurl を明示して選ぶ。
execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
	OUTPUT_VARIABLE _mxv2_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE
	COMMAND_ERROR_IS_FATAL ANY
)
find_library(MXV2_CURL curl PATHS "${_mxv2_macos_sdk}/usr/lib" NO_DEFAULT_PATH REQUIRED)
add_library(mxv2_macos STATIC src/macosfileutil.mm)
target_include_directories(mxv2_macos PRIVATE src)
target_compile_options(mxv2_macos PRIVATE -fobjc-arc)
target_link_libraries(mxv2_macos PUBLIC "${MXV2_APPKIT}")
foreach(target mxv2 mxv2_chunktest mxv2_benchmark)
	if(TARGET ${target})
		target_link_libraries(${target} PRIVATE mxv2_macos)
	endif()
endforeach()
target_link_libraries(mxv2 PRIVATE "${MXV2_CURL}")

# Profile.ini は変更せず、macOS 用 identifier は独立したビルド設定にする。
set(MXV2_MACOS_BUNDLE_ID "app.kakikou.mxv2-mac" CACHE STRING "macOS bundle identifier")
list(GET _mxv2_ver_nums 0 _mxv2_mac_major)
list(GET _mxv2_ver_nums 1 _mxv2_mac_minor)
list(GET _mxv2_ver_nums 2 _mxv2_mac_patch)
set(MXV2_MACOS_VERSION "${_mxv2_mac_major}.${_mxv2_mac_minor}.${_mxv2_mac_patch}")
set_target_properties(mxv2 PROPERTIES
	MACOSX_BUNDLE TRUE
	MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/res/macos/Info.plist.in"
)

set(_mxv2_icon "${MXV2_GENERATED_DIR}/mxv2.icns")
add_custom_command(OUTPUT "${_mxv2_icon}"
	COMMAND /bin/bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_macos_icon.sh"
		"${CMAKE_CURRENT_SOURCE_DIR}/icon/icon_mxv2.png" "${_mxv2_icon}"
	DEPENDS icon/icon_mxv2.png tools/make_macos_icon.sh
	VERBATIM
)
set_source_files_properties("${_mxv2_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(mxv2 PRIVATE "${_mxv2_icon}")

# 静的 SDL2 のライセンスもアプリと一緒に届ける。
set(MXV2_MACOS_LICENSE_OUTPUT "${MXV2_GENERATED_DIR}/macos-LICENSE")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_license.cmake")
# 原文更新時も configure / link をやり直し、POST_BUILD の同梱を更新する。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	${MXV2_MACOS_LICENSE_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_license.cmake")
set_property(TARGET mxv2 APPEND PROPERTY LINK_DEPENDS "${MXV2_MACOS_LICENSE_OUTPUT}")
add_custom_command(TARGET mxv2 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${MXV2_MACOS_LICENSE_OUTPUT}" "${MXV2_RESOURCE_DIR}/LICENSE"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${SDL2_SRC_ROOT}/LICENSE.txt" "${MXV2_RESOURCE_DIR}/SDL2-LICENSE.txt"
	VERBATIM
)
install(TARGETS mxv2 BUNDLE DESTINATION .)

include(CTest)
if(BUILD_TESTING)
	find_package(Python3 COMPONENTS Interpreter)
	if(Python3_Interpreter_FOUND)
		add_test(NAME macos_bundle_and_audio
			COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_macos.py"
				"$<TARGET_BUNDLE_DIR:mxv2>" "$<TARGET_FILE:mxv2_chunktest>"
		)
		set_tests_properties(macos_bundle_and_audio PROPERTIES TIMEOUT 60)
	endif()
endif()
