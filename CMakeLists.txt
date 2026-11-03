cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

# C++ deps: vcpkg manifest (vcpkg.json). Qt: make qt / CMAKE_PREFIX_PATH.

option(TIBIA_BUILD_TESTS "Build unit tests" OFF)

# MSVC /Zi (separate PDB) is uncacheable for ccache/sccache. Force /Z7 before
# project() so CMP0141's MSVC_DEBUG_INFORMATION_FORMAT applies to every target.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
	if(POLICY CMP0141)
		cmake_policy(SET CMP0141 NEW)
	endif()
	set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
		"$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

project(tibia)

if(APPLE)
	set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(MessageColors)
include(LoggingHelper)
include(GNUInstallDirs)

set(_TIBIA_EMSCRIPTEN_COMPILER FALSE)
if(DEFINED CMAKE_CXX_COMPILER)
    string(FIND "${CMAKE_CXX_COMPILER}" "em++" _TIBIA_EMSCRIPTEN_COMPILER_POS)
    if(NOT _TIBIA_EMSCRIPTEN_COMPILER_POS EQUAL -1)
        set(_TIBIA_EMSCRIPTEN_COMPILER TRUE)
    endif()
endif()

if(NOT DEFINED WASM)
    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten"
            OR CMAKE_CXX_COMPILER_ID MATCHES "Emscripten"
            OR _TIBIA_EMSCRIPTEN_COMPILER)
        set(WASM ON CACHE BOOL "Build for WebAssembly" FORCE)
    else()
        set(WASM OFF CACHE BOOL "Build for WebAssembly" FORCE)
    endif()
endif()

if(WASM)
    message(STATUS "WASM: ON")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten"
        OR CMAKE_CXX_COMPILER_ID MATCHES "Emscripten"
        OR _TIBIA_EMSCRIPTEN_COMPILER
        OR WASM)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    # -fPIC: static archives + pthread atomics (CMake PIC flag skips Emscripten).
    add_compile_options(-pthread -matomics -mbulk-memory -fPIC)
    add_link_options(-pthread -matomics -mbulk-memory)
endif()

option(OPTIONS_ENABLE_CCACHE "Enable ccache" OFF)
option(OPTIONS_ENABLE_SCCACHE "Use sccache to speed up compilation process" OFF)
option(OPTIONS_ENABLE_IPO "Check and Enable interprocedural optimization (IPO/LTO)" ON)

# Compiler launcher before add_subdirectory so vcpkg-built objects are not required;
# our sources get the launcher via CMAKE_*_COMPILER_LAUNCHER.
set(_TIBIA_COMPILER_LAUNCHER "")
if(OPTIONS_ENABLE_SCCACHE)
	find_program(SCCACHE_PATH sccache)
	if(SCCACHE_PATH)
		set(_TIBIA_COMPILER_LAUNCHER "${SCCACHE_PATH}")
		log_option_enabled("sccache")
	else()
		log_option_disabled("sccache")
	endif()
endif()
if(NOT _TIBIA_COMPILER_LAUNCHER AND OPTIONS_ENABLE_CCACHE)
	find_program(CCACHE ccache)
	if(CCACHE)
		set(_TIBIA_COMPILER_LAUNCHER "${CCACHE}")
		log_option_enabled("ccache")
	else()
		log_option_disabled("ccache")
	endif()
endif()
if(NOT _TIBIA_COMPILER_LAUNCHER AND OPTIONS_ENABLE_SCCACHE)
	find_program(CCACHE ccache)
	if(CCACHE)
		message(STATUS "sccache not found; falling back to ccache (${CCACHE})")
		set(_TIBIA_COMPILER_LAUNCHER "${CCACHE}")
	endif()
endif()
if(_TIBIA_COMPILER_LAUNCHER)
	set(CMAKE_C_COMPILER_LAUNCHER "${_TIBIA_COMPILER_LAUNCHER}")
	set(CMAKE_CXX_COMPILER_LAUNCHER "${_TIBIA_COMPILER_LAUNCHER}")
endif()

if(OPTIONS_ENABLE_IPO)
	log_option_enabled("ipo")
	include(CheckIPOSupported)
	check_ipo_supported(RESULT result OUTPUT output)
	if(result)
		set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
	else()
		log_war("IPO is not supported: ${output}")
	endif()
else()
	log_option_disabled("ipo")
endif()

add_subdirectory(client)

# Qt/QML client + CIP launcher. Pass CMAKE_PREFIX_PATH from make qt (WASM=1 / ANDROID=1 / IOS=1).
option(TIBIA_BUILD_APP "Build the Qt/QML tibia app when Qt is available" ON)
if(TIBIA_BUILD_APP)
    add_subdirectory(shared)
    add_subdirectory(client/tibia)
    if(NOT ANDROID AND NOT IOS)
        add_subdirectory(launcher/src)
    endif()
endif()

if(TIBIA_BUILD_TESTS AND NOT ANDROID AND NOT WASM)
    if(NOT "tests" IN_LIST VCPKG_MANIFEST_FEATURES)
        message(FATAL_ERROR "TIBIA_BUILD_TESTS requires vcpkg manifest feature 'tests' (use a *-debug / *-tests preset).")
    endif()
    enable_testing()
    add_subdirectory(tests)
    log_option_enabled("Build tests")
else()
  if(ANDROID OR WASM)
    log_option_disabled("Build tests (disabled for Android/WASM)")
  else()
    log_option_disabled("Build tests")
  endif()
endif()
