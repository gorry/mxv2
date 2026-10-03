# macOS 配布物の LICENSE。各プロジェクトの原文を保持してまとめる。
get_filename_component(_mxv2_license_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED SDL2_SRC_ROOT)
	set(SDL2_SRC_ROOT "${_mxv2_license_root}/third_party/SDL2-2.32.10-src")
endif()
if(NOT DEFINED IMGUI_DIR)
	set(IMGUI_DIR "${_mxv2_license_root}/third_party/imgui")
endif()
if(NOT DEFINED PORTABLE_MDX_DIR)
	set(PORTABLE_MDX_DIR "${_mxv2_license_root}/third_party/portable_mdx")
endif()

set(MXV2_MACOS_LICENSE_SOURCES
	"${_mxv2_license_root}/LICENSE"
	"${_mxv2_license_root}/NOTICE"
	"${SDL2_SRC_ROOT}/LICENSE.txt"
	"${SDL2_SRC_ROOT}/src/video/yuv2rgb/LICENSE"
	"${SDL2_SRC_ROOT}/src/hidapi/LICENSE-orig.txt"
	"${SDL2_SRC_ROOT}/src/libm/e_atan2.c"
	"${IMGUI_DIR}/LICENSE.txt"
	"${PORTABLE_MDX_DIR}/readme.md"
	"${_mxv2_license_root}/assets/MPLUS1p-OFL.txt"
)
set(_mxv2_license_titles
	"mxv2 / Apache License 2.0"
	"mxv2 / NOTICE and third-party attribution"
	"SDL2 / zlib license"
	"SDL2 / yuv2rgb / BSD 3-Clause license"
	"SDL2 / HIDAPI / original HIDAPI license (selected)"
	"SDL2 / fdlibm / Sun Microsystems permission notice"
	"Dear ImGui / MIT license"
	"portable_mdx / upstream attribution and license terms"
	"M PLUS 1p / SIL Open Font License 1.1"
)
set(_mxv2_license_text "mxv2 macOS distribution licenses\n\nEach component retains its own license. The texts below do not relicense third-party components.\n")
list(LENGTH MXV2_MACOS_LICENSE_SOURCES _mxv2_license_count)
math(EXPR _mxv2_license_last "${_mxv2_license_count} - 1")
foreach(_index RANGE ${_mxv2_license_last})
	list(GET MXV2_MACOS_LICENSE_SOURCES ${_index} _source)
	list(GET _mxv2_license_titles ${_index} _title)
	if(NOT EXISTS "${_source}")
		message(FATAL_ERROR "Missing distribution license: ${_source}")
	endif()
	file(READ "${_source}" _text)
	if(_text STREQUAL "")
		message(FATAL_ERROR "Empty distribution license: ${_source}")
	endif()
	if(_source STREQUAL "${SDL2_SRC_ROOT}/src/libm/e_atan2.c")
		# fdlibm の許諾はソース先頭のコメントにある。実装コードは同梱しない。
		string(FIND "${_text}" "*/" _end)
		if(NOT _text MATCHES "^/\\*" OR _end LESS 0)
			message(FATAL_ERROR "Cannot read fdlibm permission notice")
		endif()
		math(EXPR _length "${_end} + 2")
		string(SUBSTRING "${_text}" 0 ${_length} _text)
	endif()
	string(APPEND _mxv2_license_text "\n================================================================================\n${_title}\n================================================================================\n\n${_text}\n")
endforeach()

if(NOT DEFINED MXV2_MACOS_LICENSE_OUTPUT)
	message(FATAL_ERROR "MXV2_MACOS_LICENSE_OUTPUT is required")
endif()
if(MXV2_MACOS_LICENSE_VERIFY_ONLY)
	file(READ "${MXV2_MACOS_LICENSE_OUTPUT}" _actual)
	if(NOT _actual STREQUAL _mxv2_license_text)
		message(FATAL_ERROR "Bundled LICENSE does not match the distribution license texts")
	endif()
	message(STATUS "Verified bundled distribution LICENSE")
else()
	# 全文を読み終えてから書く。不足時に不完全な LICENSE を生成しない。
	if(EXISTS "${MXV2_MACOS_LICENSE_OUTPUT}")
		file(READ "${MXV2_MACOS_LICENSE_OUTPUT}" _actual)
	endif()
	if(NOT _actual STREQUAL _mxv2_license_text)
		file(WRITE "${MXV2_MACOS_LICENSE_OUTPUT}" "${_mxv2_license_text}")
	endif()
endif()
