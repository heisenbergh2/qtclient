# qtclient_add_mod — STATIC pack linked into the Soft tip (desktop + WASM).
#
#   qtclient_add_mod(<id>
#     SOURCES …
#     [QML_DIR dir]
#     [OVERRIDES_DIR dir]
#     [RESOURCES_DIR dir]
#     [MOD_JSON path]
#     [QML_MODULE name]
#   )
#
# Stages (no native MODULE/SIDE_MODULE):
#   mods/<id>/mod.json
#   mods/<id>/qml/<qmlModule>/…
#   mods/<id>/overrides/ …
#   mods/<id>/resources/ …
#
# Pack C++ is STATIC-linked into `tibia`. ModLoader calls qtclient_mod_entry
# directly (QTCLIENT_HAS_LINKED_PACK). QML stays filesystem for F5.

include_guard(GLOBAL)

function(qtclient_add_mod MOD_ID)
  cmake_parse_arguments(ARG "" "QML_DIR;OVERRIDES_DIR;RESOURCES_DIR;MOD_JSON;QML_MODULE" "SOURCES" ${ARGN})
  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "qtclient_add_mod(${MOD_ID}): SOURCES required")
  endif()
  if(NOT ARG_QML_MODULE)
    set(ARG_QML_MODULE "${MOD_ID}")
  endif()
  if(NOT TARGET tibia)
    message(FATAL_ERROR "qtclient_add_mod(${MOD_ID}): tibia target missing (linked tip)")
  endif()

  set(_target "qtclient_mod_${MOD_ID}")
  set(_outdir "${CMAKE_BINARY_DIR}/mods/${MOD_ID}")
  file(MAKE_DIRECTORY "${_outdir}")

  add_library(${_target} STATIC ${ARG_SOURCES})
  set_target_properties(${_target} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${_outdir}/lib"
    POSITION_INDEPENDENT_CODE ON)

  target_compile_definitions(${_target} PRIVATE QTCLIENT_MOD_BUILD=1)
  target_include_directories(${_target} PRIVATE
    "${TIBIA_ROOT}/include"
    "${TIBIA_ROOT}/client/tibia"
    "${CMAKE_CURRENT_SOURCE_DIR}")

  # Consume Soft include/defs without linking the executable (would cycle).
  target_include_directories(${_target} PRIVATE
    "$<TARGET_PROPERTY:tibia,INCLUDE_DIRECTORIES>")
  if(QTCLIENT_MOD_INTERNAL)
    target_compile_definitions(${_target} PRIVATE QTCLIENT_MOD_INTERNAL=1)
    target_compile_definitions(${_target} PRIVATE
      "$<TARGET_PROPERTY:tibia,COMPILE_DEFINITIONS>")
    target_compile_options(${_target} PRIVATE
      "$<TARGET_PROPERTY:tibia,COMPILE_OPTIONS>")
  endif()

  if(QTCLIENT_HOST_INCLUDE_DIR)
    target_include_directories(${_target} PRIVATE "${QTCLIENT_HOST_INCLUDE_DIR}")
  endif()

  if(TARGET Qt6::Core)
    target_link_libraries(${_target} PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    if(QTCLIENT_MOD_INTERNAL AND TARGET Qt6::GuiPrivate)
      target_link_libraries(${_target} PRIVATE Qt6::GuiPrivate)
    endif()
    if(QTCLIENT_MOD_INTERNAL AND TARGET Qt6::QuickPrivate)
      target_link_libraries(${_target} PRIVATE Qt6::QuickPrivate)
    endif()
  endif()

  set_target_properties(${_target} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    AUTOMOC ON)

  # Tip links the pack; ModLoader sees QTCLIENT_HAS_LINKED_PACK.
  target_link_libraries(tibia PRIVATE ${_target})
  target_compile_definitions(tibia PRIVATE QTCLIENT_HAS_LINKED_PACK=1)
  # Keep entry past DCE on WASM (whole-program LTO).
  if(EMSCRIPTEN OR WASM)
    target_link_options(tibia PRIVATE "LINKER:--undefined=qtclient_mod_entry")
  endif()

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
  \"qmlModule\": \"${MOD_ID}\"
}
")
  endif()

  if(ARG_QML_DIR AND EXISTS "${ARG_QML_DIR}")
    set(_qml_src_module "${ARG_QML_DIR}/${ARG_QML_MODULE}")
    if(EXISTS "${_qml_src_module}/qmldir" OR EXISTS "${_qml_src_module}")
      set(_qml_stage_from "${ARG_QML_DIR}")
    else()
      set(_qml_stage_from "${CMAKE_CURRENT_BINARY_DIR}/qml-stage-${MOD_ID}")
      file(MAKE_DIRECTORY "${_qml_stage_from}/${ARG_QML_MODULE}")
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

  # Stage FS pack next to tip after tibia links (pack STATIC is a link dep of tibia).
  add_custom_command(TARGET tibia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${_outdir}" "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}/lib"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:tibia>/mods/${MOD_ID}/native"
    COMMENT "install ${MOD_ID} FS pack next to tip")

  set(QTCLIENT_MOD_TARGET_${MOD_ID} "${_target}" PARENT_SCOPE)
  if(NOT TARGET qtclient_mods)
    add_custom_target(qtclient_mods COMMENT "all qtclient linked STATIC packs")
  endif()
  add_dependencies(qtclient_mods ${_target})
  message(STATUS "qtclient_add_mod: ${MOD_ID} STATIC → tibia + ${_outdir}")
endfunction()
