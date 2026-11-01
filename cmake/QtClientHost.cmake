# QtClient host ABI for runtime packs (desktop MODULE + WASM SIDE_MODULE).
#
# IdleOT-sized packs import most of the client symbol surface. Rather than a
# second copy of every TU, the fat host is the `tibia` / `client` executable
# with ENABLE_EXPORTS (generates an import library packs link against). On
# Emscripten the binary is MAIN_MODULE=1 and SIDE_MODULE packs resolve
# symbols from it at dlopen time (MAIN_MODULE=2 DCE drops unreferenced ABI).
#
# Consumers:
#   find_package(QtClient REQUIRED)
#   target_link_libraries(my_mod PRIVATE QtClient::Host)
# Or in-tree: include(QtClientHost.cmake) after the tibia target exists.

include_guard(GLOBAL)

function(qtclient_host_finalize)
  if(NOT TARGET tibia)
    message(FATAL_ERROR "qtclient_host_finalize: tibia target missing")
  endif()

  # Executable exports symbols for MODULE / SIDE_MODULE packs.
  set_target_properties(tibia PROPERTIES ENABLE_EXPORTS ON)
  if(APPLE AND NOT EMSCRIPTEN)
    target_link_options(tibia PRIVATE "LINKER:-export_dynamic")
  elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT WASM)
    target_link_options(tibia PRIVATE "LINKER:--export-dynamic")
  elseif(EMSCRIPTEN)
    # Do not add EXPORT_ALL here: with MAIN_MODULE=1 it expands every symbol
    # onto the wasm-ld command line and CI link dies with an empty log
    # (arg-list / OOM). MAIN_MODULE=1 on the tibia link line already retains
    # host/Qt ABI for SIDE_MODULE packs (see client/tibia/CMakeLists.txt).
  endif()

  if(NOT TARGET qtclient_host)
    add_library(qtclient_host INTERFACE)
    target_link_libraries(qtclient_host INTERFACE tibia)
    target_include_directories(qtclient_host INTERFACE
      "$<BUILD_INTERFACE:${TIBIA_ROOT}/client/tibia>"
      "$<INSTALL_INTERFACE:include/qtclient>")
  endif()

  if(NOT TARGET QtClient::Host)
    add_library(QtClient::Host ALIAS qtclient_host)
  endif()

  # Build-tree package for out-of-tree packs (idleot-client).
  set(_qtclient_config_dir "${CMAKE_BINARY_DIR}/cmake")
  file(MAKE_DIRECTORY "${_qtclient_config_dir}")
  file(WRITE "${_qtclient_config_dir}/QtClientConfig.cmake" "# Generated — build tree
if(NOT TARGET QtClient::Host)
  add_library(QtClient::Host INTERFACE IMPORTED)
  set_target_properties(QtClient::Host PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES \"${TIBIA_ROOT}/client/tibia\"
    INTERFACE_LINK_LIBRARIES \"tibia\")
endif()
set(QtClient_FOUND TRUE)
set(QTCLIENT_ROOT \"${TIBIA_ROOT}\")
include(\"\${CMAKE_CURRENT_LIST_DIR}/../cmake/QtClientMod.cmake\" OPTIONAL)
")
  message(STATUS "qtclient_host: ENABLE_EXPORTS on tibia (pack import ABI)")
endfunction()
