# QtClient tip host — packs STATIC-link into `tibia` (no MODULE/SIDE_MODULE ABI).
#
# Consumers still see QtClient::Host as includes + link-against-tibia for
# in-tree STATIC packs (via qtclient_add_mod). Out-of-tree packs rebuild the
# tip with -DQTCLIENT_LINKED_PACK=… rather than shipping a separate .wasm/.so.

include_guard(GLOBAL)

function(qtclient_host_finalize)
  if(NOT TARGET tibia)
    message(FATAL_ERROR "qtclient_host_finalize: tibia target missing")
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

  set(_qtclient_config_dir "${CMAKE_BINARY_DIR}/cmake")
  file(MAKE_DIRECTORY "${_qtclient_config_dir}")
  file(WRITE "${_qtclient_config_dir}/QtClientConfig.cmake" "# Generated — build tree (linked tip)
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
  message(STATUS "qtclient_host: tip links STATIC packs (no ENABLE_EXPORTS / MAIN_MODULE)")
endfunction()
