cmake_minimum_required(VERSION 3.16)
project(terse-prompt VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

# テストのデフォルト: トップレベルプロジェクトならON、サブプロジェクトならOFF
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
	option(TPROMPT_BUILD_TESTING "Build terse-prompt tests" ON)
	option(TPROMPT_BUILD_EXAMPLES "Build terse-prompt examples" ON)
else()
	option(TPROMPT_BUILD_TESTING "Build terse-prompt tests" OFF)
	option(TPROMPT_BUILD_EXAMPLES "Build terse-prompt examples" OFF)
endif()

# 既存のサブモジュールを追加
add_subdirectory(external/terse)

if(TPROMPT_BUILD_TESTING)
	add_subdirectory(external/attest)
endif()

# tpromptライブラリの作成
add_library(tprompt STATIC
	src/tprompt.c
)

target_include_directories(tprompt
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		$<INSTALL_INTERFACE:include>
		${CMAKE_CURRENT_SOURCE_DIR}/external/terse/c/include
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
