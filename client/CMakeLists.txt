project(tibia)

# *****************************************************************************
# Options
# *****************************************************************************

option(TOGGLE_FRAMEWORK_GRAPHICS "Use Graphics " ON)
option(TOGGLE_FRAMEWORK_XML "Use XML " ON)
option(TOGGLE_FRAMEWORK_NET "Use NET " ON)
option(TOGGLE_FRAMEWORK_PROTOBUF "Use Protobuf" ON)
option(TOGGLE_FRAMEWORK_EDITOR "Use Editor " OFF)
option(TOGGLE_BIN_FOLDER "Use build/bin folder for generate compilation files" OFF)
option(DEBUG_LOG "Enable Debug Log" OFF)
option(ASAN_ENABLED "Build this target with AddressSanitizer" OFF)
option(BUILD_STATIC_LIBRARY "Build using static libraries" OFF)
option(TOGGLE_PRE_COMPILED_HEADER "Use precompiled header (speed up compile)" ON)
option(SPEED_UP_BUILD_UNITY "Compile using build unity for speed up build" ON)

# cip_messages is added after TibiaPackages finds protobuf.

# *****************************************************************************
# Cmake Features
# *****************************************************************************
set(GNUCXX_MINIMUM_VERSION 9)
if(MSVC)
  set(CMAKE_CXX_STANDARD 20)
else()
  set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


# Make will print more details
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Generate compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# *****************************************************************************
# Client host: tibia executable (Lua client path retired)
# *****************************************************************************
# ${PROJECT_NAME} is not created. Engine sources compile into tibia.


# *****************************************************************************
# Options Code
# *****************************************************************************

# Define Framework options for use in compilation
set(FRAMEWORK_DEFINITIONS "")
if (TOGGLE_FRAMEWORK_GRAPHICS)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DFRAMEWORK_GRAPHICS)
endif()
if (TOGGLE_FRAMEWORK_XML)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DFRAMEWORK_XML)
endif()
if (TOGGLE_FRAMEWORK_NET)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DFRAMEWORK_NET)
endif()
if (TOGGLE_FRAMEWORK_PROTOBUF)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DFRAMEWORK_PROTOBUF)
endif()

if (TOGGLE_FRAMEWORK_EDITOR)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DFRAMEWORK_EDITOR)
endif()
if (ANDROID)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DOPENGL_ES=3)
elseif(WASM)
  set(FRAMEWORK_DEFINITIONS ${FRAMEWORK_DEFINITIONS} -DOPENGL_ES=2)
endif()

set(VERSION "1.0.0")

# *****************************************************************************
# Definitions code
# *****************************************************************************

# *****************************************************************************
# Sanity Check
# *****************************************************************************

# === GCC Minimum Version ===
if (CMAKE_COMPILER_IS_GNUCXX)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS GNUCXX_MINIMUM_VERSION)
    message(FATAL_ERROR "GCC version must be at least ${GNUCXX_MINIMUM_VERSION}!")
  endif()
endif()


# Multiprocessor compiller
if(CMake_MSVC_PARALLEL)
  if(CMake_MSVC_PARALLEL GREATER 0)
    add_definitions(/MP${CMake_MSVC_PARALLEL})
  else()
    add_definitions(/MP)
  endif()
endif()


# Client options
add_definitions(-DCLIENT)
add_definitions(-DNOMINMAX)
add_definitions(${FRAMEWORK_DEFINITIONS})
add_definitions(-D"VERSION=${VERSION}")
# === Build options ===
message(STATUS "Build type: " ${CMAKE_BUILD_TYPE})
add_definitions(-D"BUILD_TYPE=\\\"${CMAKE_BUILD_TYPE}\\\"")
message(STATUS "Build commit: ${BUILD_COMMIT}")
add_definitions(-D"BUILD_COMMIT=\\\"${BUILD_COMMIT}\\\"")
message(STATUS "Build revision: ${BUILD_REVISION}")
add_definitions(-D"BUILD_REVISION=\\\"${BUILD_REVISION}\\\"")


