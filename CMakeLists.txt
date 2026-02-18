cmake_minimum_required(VERSION 3.16)
project(terse-prompt VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
	add_compile_options(/utf-8)
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

# 外部依存をFetchContentで取得
include(FetchContent)

FetchContent_Declare(
	terse
	GIT_REPOSITORY https://github.com/iwadon/terse.git
	GIT_TAG fbf52a5dbe0253ab9eebb459b409476c6dfb4fb7
)
FetchContent_MakeAvailable(terse)

if(TPROMPT_BUILD_TESTING)
	FetchContent_Declare(
		attest
		GIT_REPOSITORY https://github.com/iwadon/attest.git
		GIT_TAG 29ffd9732399c02ad4759d5c1642e8786f665681
	)
	FetchContent_MakeAvailable(attest)
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
)

target_include_directories(tprompt
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
		${terse_SOURCE_DIR}/c/include
)

target_link_libraries(tprompt
	PRIVATE
		terse
)

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
