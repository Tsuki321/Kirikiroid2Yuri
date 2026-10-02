# Host-executable unit tests for pure-logic engine components.
# No cocos2d-x, no Android, no JNI — just the .c/.cpp files under test + gtest.
# Invoked from the root CMakeLists when -DKRKR2_BUILD_TESTS=ON on a non-Android host.

include(FetchContent)
FetchContent_Declare(
	googletest
	GIT_REPOSITORY https://github.com/google/googletest.git
	GIT_TAG        release-1.12.1   # last release supporting C++11
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)
find_package(Threads REQUIRED)

option(KRKR2_TEST_SANITIZERS "Run host tests with address/undefined sanitizers" OFF)
if(KRKR2_TEST_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

set(SRC_ROOT ${CMAKE_SOURCE_DIR}/src)

# --- MD5 (RFC 1321 known-answer vectors) -------------------------------------
# src/core/utils/md5.c is a standalone C translation unit (no engine deps).
add_executable(test_md5
	test_md5.cpp
	${SRC_ROOT}/core/utils/md5.c
)
target_include_directories(test_md5 PRIVATE ${SRC_ROOT}/core/utils)
target_link_libraries(test_md5 PRIVATE gtest_main)
gtest_discover_tests(test_md5)

# --- Bitap fuzzy match (header-only template) --------------------------------
# src/plugins/bitap_fuzzy.hpp is a pure template; just include it.
add_executable(test_bitap_fuzzy
	test_bitap_fuzzy.cpp
)
target_include_directories(test_bitap_fuzzy PRIVATE ${SRC_ROOT}/plugins)
target_link_libraries(test_bitap_fuzzy PRIVATE gtest_main)
gtest_discover_tests(test_bitap_fuzzy)

add_executable(test_file_operations test_file_operations.cpp)
target_include_directories(test_file_operations PRIVATE ${SRC_ROOT}/core/base)
target_link_libraries(test_file_operations PRIVATE gtest_main Threads::Threads)
gtest_discover_tests(test_file_operations PROPERTIES TIMEOUT 30)

add_executable(test_bytecode test_bytecode.cpp)
target_include_directories(test_bytecode PRIVATE ${SRC_ROOT}/core/tjs2)
target_link_libraries(test_bytecode PRIVATE gtest_main)
gtest_discover_tests(test_bytecode PROPERTIES TIMEOUT 60)

add_executable(test_xp3_index test_xp3_index.cpp)
target_include_directories(test_xp3_index PRIVATE ${SRC_ROOT}/core/base)
target_link_libraries(test_xp3_index PRIVATE gtest_main)
gtest_discover_tests(test_xp3_index PROPERTIES TIMEOUT 30)

add_executable(test_alpha_blend test_alpha_blend.cpp)
target_include_directories(test_alpha_blend PRIVATE ${SRC_ROOT}/core/tjs2 ${SRC_ROOT}/core/visual)
target_link_libraries(test_alpha_blend PRIVATE gtest_main)
gtest_discover_tests(test_alpha_blend)

# Exercise the actual interpreter with release optimization and sanitizers.
# Android's ARM translation layer cannot always unwind a native crash, so these
# tests provide source-level diagnostics for the same script/native operations.
find_path(ONIG_INCLUDE_DIR oniguruma.h REQUIRED)
find_library(ONIG_LIBRARY onig REQUIRED)
file(GLOB TJS_RUNTIME_SOURCES CONFIGURE_DEPENDS ${SRC_ROOT}/core/tjs2/*.cpp)
add_executable(test_tjs_runtime test_tjs_runtime.cpp ${TJS_RUNTIME_SOURCES})
set_target_properties(test_tjs_runtime PROPERTIES CXX_STANDARD 14)
target_compile_options(test_tjs_runtime PRIVATE -O2)
target_compile_definitions(test_tjs_runtime PRIVATE NDEBUG)
target_include_directories(test_tjs_runtime PRIVATE ${SRC_ROOT}/core/tjs2 ${SRC_ROOT}/core/utils ${ONIG_INCLUDE_DIR})
target_link_libraries(test_tjs_runtime PRIVATE gtest_main Threads::Threads ${ONIG_LIBRARY})
gtest_discover_tests(test_tjs_runtime PROPERTIES TIMEOUT 60)
