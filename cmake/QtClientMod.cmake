# qtclient_add_mod — unified pack build for desktop + WASM.
#
#   qtclient_add_mod(<id>
#     SOURCES …
#     [QML_DIR dir]
#     [OVERRIDES_DIR dir]
#     [RESOURCES_DIR dir]
#     [MOD_JSON path]
#   )
#
# Installs / stages:
#   mods/<id>/mod.json
#   mods/<id>/native/<prefix>   (shared lib / SIDE_MODULE)
#   mods/<id>/qml/<qmlModule>/… (optional FS QML module + qmldir)
#   mods/<id>/overrides/ …      (optional)
#   mods/<id>/resources/ …      (optional)
#
# Pack authors never choose PLUGIN vs SIDE_MODULE — this macro picks the toolchain.
# Native packs link QtClient::Host / qtclient_host / tibia (ENABLE_EXPORTS).
# QML is staged as a filesystem module (not STATIC-linked into the MODULE).

include_guard(GLOBAL)

function(qtclient_add_mod MOD_ID)
  cmake_parse_arguments(ARG "" "QML_DIR;OVERRIDES_DIR;RESOURCES_DIR;MOD_JSON;QML_MODULE" "SOURCES" ${ARGN})
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "qtclient_add_mod(${MOD_ID}): SOURCES required")
  endif()
  if(NOT ARG_QML_MODULE)
    set(ARG_QML_MODULE "${MOD_ID}")
  endif()

  set(_target "qtclient_mod_${MOD_ID}")
  set(_outdir "${CMAKE_BINARY_DIR}/mods/${MOD_ID}")
  file(MAKE_DIRECTORY "${_outdir}/native")

  if(EMSCRIPTEN OR WASM)
    add_library(${_target} SHARED ${ARG_SOURCES})
    # Relocatable SIDE_MODULE (host is MAIN_MODULE=2). -fPIC also set
    # project-wide for in-tree WASM; keep explicit for out-of-tree packs.
    target_compile_options(${_target} PRIVATE -fPIC)
    target_link_options(${_target} PRIVATE "SHELL:-sSIDE_MODULE=2")
    # ModLoader dlsym("qtclient_mod_entry") — keep the C entry past DCE.
    target_link_options(${_target} PRIVATE "LINKER:--export=qtclient_mod_entry")
    set_target_properties(${_target} PROPERTIES
      PREFIX ""
      SUFFIX ".wasm"
      OUTPUT_NAME "mod"
      LIBRARY_OUTPUT_DIRECTORY "${_outdir}/native"
      RUNTIME_OUTPUT_DIRECTORY "${_outdir}/native")
    # Resolve Qt + host symbols from MAIN_MODULE at dlopen — do not link a
    # second full Qt into the SIDE_MODULE.
    target_link_options(${_target} PRIVATE "SHELL:-sERROR_ON_UNDEFINED_SYMBOLS=0")
  else()
    add_library(${_target} MODULE ${ARG_SOURCES})
    set_target_properties(${_target} PROPERTIES
      PREFIX ""
      OUTPUT_NAME "mod"
      LIBRARY_OUTPUT_DIRECTORY "${_outdir}/native"
      RUNTIME_OUTPUT_DIRECTORY "${_outdir}/native")
  endif()

  target_compile_definitions(${_target} PRIVATE QTCLIENT_MOD_BUILD=1)
  # Public Mod API surface (include/qtclient/mod_api_v1.h → mods/* via client/tibia).
  target_include_directories(${_target} PRIVATE
    "${TIBIA_ROOT}/include"
    "${TIBIA_ROOT}/client/tibia"
    "${CMAKE_CURRENT_SOURCE_DIR}")

  # Host import ABI. Prefer QtClient::Host / qtclient_host, else tibia.
  # Out-of-tree packs (client-sdk): optional QTCLIENT_HOST_LIBRARY path, else
  # allow unresolved host symbols until ModLoader dlopen (FDW-style).
  set(_host_target "")
  if(TARGET QtClient::Host)
    set(_host_target QtClient::Host)
  elseif(TARGET qtclient_host)
    set(_host_target qtclient_host)
  elseif(TARGET tibia)
    set_property(TARGET tibia PROPERTY ENABLE_EXPORTS ON)
    set(_host_target tibia)
  endif()
  # client-sdk ships INTERFACE-only QtClient::Host (headers, no binary). Treat
  # that like "no host" so MODULE/SIDE_MODULE can leave host symbols unresolved
  # until ModLoader dlopen (FDW-style).
  set(_host_is_interface FALSE)
  if(_host_target)
    get_target_property(_host_type ${_host_target} TYPE)
    if(_host_type STREQUAL "INTERFACE_LIBRARY")
      set(_host_is_interface TRUE)
    endif()
  endif()
  if(_host_target AND NOT _host_is_interface)
    target_link_libraries(${_target} PRIVATE ${_host_target})
  elseif(QTCLIENT_HOST_LIBRARY)
    target_link_libraries(${_target} PRIVATE "${QTCLIENT_HOST_LIBRARY}")
  elseif(NOT EMSCRIPTEN AND NOT WASM)
    if(APPLE)
      target_link_options(${_target} PRIVATE "LINKER:-undefined,dynamic_lookup")
    elseif(UNIX)
      target_link_options(${_target} PRIVATE "LINKER:--allow-shlib-undefined")
    elseif(MSVC)
      # Resolve Soft host exports at LoadLibrary time.
      target_link_options(${_target} PRIVATE "/FORCE:UNRESOLVED")
    endif()
  endif()
  # INTERFACE Host still contributes include dirs.
  if(_host_is_interface)
    target_link_libraries(${_target} PRIVATE ${_host_target})
    if(NOT EMSCRIPTEN AND NOT WASM)
      if(APPLE)
        target_link_options(${_target} PRIVATE "LINKER:-undefined,dynamic_lookup")
      elseif(UNIX)
        target_link_options(${_target} PRIVATE "LINKER:--allow-shlib-undefined")
      elseif(MSVC)
        target_link_options(${_target} PRIVATE "/FORCE:UNRESOLVED")
      endif()
    endif()
  endif()
  if(QTCLIENT_HOST_INCLUDE_DIR)
    target_include_directories(${_target} PRIVATE "${QTCLIENT_HOST_INCLUDE_DIR}")
  endif()

  # Include dirs for mod_api_v1.h → mods/*.h / sprites/hdscale.h.
  # Temporary migration bridge only (IdleOT). New packs must not set QTCLIENT_MOD_INTERNAL.
  if(TARGET tibia)
    target_include_directories(${_target} PRIVATE
      "$<TARGET_PROPERTY:tibia,INCLUDE_DIRECTORIES>")
    if(QTCLIENT_MOD_INTERNAL)
      target_compile_definitions(${_target} PRIVATE QTCLIENT_MOD_INTERNAL=1)
      target_compile_definitions(${_target} PRIVATE
        "$<TARGET_PROPERTY:tibia,COMPILE_DEFINITIONS>")
      target_compile_options(${_target} PRIVATE
        "$<TARGET_PROPERTY:tibia,COMPILE_OPTIONS>")
    endif()
  elseif(QTCLIENT_MOD_INTERNAL)
    # Out-of-tree pack against client-sdk or qtclient source + build (cip_gen, engine).
    # Matches qtclient_deps INTERFACE includes + FRAMEWORK_* defs from client/CMakeLists.txt.
    target_compile_definitions(${_target} PRIVATE
      QTCLIENT_MOD_INTERNAL=1
      QT_NO_KEYWORDS
      FRAMEWORK_GRAPHICS
      FRAMEWORK_NET
      FRAMEWORK_PROTOBUF
      FRAMEWORK_XML
      QTCLIENT_LUA=0
      QTCLIENT_NATIVE_WINDOW=0
      TIBIA_WITH_GAMECORE
      TIBIA_NO_OPENSSL)
    target_include_directories(${_target} PRIVATE
      "${TIBIA_ROOT}"
      "${TIBIA_ROOT}/include"
      "${TIBIA_ROOT}/include/engine"
      "${TIBIA_ROOT}/client"
      "${TIBIA_ROOT}/client/tibia"
      "${TIBIA_ROOT}/client/tibia/engine"
      "${TIBIA_ROOT}/client/tibia/engine/framework/graphics/glad/include")
    # Prefer SDK-bundled cipgen (pack-client-sdk.sh), else a Soft host build tree.
    if(EXISTS "${TIBIA_ROOT}/cipgen/cip_gen.h")
      target_include_directories(${_target} PRIVATE "${TIBIA_ROOT}/cipgen")
    elseif(QTCLIENT_HOST_BUILD)
      set(_host_build "${QTCLIENT_HOST_BUILD}")
    elseif(DEFINED ENV{QTCLIENT_HOST_BUILD} AND EXISTS "$ENV{QTCLIENT_HOST_BUILD}")
      set(_host_build "$ENV{QTCLIENT_HOST_BUILD}")
    else()
      set(_host_build "")
    endif()
    if(DEFINED _host_build AND _host_build)
      if(EXISTS "${_host_build}/client/tibia/cipgen/cip_gen.h")
        target_include_directories(${_target} PRIVATE
          "${_host_build}/client/tibia/cipgen"
          "${_host_build}/client/tibia/protobuf")
      else()
        message(WARNING "qtclient_add_mod(${MOD_ID}): QTCLIENT_HOST_BUILD=${_host_build} missing cipgen/cip_gen.h")
      endif()
    elseif(NOT EXISTS "${TIBIA_ROOT}/cipgen/cip_gen.h")
      message(WARNING "qtclient_add_mod(${MOD_ID}): QTCLIENT_MOD_INTERNAL needs client-sdk cipgen/ or -DQTCLIENT_HOST_BUILD=…")
    endif()
    # Generated CIP protobuf stubs (#include <appearances.pb.h>). client-sdk ships
    # them under include/protobuf/; Soft host builds under client/tibia/protobuf/.
    foreach(_pb_inc IN ITEMS
        "${TIBIA_ROOT}/include/protobuf"
        "${TIBIA_ROOT}/client/tibia/protobuf")
      if(EXISTS "${_pb_inc}")
        target_include_directories(${_target} PRIVATE "${_pb_inc}")
      endif()
    endforeach()
    # Soft C++ deps (fmt, …). client-sdk is headers-only — packs must use vcpkg
    # (manifest / host build / VCPKG_ROOT installed) the same way tibia does.
    set(_vcpkg_inc_roots "")
    if(DEFINED _host_build AND _host_build)
      list(APPEND _vcpkg_inc_roots "${_host_build}/vcpkg_installed")
    endif()
    list(APPEND _vcpkg_inc_roots
      "${CMAKE_BINARY_DIR}/vcpkg_installed"
      "${CMAKE_CURRENT_BINARY_DIR}/vcpkg_installed")
    if(DEFINED ENV{VCPKG_ROOT})
      list(APPEND _vcpkg_inc_roots "$ENV{VCPKG_ROOT}/installed")
    endif()
    foreach(_root IN LISTS _vcpkg_inc_roots)
      file(GLOB _vcpkg_inc "${_root}/*/include")
      foreach(_inc IN LISTS _vcpkg_inc)
        target_include_directories(${_target} PRIVATE
          "${_inc}"
          "${_inc}/stduuid"
          "${_inc}/utf8cpp")
      endforeach()
    endforeach()
    find_package(fmt CONFIG QUIET)
    if(TARGET fmt::fmt)
      target_link_libraries(${_target} PRIVATE fmt::fmt)
    endif()
  endif()

  # Qt headers for QObject / QString / rhi. Desktop MODULE links Qt; WASM
  # SIDE_MODULE only consumes includes/defs — symbols come from MAIN_MODULE.
  if(TARGET Qt6::Core)
    if(EMSCRIPTEN OR WASM)
      set(_qt_mods Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
      if(QTCLIENT_MOD_INTERNAL)
        list(APPEND _qt_mods Qt6::GuiPrivate Qt6::QuickPrivate)
      endif()
      foreach(_qt IN LISTS _qt_mods)
        if(TARGET ${_qt})
          target_include_directories(${_target} PRIVATE
            "$<TARGET_PROPERTY:${_qt},INTERFACE_INCLUDE_DIRECTORIES>")
          target_compile_definitions(${_target} PRIVATE
            "$<TARGET_PROPERTY:${_qt},INTERFACE_COMPILE_DEFINITIONS>")
          target_compile_options(${_target} PRIVATE
            "$<TARGET_PROPERTY:${_qt},INTERFACE_COMPILE_OPTIONS>")
        endif()
      endforeach()
    else()
      target_link_libraries(${_target} PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
      # rhi/qshader.h (weather overlays) lives on Gui private headers.
      if(QTCLIENT_MOD_INTERNAL AND TARGET Qt6::GuiPrivate)
        target_link_libraries(${_target} PRIVATE Qt6::GuiPrivate)
      endif()
      if(QTCLIENT_MOD_INTERNAL AND TARGET Qt6::QuickPrivate)
        target_link_libraries(${_target} PRIVATE Qt6::QuickPrivate)
      endif()
    endif()
  endif()
  # Match host language dialect (tibia uses gnu++23 on this tree).
  # AUTOMOC: packs with Q_OBJECT / #include "foo.moc" (IdleOT socialdialog).
  set_target_properties(${_target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    AUTOMOC ON)

  set(_mod_json "${ARG_MOD_JSON}")
  if(NOT _mod_json)
    set(_mod_json "${CMAKE_CURRENT_SOURCE_DIR}/mod.json")
  endif()
  if(EXISTS "${_mod_json}")
    configure_file("${_mod_json}" "${_outdir}/mod.json" COPYONLY)
  else()
    file(WRITE "${_outdir}/mod.json" "{
  \"id\": \"${MOD_ID}\",
  \"QTCLIENT_MOD_API\": 1,
  \"autoload\": true,
  \"priority\": 50,
  \"native\": \"native/mod\",
  \"qmlModule\": \"${MOD_ID}\"
}
")
  endif()

  if(ARG_QML_DIR AND EXISTS "${ARG_QML_DIR}")
    # Prefer source already laid out as qml/<module>/…; otherwise nest flat files.
    set(_qml_src_module "${ARG_QML_DIR}/${ARG_QML_MODULE}")
    if(EXISTS "${_qml_src_module}/qmldir" OR EXISTS "${_qml_src_module}")
      set(_qml_stage_from "${ARG_QML_DIR}")
    else()
      set(_qml_stage_from "${CMAKE_CURRENT_BINARY_DIR}/qml-stage-${MOD_ID}")
      file(MAKE_DIRECTORY "${_qml_stage_from}/${ARG_QML_MODULE}")
      # Configure-time nest for flat pack trees (e.g. _sample).
      file(GLOB _flat_qml LIST_DIRECTORIES false "${ARG_QML_DIR}/*")
      foreach(_entry IN LISTS _flat_qml)
        get_filename_component(_name "${_entry}" NAME)
        if(_name STREQUAL "CMakeLists.txt")
          continue()
        endif()
        file(COPY "${_entry}" DESTINATION "${_qml_stage_from}/${ARG_QML_MODULE}")
      endforeach()
    endif()
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${_outdir}/qml"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${_qml_stage_from}" "${_outdir}/qml"
      COMMAND ${CMAKE_COMMAND}
        -DMODULE_DIR="${_outdir}/qml/${ARG_QML_MODULE}"
        -DMODULE_NAME="${ARG_QML_MODULE}"
        -P "${TIBIA_ROOT}/cmake/GeneratePackQmldir.cmake"
      COMMENT "stage ${MOD_ID} qml FS module ${ARG_QML_MODULE}")
  endif()
  if(ARG_OVERRIDES_DIR AND EXISTS "${ARG_OVERRIDES_DIR}")
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARG_OVERRIDES_DIR}" "${_outdir}/overrides"
      COMMENT "stage ${MOD_ID} overrides")
  endif()
  if(ARG_RESOURCES_DIR AND EXISTS "${ARG_RESOURCES_DIR}")
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARG_RESOURCES_DIR}" "${_outdir}/resources"
      COMMENT "stage ${MOD_ID} resources")
  endif()

  # Dev convenience: stage next to the client binary after the pack links.
  # Link dependency on tibia (ENABLE_EXPORTS) orders host before the MODULE;
  # do not add_dependencies(tibia → mod) — that would cycle with the link edge.
  # Wipe destination first — copy_directory merges and leaves stale flat qml/.
  if(TARGET tibia)
    add_custom_command(TARGET ${_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}"
      COMMENT "install ${MOD_ID} next to client")
  endif()

  set(QTCLIENT_MOD_TARGET_${MOD_ID} "${_target}" PARENT_SCOPE)
  if(NOT TARGET qtclient_mods)
    add_custom_target(qtclient_mods COMMENT "all qtclient MODULE/SIDE_MODULE packs")
  endif()
  add_dependencies(qtclient_mods ${_target})
  message(STATUS "qtclient_add_mod: ${MOD_ID} -> ${_outdir}")
endfunction()
