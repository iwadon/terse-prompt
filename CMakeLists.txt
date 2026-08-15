cmake_minimum_required(VERSION 3.16)
project(terse-prompt VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
	add_compile_options(/source-charset:utf-8)
else()
	# 暗黙の関数宣言を即座にエラー化し、非標準関数の呼び出しを早期検出する
	add_compile_options(-Werror=implicit-function-declaration)
endif()

include(CTest)

# テストのデフォルト: トップレベルプロジェクトならON、サブプロジェクトならOFF
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
	option(TPROMPT_BUILD_TESTING "Build terse-prompt tests" ON)
	option(TPROMPT_BUILD_EXAMPLES "Build terse-prompt examples" ON)
else()
	option(TPROMPT_BUILD_TESTING "Build terse-prompt tests" OFF)
	option(TPROMPT_BUILD_EXAMPLES "Build terse-prompt examples" OFF)
endif()

# テストモード: terseのモック機能を有効化
option(TPROMPT_USE_TERSE_TEST_MODE "Enable terse test mode for TTY-independent testing" ${TPROMPT_BUILD_TESTING})

# terseのテストモードを有効化
if(TPROMPT_USE_TERSE_TEST_MODE)
	set(TERSE_ENABLE_TEST_MODE ON CACHE BOOL "Enable terse test mode" FORCE)
endif()

option(TPROMPT_USE_SYSTEM_TERSE "Use system-installed terse instead of fetching" OFF)
if(TPROMPT_USE_SYSTEM_TERSE)
	find_package(terse REQUIRED)
else()
	include(FetchContent)
	FetchContent_Declare(
		terse
		GIT_REPOSITORY https://github.com/iwadon/terse.git
		GIT_TAG 2843738eae1c5288a2acd74f1a198c48e509bf7b
	)
	FetchContent_MakeAvailable(terse)
endif()

if(TPROMPT_BUILD_TESTING)
	option(TPROMPT_USE_SYSTEM_ATTEST "Use system-installed attest instead of fetching" OFF)
	if(TPROMPT_USE_SYSTEM_ATTEST)
		find_package(attest REQUIRED)
	else()
		include(FetchContent)
		FetchContent_Declare(
			attest
			GIT_REPOSITORY https://github.com/iwadon/attest.git
			GIT_TAG b083754b78e87e725521078ba064c6f913b3b2d4
		)
		FetchContent_MakeAvailable(attest)
	endif()
endif()

# tpromptライブラリの作成
add_library(tprompt STATIC
	src/tprompt_action.c
	src/tprompt_api.c
	src/tprompt_buffer.c
	src/tprompt_completion.c
	src/tprompt_display.c
	src/tprompt_error.c
	src/tprompt_history.c
	src/tprompt_init.c
	src/tprompt_keybinding.c
	src/tprompt_match.c
	src/tprompt_prompt.c
	src/tprompt_session.c
	src/tprompt_status.c
	src/tprompt_utf8.c
	src/tprompt_util.c
)

target_include_directories(tprompt
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
)

if(TPROMPT_USE_SYSTEM_TERSE)
	target_link_libraries(tprompt PUBLIC terse::terse)
else()
	target_link_libraries(tprompt PUBLIC terse)
endif()

# インストールルール
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS tprompt
	EXPORT terse-promptTargets
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(DIRECTORY include/
	DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
	FILES_MATCHING PATTERN "*.h"
)

# CMakeパッケージ設定（システムterseを使用している場合のみ）
# FetchContent経由のterseはエクスポートセットに含められない
if(TPROMPT_USE_SYSTEM_TERSE)
	set(TPROMPT_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/terse-prompt")

	write_basic_package_version_file(
		"${CMAKE_CURRENT_BINARY_DIR}/terse-promptConfigVersion.cmake"
		VERSION ${PROJECT_VERSION}
		COMPATIBILITY SameMajorVersion
	)

	configure_package_config_file(
		"${CMAKE_CURRENT_SOURCE_DIR}/cmake/terse-promptConfig.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/terse-promptConfig.cmake"
		INSTALL_DESTINATION "${TPROMPT_INSTALL_CMAKEDIR}"
	)

	install(EXPORT terse-promptTargets
		FILE terse-promptTargets.cmake
		NAMESPACE terse-prompt::
		DESTINATION "${TPROMPT_INSTALL_CMAKEDIR}"
	)

	install(FILES
		"${CMAKE_CURRENT_BINARY_DIR}/terse-promptConfig.cmake"
		"${CMAKE_CURRENT_BINARY_DIR}/terse-promptConfigVersion.cmake"
		DESTINATION "${TPROMPT_INSTALL_CMAKEDIR}"
	)
endif()

# テストの追加
if(TPROMPT_BUILD_TESTING)
	add_subdirectory(tests)
endif()

# Examples の追加
if(TPROMPT_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

# Development tools (always built when building as top-level project)
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
	add_subdirectory(tools)
endif()