# *****************************************************************************
# Packages / Libs (vcpkg)
# *****************************************************************************
include("${CMAKE_SOURCE_DIR}/cmake/TibiaPackages.cmake")
if(TOGGLE_FRAMEWORK_PROTOBUF)
  add_subdirectory(tibia/protobuf)
endif()

if(APPLE)
  find_library(FOUNDATION Foundation REQUIRED)
  find_library(IOKIT IOKit REQUIRED)
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT WASM)
  find_package(X11 REQUIRED)
endif()
if(APPLE AND NOT ANDROID AND NOT WASM)
  find_library(COCOA_LIBRARY Cocoa REQUIRED)
  find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
  find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
  find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
endif()
if(WIN32)
  find_package(DbgHelp REQUIRED)
endif(WIN32)
if(ANDROID)
  include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine/framework/core/minizip)
  set(MINIZIP_LIBRARY "")
  find_package(game-activity REQUIRED CONFIG)
  find_package(EGL REQUIRED)
endif()


# *****************************************************************************
# Engine / host source configuration
# *****************************************************************************
set(SOURCE_FILES
        tibia/engine/framework/core/application.cpp
        tibia/engine/framework/core/asyncdispatcher.cpp
        tibia/engine/framework/core/binarytree.cpp
        tibia/engine/framework/core/clock.cpp
        tibia/engine/framework/core/config.cpp
        tibia/engine/framework/core/configmanager.cpp
        tibia/engine/framework/core/event.cpp
        tibia/engine/framework/core/eventdispatcher.cpp
        tibia/engine/framework/core/gameevents.cpp
        tibia/engine/framework/core/filestream.cpp
        tibia/engine/framework/core/logger.cpp
        tibia/engine/framework/core/resourcemanager.cpp
        tibia/engine/framework/core/scheduledevent.cpp
        tibia/engine/framework/core/unzipper.cpp
        tibia/engine/framework/core/unzipper.h
        tibia/engine/framework/core/timer.cpp
        tibia/engine/framework/discord/discord.cpp
        tibia/engine/framework/net/gamelink.cpp
        tibia/engine/framework/net/inputmessage.cpp
        tibia/engine/framework/net/outputmessage.cpp
        tibia/engine/framework/net/protocol.cpp
        tibia/engine/framework/net/protocolhttp.cpp
        tibia/engine/framework/platform/androidgameactivity.cpp
        tibia/engine/framework/platform/androidmanager.cpp
        tibia/engine/framework/platform/androidmanager.h
        tibia/engine/framework/platform/androidwindow.cpp
        tibia/engine/framework/platform/androidwindow.h
        tibia/engine/framework/platform/platform.cpp
        tibia/engine/framework/platform/platformwindow.cpp
        tibia/engine/framework/platform/unixcrashhandler.cpp
        tibia/engine/framework/platform/unixplatform.cpp
        tibia/engine/framework/platform/win32crashhandler.cpp
        tibia/engine/framework/platform/win32platform.cpp
        tibia/engine/framework/stdext/demangle.cpp
        tibia/engine/framework/stdext/math.cpp
        tibia/engine/framework/stdext/net.cpp
        tibia/engine/framework/stdext/string.cpp
        tibia/engine/framework/stdext/time.cpp
        tibia/engine/framework/stdext/uri.cpp
        tibia/engine/framework/stdext/qrcodegen.cpp
        tibia/engine/framework/util/color.cpp
        tibia/engine/framework/util/crypt.cpp
        tibia/engine/framework/util/stats.cpp
        tibia/engine/framework/proxy/proxy.cpp
        tibia/engine/framework/proxy/proxy_client.cpp

        tibia/engine/client/animatedtext.cpp
        tibia/engine/client/animator.cpp
        tibia/engine/client/attachableobject.cpp
        tibia/engine/client/client.cpp
        tibia/engine/client/creatures.cpp
        tibia/engine/client/gameconfig.cpp
        tibia/engine/client/heightstamp.cpp
        tibia/engine/client/houses.cpp
        tibia/engine/client/item.cpp
        tibia/engine/client/itemtype.cpp
        tibia/engine/client/lightfader.cpp
        tibia/engine/client/mapio.cpp
        tibia/engine/client/minimap.cpp
        tibia/engine/client/cyclopediamap.cpp
        tibia/engine/client/outfit.cpp
        tibia/engine/client/player.cpp
        tibia/engine/client/position.cpp
        tibia/engine/client/protocolgame.cpp
        tibia/protocol/messagemodes.cpp
        tibia/protocol/extendedopcode.cpp
        tibia/protocol/servermessages.cpp
        tibia/protocol/clientmessages.cpp
        tibia/protocol/dispatch.gen.cpp
        tibia/engine/client/statictext.cpp
        tibia/assets/spritesheet.cpp
        tibia/assets/thingtype.cpp
        tibia/assets/things.cpp
        tibia/world/container.cpp
        tibia/world/creature.cpp
        tibia/world/effect.cpp
        tibia/world/game.cpp
        tibia/world/localplayer.cpp
        tibia/world/map.cpp
        tibia/world/missile.cpp
        tibia/world/thing.cpp
        tibia/world/tile.cpp
        tibia/world/attachedeffect.cpp
        tibia/world/attachedeffectmanager.cpp
        tibia/world/paperdoll.cpp
        tibia/world/paperdollmanager.cpp
        tibia/world/sessionhooks.cpp
        tibia/engine/client/towns.cpp
        tibia/engine/client/satellitemap.cpp
)

