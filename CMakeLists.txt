cmake_minimum_required(VERSION 3.6)
project(krkr2yuri)

set(CMAKE_CXX_STANDARD 11)
set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins)
set(COCOS2DX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/port/cocos2d-x)

option(KRKR2_BUILD_TESTS "Build host unit tests (no engine/cocos/android)" OFF)

if(CMAKE_SYSTEM_NAME MATCHES "Android")
	# --- Android: full engine + plugins + JNI shared lib ---
	add_subdirectory(${KRKR2CORE_PATH})
	add_subdirectory(${KRKR2PLUGIN_PATH})

	add_library(${PROJECT_NAME} SHARED
		project/android/app/cpp/krkr2_android.cpp
	)
	target_compile_options(${PROJECT_NAME} PUBLIC
		-fPIE
	)
	target_include_directories(${PROJECT_NAME} PUBLIC
		${KRKR2CORE_PATH}/environ/cocos2d
		${PORTBUILD_PATH}/include
		${PORTBUILD_PATH}/include/breakpad
		${COCOS2DX_PATH}/cocos
		${COCOS2DX_PATH}/cocos/audio/include
	)
	target_link_directories(${PROJECT_NAME} PUBLIC
		${PORTBUILD_PATH}/lib
	)
	target_link_libraries(${PROJECT_NAME} PUBLIC
		-Wl,-Bstatic
		-Wl,--whole-archive # cpp_android_spec for jni function, this is important to add whole libraries !
		cpp_android_spec krkr2plugin
		-Wl,--no-whole-archive
		krkr2core
	)
elseif(KRKR2_BUILD_TESTS)
	# --- Host: unit tests only (no engine, no cocos, no android) ---
	enable_testing()
	add_subdirectory(tests)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} not supported yet. "
		"Use -DKRKR2_BUILD_TESTS=ON for the host unit-test suite.")
endif()
