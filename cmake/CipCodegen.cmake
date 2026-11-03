# cip_codegen(<target> [CHUNKS n])
#
# Generates CIP's QObject API (upstream/contract, vendored by `make tibia-sync`) as
# C++ bases into <binary dir>/cipgen and adds them to <target>:
#   cip_gen.h           cip::<CIP class> bases with CIP's Q_PROPERTY/signals/slots/Q_ENUM
#   cip_options.h       cip::EClientOption + ClientOptionNames
#   cip_registration.h  cip::registerQmlTypes()
#   qrc/qt/qml/qmlcomponents/{qmldir,qml/qmldir}  (generated, not linked; the app
#                                                module writes its own qmldir)
# Handwritten implementations are bound in a cip_impl.h on the include path
# with CIP_IMPLEMENT(::cip::<CIP class>, <impl class>).

set(CIP_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")

function(cip_codegen target)
  cmake_parse_arguments(ARG "" "CHUNKS" "" ${ARGN})
  if(NOT ARG_CHUNKS)
    set(ARG_CHUNKS 12)
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  set(contract "${CIP_ROOT}/client/upstream/contract")
  set(out "${CMAKE_CURRENT_BINARY_DIR}/cipgen")
  set(outputs
    "${out}/cip_fwd.h" "${out}/cip_gen.h" "${out}/cip_options.h"
    "${out}/cip_registration.h" "${out}/cip_registration.cpp"
    "${out}/qrc/qt/qml/qmlcomponents/qmldir" "${out}/qrc/qt/qml/qmlcomponents/qml/qmldir")
  set(sources "${out}/cip_registration.cpp")
  math(EXPR last "${ARG_CHUNKS} - 1")
  foreach(i RANGE ${last})
    string(LENGTH "${i}" len)
    if(len EQUAL 1)
      set(i "0${i}")
    endif()
    list(APPEND outputs "${out}/cip_gen_${i}.h" "${out}/cip_gen_${i}.cpp")
    list(APPEND sources "${out}/cip_gen_${i}.h" "${out}/cip_gen_${i}.cpp")
  endforeach()

  add_custom_command(
    OUTPUT ${outputs}
    COMMAND Python3::Interpreter "${CIP_ROOT}/tools/cip/gen_cpp.py" "${contract}" "${out}"
            --chunks ${ARG_CHUNKS} --qml "${CIP_ROOT}/client/upstream/qml/qmlcomponents"
    DEPENDS
      "${CIP_ROOT}/tools/cip/gen_cpp.py"
      "${contract}/moc.json" "${contract}/elements.json"
      "${contract}/qml_registrations.json" "${contract}/option_ids.json"
      "${CIP_ROOT}/client/upstream/MANIFEST.json"
    COMMENT "Generating CIP QObject bases from client/upstream/contract"
    VERBATIM)

  # Named target so STATIC linked packs can depend on codegen without linking
  # `tibia` (which would cycle: tibia → qtclient_mod_* → tibia).
  add_custom_target(cip_codegen_${target} DEPENDS ${outputs})

  set(CIP_QMLDIR_ROOT "${out}/qrc" PARENT_SCOPE)
  target_sources(${target} PRIVATE ${sources})
  target_include_directories(${target} PUBLIC "${out}")
  set_source_files_properties(${sources} PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON SKIP_PRECOMPILE_HEADERS ON)
  set_target_properties(${target} PROPERTIES AUTOMOC ON)
endfunction()
