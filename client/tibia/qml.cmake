# *****************************************************************************
# Qt Quick host: runs Tibia's QML UI inside the client (TOGGLE_QML)
# *****************************************************************************
# Qt is not a vcpkg dependency: tools/qt/install-qt.sh installs the pinned
# version and the build scripts pass its prefix in CMAKE_PREFIX_PATH.

option(TOGGLE_QML "Embed Qt Quick and run Tibia's QML UI" ON)

set(QTCLIENT_QML OFF)
if(TOGGLE_QML AND NOT ANDROID)
  # The project's legacy cmake/FindOpenGL.cmake does not set OpenGL_FOUND,
  # which Qt's WrapOpenGL needs: find Qt with CMake's own module.
  # It also caches OPENGL_gl_LIBRARY as "-framework OpenGL", which CMake's
  # module rejects; shadow it with the framework path for the lookup.
  set(_qtclient_module_path "${CMAKE_MODULE_PATH}")
  list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
  if(APPLE AND OPENGL_FRAMEWORK)
    set(OPENGL_gl_LIBRARY "${OPENGL_FRAMEWORK}")
  endif()
  find_package(Qt6 6.10 QUIET COMPONENTS Core Gui GuiPrivate Qml Quick QuickControls2 ShaderTools)
  unset(OPENGL_gl_LIBRARY)
  set(CMAKE_MODULE_PATH "${_qtclient_module_path}")
  if(TARGET Qt6::Quick)
    set(QTCLIENT_QML ON)
    message(STATUS "QML UI (Qt ${Qt6_VERSION}): ON")
  else()
    message(WARNING "TOGGLE_QML is ON but Qt 6.10 was not found (run tools/qt/install-qt.sh); building without the QML UI")
  endif()
endif()

if(QTCLIENT_QML)
  qt_standard_project_setup(REQUIRES 6.10)

  # Qt 6.10's qyieldcpu.h calls __yield without <arm_acle.h>, which current
  # Apple clang rejects. Only targets created below (the Qt ones) get this.
  if(APPLE AND (CMAKE_OSX_ARCHITECTURES MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"))
    add_compile_options("SHELL:-include arm_acle.h")
  endif()

  set(QTCLIENT_QML_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qml ${CMAKE_CURRENT_BINARY_DIR}/qml)

  file(GLOB_RECURSE QTCLIENT_QML_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/framework/qml/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/framework/qml/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/client/qml/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/client/qml/*.h
  )
  # Built by the QML module targets under ../qml.
  list(FILTER QTCLIENT_QML_SOURCES EXCLUDE REGEX "/framework/qml/(legacycontrols|stubs)/")

  # Qt code lives in its own library: AUTOMOC, no unity build, no PCH, and no
  # Qt keywords leaking into the engine sources.
  qt_add_library(qtclient_qml STATIC ${QTCLIENT_QML_SOURCES})
  set_target_properties(qtclient_qml PROPERTIES AUTOMOC ON UNITY_BUILD OFF)
  target_compile_definitions(qtclient_qml PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(qtclient_qml PUBLIC
    Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick Qt6::QuickControls2
    qtclient_qml_tibiaplugin qtclient_qml_legacyplugin
    qtclient_qml_webengineplugin qtclient_qml_webchannelplugin
  )
  # The QML host calls into the engine and the engine calls the host's
  # Qt-free hooks: static libraries may depend on each other cyclically.
  target_link_libraries(qtclient_qml PRIVATE qtclient_core)
  target_link_libraries(qtclient_core PUBLIC qtclient_qml)
  target_compile_definitions(qtclient_core PUBLIC QTCLIENT_HAS_QML)

  if(APPLE)
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY BUILD_RPATH "${QT6_INSTALL_PREFIX}/lib")
  elseif(UNIX AND NOT WASM)
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY BUILD_RPATH "${QT6_INSTALL_PREFIX}/lib")
  endif()
endif()