if (TOGGLE_FRAMEWORK_GRAPHICS)
  set(SOURCE_FILES ${SOURCE_FILES}
          tibia/engine/framework/core/adaptativeframecounter.cpp
          tibia/engine/framework/core/graphicalapplication.cpp
          tibia/engine/framework/input/mouse.cpp
          tibia/engine/framework/graphics/animatedtexture.cpp
          tibia/engine/framework/graphics/apngloader.cpp
          tibia/engine/framework/graphics/coordsbuffer.cpp
          tibia/engine/framework/graphics/textureatlas.cpp
          tibia/engine/framework/graphics/render/resourceregistry.cpp
          tibia/engine/framework/graphics/graphics.cpp
          tibia/engine/framework/graphics/image.cpp
          tibia/engine/framework/graphics/texture.cpp
          tibia/engine/framework/graphics/texturemanager.cpp
  )
  if(WIN32)
    set(SOURCE_FILES ${SOURCE_FILES}
            tibia/engine/framework/graphics/gl_win32.cpp)
  endif()
endif()

if (WASM)
  set(SOURCE_FILES ${SOURCE_FILES}
          tibia/engine/framework/platform/browserplatform.cpp
          tibia/engine/framework/platform/browserwindow.cpp
  )
endif()

if(NOT WASM)
  # Vendored minizip sources for ZIP extraction fallback (must be after SOURCE_FILES definition)
  set(SOURCE_FILES ${SOURCE_FILES}
          tibia/engine/framework/core/minizip/ioapi.c
          tibia/engine/framework/core/minizip/ioapi_mem.c
          tibia/engine/framework/core/minizip/unzip.c
          tibia/engine/framework/core/minizip/crypt.c
  )
endif()

if(WIN32)
  set(SOURCE_FILES ${SOURCE_FILES} tibia/engine/framework/platform/win32window.cpp)
elseif(APPLE AND NOT ANDROID AND NOT WASM)
  set(SOURCE_FILES ${SOURCE_FILES}
          tibia/engine/framework/platform/cocoawindow.mm
          tibia/engine/framework/platform/cocoaview.mm
  )
  set_source_files_properties(
    tibia/engine/framework/platform/cocoawindow.mm
    tibia/engine/framework/platform/cocoaview.mm
    PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
    SKIP_PRECOMPILE_HEADERS ON
  )
elseif(UNIX AND NOT ANDROID AND NOT WASM)
  set(SOURCE_FILES ${SOURCE_FILES} tibia/engine/framework/platform/x11window.cpp)
endif()

