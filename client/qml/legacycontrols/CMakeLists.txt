# QtQuick.LegacyControls: Tibia's port of Qt Quick Controls 1 (LGPL), QML from
# the Tibia client plus the Qt 5 private C++ types it needs.
file(GLOB LEGACY_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qml ${CMAKE_CURRENT_SOURCE_DIR}/*.js)
file(GLOB LEGACY_CPP CONFIGURE_DEPENDS
    ${QTCLIENT_QML_SOURCE_DIR}/framework/qml/legacycontrols/*.cpp ${QTCLIENT_QML_SOURCE_DIR}/framework/qml/legacycontrols/*.h)
set_source_files_properties(TextSingleton.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(qtclient_qml_legacy
    URI QtQuick.LegacyControls
    VERSION 1.0
    STATIC
    NO_LINT
    OUTPUT_DIRECTORY ${QTCLIENT_QML_OUTPUT_DIR}/QtQuick/LegacyControls
    QML_FILES ${LEGACY_QML}
    SOURCES ${LEGACY_CPP}
    IMPORTS QtQuick QtQuick.Layouts QtQuick.Window
)
target_include_directories(qtclient_qml_legacy PRIVATE ${QTCLIENT_QML_SOURCE_DIR}/framework/qml/legacycontrols)
target_link_libraries(qtclient_qml_legacy PRIVATE Qt6::Quick Qt6::Qml)
