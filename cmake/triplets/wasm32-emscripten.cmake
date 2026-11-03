# Wasm as a first-class OS. Chainload Qt's wasm toolchain (same emsdk as make qt WASM=1)
# via VCPKG_CHAINLOAD_TOOLCHAIN_FILE / QT_WASM_TOOLCHAIN from the configure env.
set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)

if(DEFINED ENV{VCPKG_CHAINLOAD_TOOLCHAIN_FILE} AND NOT "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
elseif(DEFINED ENV{QT_WASM_TOOLCHAIN} AND NOT "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{QT_WASM_TOOLCHAIN}")
elseif(DEFINED ENV{EMSDK} AND NOT "${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
endif()

# Match Qt wasm_multithread (SJLJ / emscripten longjmp), pthread + atomics.
# -fPIC: static archives (libpng, protobuf, …) into the linked tip.
set(VCPKG_C_FLAGS "-pthread -matomics -mbulk-memory -fPIC -sSUPPORT_LONGJMP=emscripten")
set(VCPKG_CXX_FLAGS "-pthread -matomics -mbulk-memory -fPIC -sSUPPORT_LONGJMP=emscripten")
set(VCPKG_LINKER_FLAGS "-pthread -matomics -mbulk-memory")

set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    "-DCMAKE_C_FLAGS=-pthread -matomics -mbulk-memory -fPIC -sSUPPORT_LONGJMP=emscripten"
    "-DCMAKE_CXX_FLAGS=-pthread -matomics -mbulk-memory -fPIC -sSUPPORT_LONGJMP=emscripten"
    "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF"
)