# Engine for tibia (and wasm/mobile hosts): no Lua, OTUI, HTML, modules, or
# native window (the host defines g_window). Script hooks reach GameEvents only.
set(GAMECORE_SOURCES ${SOURCE_FILES})
list(FILTER GAMECORE_SOURCES EXCLUDE REGEX
  "^(tibia/engine/framework/luaengine/(luaexception|luainterface|luaobject|luavaluecasts)\.|tibia/engine/framework/luafunctions\.cpp|tibia/engine/client/luafunctions\.cpp|tibia/engine/client/luavaluecasts_client\.cpp|tibia/engine/framework/html/|tibia/engine/client/ui[a-z]*\.cpp|tibia/engine/framework/core/module(manager)?\.cpp|tibia/engine/framework/core/garbagecollection\.cpp|tools/datdump\.cpp\
|tibia/engine/framework/platform/(cocoawindow|cocoaview|x11window|win32window|browserwindow|androidwindow|androidgameactivity)\.)")
set_source_files_properties(
  tibia/assets/things.cpp
  tibia/assets/spritesheet.cpp
  tibia/protocol/servermessages.cpp
  tibia/protocol/clientmessages.cpp
  tibia/protocol/dispatch.gen.cpp
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)
set(_protocol_stamp "${CMAKE_CURRENT_BINARY_DIR}/protocol_dispatch.stamp")
add_custom_command(
  OUTPUT "${_protocol_stamp}"
  COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/gen_dispatch.py"
          --check
          --messages "${CMAKE_SOURCE_DIR}/client/upstream/protocol/messages.json"
          --codes "${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine/client/protocolcodes.h"
          --server "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/servermessages.cpp"
          --client "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/clientmessages.cpp"
          --map "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/handler_map.json"
          --out "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/dispatch.gen.cpp"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_protocol_stamp}"
  DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/gen_dispatch.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/handler_map.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/dispatch.gen.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/servermessages.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tibia/protocol/clientmessages.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine/client/protocolcodes.h"
    "${CMAKE_SOURCE_DIR}/client/upstream/protocol/messages.json"
  VERBATIM
)
add_custom_target(protocol_dispatch_check DEPENDS "${_protocol_stamp}")
list(FILTER GAMECORE_SOURCES EXCLUDE REGEX "/platform/|/proxy/|/discord/|/(application|graphicalapplication|consoleapplication|eventdispatcher|mouse)\\.cpp$")
set(_tibia_engine_abs)
foreach(_src IN LISTS GAMECORE_SOURCES)
  list(APPEND _tibia_engine_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
endforeach()
set(TIBIA_ENGINE_SOURCES "${_tibia_engine_abs}" PARENT_SCOPE)
add_library(qtclient_deps INTERFACE)
target_link_libraries(qtclient_deps INTERFACE tibia_thirdparty)
add_dependencies(qtclient_deps protocol_dispatch_check)
target_compile_definitions(qtclient_deps INTERFACE QTCLIENT_LUA=0 QTCLIENT_NATIVE_WINDOW=0 ${FRAMEWORK_DEFINITIONS})
target_compile_features(qtclient_deps INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

if(NOT MSVC AND CMAKE_INTERPROCEDURAL_OPTIMIZATION MATCHES "^(ON|1|TRUE)$")
  target_link_options(qtclient_deps INTERFACE -flto=auto)
endif()

# Engine sources compile into tibia. A framework PCH would also apply to Qt
# sources (QT_NO_KEYWORDS), so headers include what they need themselves.
message(STATUS "Use precompiled header: OFF")

# Exclude vendored minizip C files from PCH and unity build (C files cannot use C++ PCH)
if(NOT WASM)
  set_source_files_properties(
    tibia/engine/framework/core/minizip/ioapi.c
    tibia/engine/framework/core/minizip/ioapi_mem.c
    tibia/engine/framework/core/minizip/unzip.c
    tibia/engine/framework/core/minizip/crypt.c
    PROPERTIES
      SKIP_PRECOMPILE_HEADERS ON
      SKIP_UNITY_BUILD_INCLUSION ON
  )
endif()

if(SPEED_UP_BUILD_UNITY)
  log_option_enabled("Build unity for speed up compilation")
endif(SPEED_UP_BUILD_UNITY)

# Legacy Lua main.cpp / executable path retired.

# *****************************************************************************
# Build flags
# *****************************************************************************
if (NOT MSVC)
  if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(qtclient_deps INTERFACE -Wno-deprecated-declarations)
  endif()
endif()

if(THREADS_HAVE_PTHREAD_ARG)
  target_compile_options(qtclient_deps INTERFACE "-pthread")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_definitions(-DNDEBUG)
endif()

# *****************************************************************************
# Definitions code
# *****************************************************************************

# === ASAN ===
if(ASAN_ENABLED)
  log_option_enabled("asan")

  if(MSVC)
    string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(qtclient_deps INTERFACE /fsanitize=address)
if(TARGET ${PROJECT_NAME})
    target_link_options(${PROJECT_NAME} PUBLIC /fsanitize=address)
endif()
  else()
    target_compile_options(qtclient_deps INTERFACE -fsanitize=address)
if(TARGET ${PROJECT_NAME})
    target_link_options(${PROJECT_NAME} PUBLIC -fsanitize=address)
endif()
  endif()
else()
  log_option_disabled("asan")
endif()

# === DEBUG LOG ===
# cmake -DDEBUG_LOG=ON ..
if(CMAKE_BUILD_TYPE MATCHES Debug)
  target_compile_definitions(qtclient_deps INTERFACE -DDEBUG_LOG=ON )
  log_option_enabled("DEBUG LOG")
else()
  log_option_disabled("DEBUG LOG")
endif(CMAKE_BUILD_TYPE MATCHES Debug)

target_compile_definitions(qtclient_deps INTERFACE ENABLE_DISCORD_RPC=0)

# *****************************************************************************
# Includes and librarys
# *****************************************************************************
if(MSVC)

  # cpp-httplib 0.43+ requires Windows 10 or later.
  # Full windows.h (no WIN32_LEAN_AND_MEAN). rpcndr `small` is undef'd in
  # framework/win32_platform.h after the SDK headers.
  target_compile_definitions(qtclient_deps
          INTERFACE
          NTDDI_VERSION=0x0A000000
          WINVER=0x0A00
          _WIN32_WINNT=0x0A00
          NOMINMAX
  )

  target_compile_definitions(qtclient_deps INTERFACE CRASH_HANDLER)

  # /FS /Zf are PDB (/Zi) helpers; with /Z7 they are unnecessary and confuse
  # cacheability checks. /MP is fine under Ninja (one TU per cl invocation).
  target_compile_options(qtclient_deps INTERFACE /MP /EHsc /bigobj)

  # MSVC 19.44 ICEs in stdext::safe_cast instantiations with release optimization.
  set_source_files_properties(
          PROPERTIES
            COMPILE_OPTIONS "/Od;/Ob0"
            SKIP_PRECOMPILE_HEADERS ON
  )

  # Belt-and-suspenders for older CMake / CMP0141 OLD: rewrite flag strings and
  # force Embedded so deps added later do not reintroduce /Zi.
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded" CACHE STRING
      "MSVC debug info (/Z7) — required for ccache/sccache" FORCE)
  foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    foreach(_lang C CXX)
      set(_var "CMAKE_${_lang}_FLAGS_${_cfg}")
      if(DEFINED ${_var})
        string(REPLACE "/Zi" "/Z7" ${_var} "${${_var}}")
        string(REPLACE "/ZI" "/Z7" ${_var} "${${_var}}")
        set(${_var} "${${_var}}" CACHE STRING "" FORCE)
      endif()
    endforeach()
  endforeach()

  # /MD to match Qt; vcpkg triplet is x64-windows-static-md (static libs).
  log_option_disabled("STATIC_LIBRARY (MSVC CRT is /MD)")
  foreach(msvc_target IN ITEMS qtclient_deps)
    if(TARGET ${msvc_target})
      set_property(TARGET ${msvc_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
  endforeach()

  target_include_directories(qtclient_deps
          INTERFACE
          $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(qtclient_deps
          INTERFACE
          tibia_thirdparty
          ${DBGHELP_LIBRARY}
          Threads::Threads
          winmm.lib
          bcrypt.lib
  )

  if (TOGGLE_FRAMEWORK_PROTOBUF)
      target_link_libraries(qtclient_deps
          INTERFACE
          protobuf::libprotobuf
          protobuf
      )
  endif()
elseif(ANDROID)
  target_include_directories(qtclient_deps
          INTERFACE
          $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
          ${EGL_INCLUDE_DIR}
  )
  target_link_libraries(qtclient_deps
          INTERFACE
          tibia_thirdparty
          ${EGL_LIBRARY}
          ${MINIZIP_LIBRARY}
          Threads::Threads
          OpenSLES
          game-activity::game-activity
          GLESv3
          android
          log
  )

  if (TOGGLE_FRAMEWORK_PROTOBUF)
      target_link_libraries(qtclient_deps
          INTERFACE
          protobuf::libprotobuf protobuf
      )
  endif()

elseif(WASM)
  option(WASM_USE_WASM_EXCEPTIONS "Use native WebAssembly exception handling" ON)
  # Absolute path; monorepo build.sh may override with data/ottibia/engine/client/browser/shell.html.
  set(WASM_SHELL_FILE "${CMAKE_SOURCE_DIR}/browser/shell.html" CACHE FILEPATH
      "Emscripten --shell-file for the WASM HTML wrapper")
  # Fixed SAB heap (ALLOW_MEMORY_GROWTH=0 — Chrome WebGL rejects resizable SAB).
  set(WASM_INITIAL_MEMORY "1073741824" CACHE STRING
      "Emscripten -sINITIAL_MEMORY= bytes (fixed WASM heap)")

  set(_WASM_LONGJMP_MODE "emscripten")
  # Shell + IdleOT SIDE_MODULE keepalives — see cmake/host_exports.json
  # (regen: scripts/lifecycle/lib/gen-host-exports.sh).
  set(_WASM_HOST_EXPORTS "${CMAKE_SOURCE_DIR}/cmake/host_exports.json")
  if(NOT EXISTS "${_WASM_HOST_EXPORTS}")
    message(FATAL_ERROR "WASM: missing ${_WASM_HOST_EXPORTS}")
  endif()
  set(_WASM_CXX_FLAGS "-O3 -ffast-math -msimd128 -frtti -pthread -fexceptions")
  if(WASM_USE_WASM_EXCEPTIONS)
    set(_WASM_LONGJMP_MODE "wasm")
    string(APPEND _WASM_CXX_FLAGS " -fwasm-exceptions")
  endif()

  # Match desktop qtclient_deps: PROJECT_SOURCE_DIR (launcher/…), client/,
  # client/tibia, and engine.
  target_include_directories(qtclient_deps
          INTERFACE
          ${PROJECT_SOURCE_DIR}
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_SOURCE_DIR}/client/tibia
          ${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine
  )
  target_link_libraries(qtclient_deps
          INTERFACE
          tibia_thirdparty
          Threads::Threads
  )

  if (TOGGLE_FRAMEWORK_PROTOBUF)
      target_link_libraries(qtclient_deps
          INTERFACE
          protobuf::libprotobuf protobuf
      )
  endif()


  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(qtclient_deps
            INTERFACE
            -Wall -Wextra -Wpedantic
    )
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
  endif()

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_WASM_CXX_FLAGS}")

  set(WASM_LINK_OPTIONS
          -sINVOKE_RUN=0
          -sEXIT_RUNTIME=1
          -sTEXTDECODER=2
          -sCASE_INSENSITIVE_FS=1
          -lidbfs.js
          -lwebsocket.js
          # setCanvasElementSize: browserwindow applyShellLayout (Debug ASSERTIONS
          # abort on typeof Module.setCanvasElementSize when the symbol is missing).
          -sEXPORTED_RUNTIME_METHODS=ccall,setCanvasElementSize,FS,IDBFS
          "-sEXPORTED_FUNCTIONS=@${_WASM_HOST_EXPORTS}"
          # Must list every key shell.html assigns on Module (Emscripten abort if extra).
          -sINCOMING_MODULE_JS_API=[locateFile,preRun,postRun,print,printErr,canvas,setStatus,monitorRunDependencies,onAbort]
          -sFORCE_FILESYSTEM
          -sMALLOC=dlmalloc
          "-sSUPPORT_LONGJMP=${_WASM_LONGJMP_MODE}"
          # Do not pass -sWEBSOCKET_SUBPROTOCOL (empty value is invalid).
          # The game WebSocket omits the subprotocol so Canary handshakes work.
          -sOFFSCREENCANVAS_SUPPORT=1
          -sOFFSCREEN_FRAMEBUFFER
          -sENVIRONMENT=web,worker
          -sPROXY_TO_PTHREAD
          -sFULL_ES2=1
          -sMIN_WEBGL_VERSION=2
          -sMAX_WEBGL_VERSION=2
          -sUSE_PTHREADS=1
          -sFETCH=1
          -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency
          # Growable WASM memory becomes a resizable SharedArrayBuffer, which
          # Chrome 149+ refuses to accept as a source for WebGL uploads
          # ("ArrayBuffer value must not be resizable"). Use a fixed heap.
          -sALLOW_MEMORY_GROWTH=0
          -sINITIAL_MEMORY=${WASM_INITIAL_MEMORY}
          # No --preload-file: the page syncs the runtime tree (content-web, see
          # deploy/build-browser.sh) into MEMFS in Module.preRun via @qtclient/client-sync.
          --shell-file=${WASM_SHELL_FILE}
  )

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND WASM_LINK_OPTIONS -sASSERTIONS=1)
  endif()

if(TARGET ${PROJECT_NAME})
  target_link_options(${PROJECT_NAME} PRIVATE ${WASM_LINK_OPTIONS})
endif()
  string(REPLACE ";" " " _WASM_LINK_LOG "${WASM_LINK_OPTIONS}")
  message(STATUS "WASM link options: ${_WASM_LINK_LOG}")
  set(CMAKE_EXECUTABLE_SUFFIX ".html")

else() # Linux / macOS
  target_include_directories(qtclient_deps
          INTERFACE
          $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tibia/engine>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(qtclient_deps
          INTERFACE
          tibia_thirdparty
          Threads::Threads
          $<$<AND:$<PLATFORM_ID:Linux>,$<NOT:$<BOOL:${ANDROID}>>>:X11::X11>
  )

  if (TOGGLE_FRAMEWORK_PROTOBUF)
      target_link_libraries(qtclient_deps
          INTERFACE
          protobuf::libprotobuf protobuf
      )
  endif()

  if(APPLE AND NOT ANDROID AND NOT WASM)
    target_link_libraries(qtclient_deps
            INTERFACE
            ${COCOA_LIBRARY}
            ${COREVIDEO_LIBRARY}
            ${QUARTZCORE_LIBRARY}
            ${OPENGL_FRAMEWORK}
    )
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(qtclient_deps
            INTERFACE
            -Wall -Wextra -Wpedantic
    )
  endif()

endif()

# macOS bundle identity lives on the `tibia` target (client/tibia/CMakeLists.txt).
# Product packs under client/mods/*/ override MACOSX_BUNDLE_* there.

if(ENABLE_DISCORD_RPC AND NOT ANDROID)
        target_link_libraries(qtclient_deps INTERFACE ${DISCORDRPC_LIBRARY})
endif()

# *****************************************************************************
# Enable debug console only for debug build
# *****************************************************************************
if(WIN32)
if(TARGET ${PROJECT_NAME})
  set_target_properties(${PROJECT_NAME}
          PROPERTIES
          LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
          LINK_FLAGS_RELEASE "/SUBSYSTEM:windows /ENTRY:mainCRTStartup"
          LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:windows /ENTRY:mainCRTStartup"
          LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:windows /ENTRY:mainCRTStartup"
  )
endif()
endif(WIN32)

if(ANDROID)
  set(CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=lld")
  set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")
endif()

# *****************************************************************************
# Link compilation files folder
# *****************************************************************************

## Link compilation files to build/bin folder, else link to the main dir
if (TOGGLE_BIN_FOLDER)
if(TARGET ${PROJECT_NAME})
  set_target_properties(${PROJECT_NAME}
          PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  )
endif()
else()
if(TARGET ${PROJECT_NAME})
  set_target_properties(${PROJECT_NAME}
          PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/"
  )
endif()
endif()

