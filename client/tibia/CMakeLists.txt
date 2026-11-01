# tibia: CIP client architecture. CIP's QML UI (upstream/, vendored by
# `make tibia-sync`, never edited) runs on Qt Quick with CIP's bootstrap;
# controllers implement the generated cip:: bases (tools/cip/gen_cpp.py);
# The engine (protocol, world, assets, net buffers) is compiled into this
# executable. Packs link the fat host ABI via ENABLE_EXPORTS / QtClient::Host
# (see cmake/QtClientHost.cmake). There is no separate gamecore library.
#
# Built two ways:
#   - from the qtclient host (add_subdirectory): desktop, wasm, mobile.
#   - standalone (cmake -S client/tibia): QML shell only.
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED PROJECT_NAME)
  project(tibia LANGUAGES CXX)
  set(TIBIA_WITH_GAMECORE OFF)
elseif(TIBIA_ENGINE_SOURCES)
  set(TIBIA_WITH_GAMECORE ON)
endif()
# Nested under the host project too — Qt5LegacyControls padding.h needs C++20
# (defaulted operator==); Qt's toolchain default is still C++17.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(TIBIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${TIBIA_ROOT}/cmake/CipCodegen.cmake")
include("${TIBIA_ROOT}/cmake/CipQmlModule.cmake")
include("${TIBIA_ROOT}/cmake/TibiaMods.cmake")
include("${TIBIA_ROOT}/cmake/QtClientMod.cmake")
include("${TIBIA_ROOT}/cmake/QtClientHost.cmake")

# Dev: load QML from the source tree (umbrella `make client-build`). Production
# packaging leaves this OFF so QML stays embedded + qmlcachegen'd.
option(TIBIA_QML_DISK "Load CIP/mod QML from disk with hot reload (dev)" OFF)

# WASM host must be relocatable MAIN_MODULE so ModLoader can dlopen pack native/.
if(EMSCRIPTEN OR WASM)
  option(TIBIA_WASM_MAIN_MODULE "Build tibia.wasm as Emscripten MAIN_MODULE for runtime packs" ON)
endif()


# Project FindOpenGL predates Qt's OpenGL lookup; Qt must use CMake's.
set(_tibia_module_path "${CMAKE_MODULE_PATH}")
list(REMOVE_ITEM CMAKE_MODULE_PATH "${TIBIA_ROOT}/cmake")
if(APPLE AND OPENGL_FRAMEWORK AND NOT IS_ABSOLUTE "${OPENGL_gl_LIBRARY}")
  set(OPENGL_gl_LIBRARY "${OPENGL_FRAMEWORK}")
  set(_tibia_unset_gl ON)
endif()
find_package(Qt6 6.10 QUIET COMPONENTS Core Gui Network Qml Quick QuickPrivate QuickControls2 ShaderTools Core5Compat)
# CipSoft launcher stage embedded in tibia.wasm (Widgets UI).
if(EMSCRIPTEN OR WASM)
  find_package(Qt6 6.10 QUIET COMPONENTS Widgets)
endif()
if(_tibia_unset_gl)
  unset(OPENGL_gl_LIBRARY)
endif()
set(CMAKE_MODULE_PATH "${_tibia_module_path}")
if(NOT Qt6_FOUND)
  message(STATUS "tibia: Qt 6.10 not found (make qt, then pass CMAKE_PREFIX_PATH); skipping")
  return()
endif()

qt_standard_project_setup(REQUIRES 6.10)
set(CMAKE_UNITY_BUILD OFF)
set(CMAKE_AUTOMOC ON)
# Qt 6.10 headers use __yield without including arm_acle.h on Apple arm64.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  add_compile_options("SHELL:-include arm_acle.h")
endif()

# Desktop OpenGL: macOS/Linux use vendored glad; Windows uses the compile-only
# gl_win32 surface (Qt QRhi owns the GPU). ES/wasm use GLES3 (glutil.h).
# Glad is wired after Qt so we never call find_package(OpenGL) before WrapOpenGL.
if(NOT ANDROID AND NOT WASM AND NOT EMSCRIPTEN AND NOT WIN32)
  add_subdirectory(engine/framework/graphics/glad)
  if(TARGET qtclient_deps)
    target_link_libraries(qtclient_deps INTERFACE tibia_glad)
  endif()
endif()

get_target_property(_qt_core_type Qt6::Core TYPE)
if(_qt_core_type STREQUAL "STATIC_LIBRARY")
  set(TIBIA_QT_STATIC ON)
endif()

add_subdirectory(legacycontrols)
add_subdirectory(qmlstubs)
# Resolve enabled packs before qmlcomponents so pack overrides/ layers apply.
tibia_resolve_enabled_mods()
cip_qml_module()

qt_add_executable(tibia
  main.cpp
  app/bootstrap.cpp app/bootstrap.h
  app/clientoptions.cpp app/clientoptions.h
  app/graphicsrestart.cpp app/graphicsrestart.h
  app/crashlog.cpp app/crashlog.h
  app/cipdomainmap.cpp app/cipdomainmap.h
  app/launcherenv.cpp app/launcherenv.h
  app/sessionbootstrap.cpp app/sessionbootstrap.h
  app/hints.cpp app/hints.h
  app/overrideinterceptor.cpp app/overrideinterceptor.h
  app/packqmlinterceptor.cpp app/packqmlinterceptor.h
  app/qmldisk.cpp app/qmldisk.h
  authentication/loginrequestuploader.cpp authentication/loginrequestuploader.h
  shared/clientservicesuploader.cpp shared/clientservicesuploader.h
  game/boostedcreaturerequestuploader.cpp game/boostedcreaturerequestuploader.h
  game/liveinformationrequestuploader.cpp game/liveinformationrequestuploader.h
  game/showoffrequestuploader.cpp game/showoffrequestuploader.h
  news/eventqmlentry.cpp news/eventqmlentry.h
  news/eventsuploader.cpp news/eventsuploader.h
  news/newsrequestuploader.cpp news/newsrequestuploader.h
  news/newsstorage.cpp news/newsstorage.h
  controllers/characterselectioncontroller.cpp controllers/characterselectioncontroller.h
  gamewindow/gamewindowcontroller.cpp gamewindow/gamewindowcontroller.h
  gamewindow/newsdialogcontroller.cpp gamewindow/newsdialogcontroller.h
  gamewindow/calendardialogcontroller.cpp gamewindow/calendardialogcontroller.h
  gamewindow/naggingmessagedialogcontroller.cpp gamewindow/naggingmessagedialogcontroller.h
  gamewindow/mousecursorcontroller.cpp gamewindow/mousecursorcontroller.h
  gamewindow/sidebarmanager.cpp gamewindow/sidebarmanager.h
  gamewindow/sidebarwidgetcontrollerbase.cpp gamewindow/sidebarwidgetcontrollerbase.h
  imageproviders/optimizedborderimageprovider.cpp imageproviders/optimizedborderimageprovider.h
  imageproviders/imagestripimageprovider.cpp imageproviders/imagestripimageprovider.h
  qmlcomponents/outlinetext.cpp qmlcomponents/outlinetext.h
  qmlcomponents/outfittypes.cpp qmlcomponents/outfittypes.h
  qmlcomponents/texthelper.cpp qmlcomponents/texthelper.h
  qmlcomponents/tooltiphelper.cpp qmlcomponents/tooltiphelper.h
  qmlcomponents/tutorialhelper.cpp qmlcomponents/tutorialhelper.h
  renderer/renderdriver.cpp renderer/renderdriver.h
  cip_impl.h
)
cip_codegen(tibia)
target_include_directories(tibia PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# WASM qtclient_deps historically omitted repo/client roots that desktop has —
# keep them on tibia so #include <tibia/…> and "launcher/…" resolve.
if(WASM OR EMSCRIPTEN)
  target_include_directories(tibia PRIVATE
    "${TIBIA_ROOT}"
    "${TIBIA_ROOT}/client")
endif()
target_compile_definitions(tibia PRIVATE QT_NO_KEYWORDS)

if(TIBIA_QML_DISK)
  if(WASM OR EMSCRIPTEN OR ANDROID OR IOS)
    message(FATAL_ERROR "TIBIA_QML_DISK is desktop-dev only")
  endif()
  message(STATUS "tibia: TIBIA_QML_DISK — QML from source tree, NO_CACHEGEN, F5 clears cache")
  # Override roots (colon-separated): engine Qt forks + enabled pack stock forks.
  set(_qml_override_dirs "${TIBIA_ROOT}/client/overrides")
  set(_qml_mod_dirs "")
  foreach(_mod IN LISTS QTCLIENT_ENABLED_MODS)
    set(_pack_ov "${TIBIA_ROOT}/client/mods/${_mod}/overrides")
    if(EXISTS "${_pack_ov}")
      string(APPEND _qml_override_dirs ":${_pack_ov}")
    endif()
    set(_pack_qml "${TIBIA_ROOT}/client/mods/${_mod}/qml")
    if(EXISTS "${_pack_qml}")
      if(_qml_mod_dirs STREQUAL "")
        set(_qml_mod_dirs "${_pack_qml}")
      else()
        string(APPEND _qml_mod_dirs ":${_pack_qml}")
      endif()
    endif()
  endforeach()
  target_compile_definitions(tibia PRIVATE
    TIBIA_QML_DISK=1
    TIBIA_QML_CIP_DIR="${TIBIA_ROOT}/client/upstream/qml"
    TIBIA_QML_MOD_DIR="${_qml_mod_dirs}"
    TIBIA_QML_OVERRIDES_DIR="${_qml_override_dirs}")
endif()
if(WIN32)
  # NOMINMAX: windows.h otherwise macros min/max. Full SDK include order and
  # rpcndr cleanup live in framework/win32_platform.h (via pch / compiler.h).
  target_compile_definitions(tibia PRIVATE NOMINMAX)
  target_link_libraries(tibia PRIVATE ws2_32 imagehlp)
endif()
# QuickPrivate: QQuickText, which CIP's outline text items wrap.
target_link_libraries(tibia PRIVATE
  Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickPrivate Qt6::QuickControls2
  tibia_legacycontrolsplugin tibia_webengineplugin tibia_webchannelplugin
  qmlcomponentsplugin)
# Public HD scaler + mods registry / WorldView / ModLoader; packs via TibiaMods + QtClientMod.
target_sources(tibia PRIVATE
  sprites/hdscale.cpp sprites/hdscale.h
  mods/registry.cpp mods/registry.h
  mods/mod_api_v1.cpp
  mods/mission_backend.h mods/attention_backend.h mods/instance_backend.h mods/weather_driver.h
  mods/worldview.cpp mods/worldview.h
  mods/modloader.cpp mods/modloader.h
  mods/mod_entry.h)
target_include_directories(tibia PRIVATE "${TIBIA_ROOT}/include")
# Host build id for ModLoader / mod_api_v1 — full SHA for artifact pinning.
# Prefer CI/env (SOURCE_COMMIT / GITHUB_SHA / QTCLIENT_HOST_BUILD_ID), else git HEAD.
set(_qtclient_host_sha "$ENV{QTCLIENT_HOST_BUILD_ID}")
if(NOT _qtclient_host_sha)
  set(_qtclient_host_sha "$ENV{SOURCE_COMMIT}")
endif()
if(NOT _qtclient_host_sha)
  set(_qtclient_host_sha "$ENV{GITHUB_SHA}")
endif()
if(NOT _qtclient_host_sha)
  execute_process(
    COMMAND git -C "${TIBIA_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE _qtclient_host_sha
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
endif()
if(NOT _qtclient_host_sha)
  set(_qtclient_host_sha "unknown")
endif()
target_compile_definitions(tibia PRIVATE "QTCLIENT_HOST_BUILD_ID=\"${_qtclient_host_sha}\"")
# Pack install stamps mod.json from this file (matches the binary, not a later git HEAD).
file(WRITE "${CMAKE_BINARY_DIR}/hostBuildId" "${_qtclient_host_sha}\n")
# Product identity: stock CipSoft defaults; enabled packs set these via PARENT_SCOPE
# (see client/mods/*/CMakeLists.txt). Applied once after mods so -D is not redefined.
set(TIBIA_PRODUCT_NAME "Tibia")
set(TIBIA_PRODUCT_ORG "CipSoft")
set(TIBIA_PRODUCT_URL "https://www.tibia.com/")
set(TIBIA_DEFAULT_CDN "")
set(TIBIA_CLIENT_PACKAGE_DIR "Tibia.app")

tibia_add_enabled_mods()

target_compile_definitions(tibia PRIVATE
  "TIBIA_PRODUCT_NAME=\"${TIBIA_PRODUCT_NAME}\""
  "TIBIA_PRODUCT_ORG=\"${TIBIA_PRODUCT_ORG}\""
  "TIBIA_PRODUCT_URL=\"${TIBIA_PRODUCT_URL}\""
  "TIBIA_DEFAULT_CDN=\"${TIBIA_DEFAULT_CDN}\""
  "TIBIA_CLIENT_PACKAGE_DIR=\"${TIBIA_CLIENT_PACKAGE_DIR}\"")

# Bundle identity: CIP-neutral; product mods override at runtime via primaryIdentity().
set(_tibia_bundle_id com.tibia.client)
set(_tibia_bundle_name Tibia)
set_target_properties(tibia PROPERTIES
  MACOSX_BUNDLE ON
  MACOSX_BUNDLE_GUI_IDENTIFIER ${_tibia_bundle_id}
  MACOSX_BUNDLE_BUNDLE_NAME ${_tibia_bundle_name}
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
  WIN32_EXECUTABLE ON
  QT_QML_ROOT_PATH "${CIP_ROOT}/client/upstream/qml")
# Desktop qtclient Play binary is `client` (Contents/MacOS/client). Browser CDN expects tibia.js + tibia.wasm.
if(EMSCRIPTEN)
  set_target_properties(tibia PROPERTIES OUTPUT_NAME tibia)
else()
  set_target_properties(tibia PROPERTIES OUTPUT_NAME client)
endif()
# Match client/CMakeLists.txt TOGGLE_BIN_FOLDER so package_desktop --bin build/<preset>/bin finds it.
if(TOGGLE_BIN_FOLDER)
  set_target_properties(tibia PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  if(APPLE)
    set_target_properties(tibia PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  endif()
endif()

if(TIBIA_WITH_GAMECORE)
  target_sources(tibia PRIVATE
    app/qtplatformwindow.cpp app/qtplatformwindow.h
    chat/chatgameactionhandler.cpp chat/chatgameactionhandler.h
    shared/sharedciptypes.cpp shared/sharedciptypes.h
    config/configciptypes.cpp config/configciptypes.h
    sound/soundciptypes.cpp sound/soundciptypes.h
    sessiondump/sessiondumpciptypes.cpp sessiondump/sessiondumpciptypes.h
    network/networkciptypes.cpp network/networkciptypes.h
    protocol/protocolciptypes.cpp protocol/protocolciptypes.h
    assets/assetsciptypes.cpp assets/assetsciptypes.h
    authentication/authenticationciptypes.cpp authentication/authenticationciptypes.h
    taskboard/taskboardciptypes.cpp taskboard/taskboardciptypes.h
    news/newsciptypes.cpp news/newsciptypes.h
    client/clientciptypes.cpp client/clientciptypes.h
    common/commonciptypes.cpp common/commonciptypes.h
    dailyreward/dailyrewardciptypes.cpp dailyreward/dailyrewardciptypes.h
    questlog/questlogciptypes.cpp questlog/questlogciptypes.h
    quickloot/quicklootciptypes.cpp quickloot/quicklootciptypes.h
    skillwheel/skillwheelciptypes.cpp skillwheel/skillwheelciptypes.h
    systemsurvey/systemsurveyciptypes.cpp systemsurvey/systemsurveyciptypes.h
    teamfinder/teamfinderciptypes.cpp teamfinder/teamfinderciptypes.h
    bossbestiary/bossbestiaryciptypes.cpp bossbestiary/bossbestiaryciptypes.h
    bossbestiary/bosstrackerstorage.cpp bossbestiary/bosstrackerstorage.h
    exaltation/exaltationciptypes.cpp exaltation/exaltationciptypes.h
    exivaoptions/exivaoptionsciptypes.cpp exivaoptions/exivaoptionsciptypes.h
    highscores/highscoresciptypes.cpp highscores/highscoresciptypes.h
    imbuing/imbuingciptypes.cpp imbuing/imbuingciptypes.h
    market/marketciptypes.cpp market/marketciptypes.h
    prey/preyciptypes.cpp prey/preyciptypes.h
    reporting/reportingciptypes.cpp reporting/reportingciptypes.h
    screenshot/screenshotciptypes.cpp screenshot/screenshotciptypes.h
    spells/spellsciptypes.cpp spells/spellsciptypes.h
    proficiency/proficiencyciptypes.cpp proficiency/proficiencyciptypes.h
    gameWidnow/gameWidnowciptypes.cpp gameWidnow/gameWidnowciptypes.h
    chat/chatchannel.cpp chat/chatchannel.h
    chat/chatmodemanager.cpp chat/chatmodemanager.h
    chat/chatprotocolmessagehandler.cpp chat/chatprotocolmessagehandler.h
    chat/gamemessagehandler.cpp chat/gamemessagehandler.h
    chat/textstorage.cpp chat/textstorage.h
    shared/tibiaurltools.cpp shared/tibiaurltools.h
    gamewindow/chatcontroller.cpp gamewindow/chatcontroller.h
    creatures/creaturesciptypes.cpp creatures/creaturesciptypes.h
    friends/friendsciptypes.cpp friends/friendsciptypes.h
    gamewindow/gamewindowciptypes.cpp gamewindow/gamewindowciptypes.h
    gamewindow/remainingdialogs.cpp gamewindow/remainingdialogs.h
    gamewindow/whiteblacklistdialogcontroller.cpp gamewindow/whiteblacklistdialogcontroller.h
    gamewindow/reportviolationcontroller.cpp gamewindow/reportviolationcontroller.h
    gamewindow/bugreportdialogcontroller.cpp gamewindow/bugreportdialogcontroller.h
    client/gameclient.cpp client/gameclient.h
    client/gamecore.cpp client/gamecore.h
    qmlcomponents/qmlcomponentsciptypes.cpp qmlcomponents/qmlcomponentsciptypes.h
    qmlcomponents/soundhelper.cpp qmlcomponents/soundhelper.h
    config/sidebaroptions.cpp config/sidebaroptions.h
    container/containerciptypes.cpp container/containerciptypes.h
    container/containerslotinformation.cpp container/containerslotinformation.h
    container/qmlitemslotdata.cpp container/qmlitemslotdata.h
    config/specialconditionsoptions.cpp config/specialconditionsoptions.h
    config/specialconditionobject.cpp config/specialconditionobject.h
    config/optionsarchive.cpp config/optionsarchive.h
    config/clientoptiondefaults.cpp config/clientoptiondefaults.h
    config/optionspages.cpp config/optionspages.h
    config/hotkeyoptions.cpp config/hotkeyoptions.h
    app/portalurls.cpp app/portalurls.h
    game/gameciptypes.cpp game/gameciptypes.h
    game/cooldownstorage.cpp game/cooldownstorage.h
    gamewindow/actionbarcontroller.cpp gamewindow/actionbarcontroller.h
    gamewindow/actionbarmanager.cpp gamewindow/actionbarmanager.h
    gamewindow/cooldownbarcontroller.cpp gamewindow/cooldownbarcontroller.h
    gamewindow/actionbuttoncontroller.cpp gamewindow/actionbuttoncontroller.h
    gamewindow/assignactiondialogs.cpp gamewindow/assignactiondialogs.h
    input/keyboardshortcuthandler.cpp input/keyboardshortcuthandler.h
    input/inputciptypes.cpp input/inputciptypes.h
    input/clickareaeventfilter.cpp input/clickareaeventfilter.h
    input/eventfilters.cpp input/eventfilters.h
    input/domainhandlers.cpp input/domainhandlers.h
    input/crosshairgameactionhandlers.cpp input/crosshairgameactionhandlers.h
    input/playermovementgameactionhandler.cpp input/playermovementgameactionhandler.h
    input/playermovementinputinterpreter.cpp input/playermovementinputinterpreter.h
    gamewindow/targetselectioncontroller.cpp gamewindow/targetselectioncontroller.h
    spells/spellinfostorage.cpp spells/spellinfostorage.h
    gamewindow/messagedialogcontroller.cpp gamewindow/messagedialogcontroller.h
    gamewindow/deathdialogcontroller.cpp gamewindow/deathdialogcontroller.h
    gamewindow/createcharacterdialogcontroller.cpp gamewindow/createcharacterdialogcontroller.h
    gamewindow/createaccountdialogcontroller.cpp gamewindow/createaccountdialogcontroller.h
    gamewindow/waitdialogcontroller.cpp gamewindow/waitdialogcontroller.h
    gamewindow/optionsmenudialogcontroller.cpp gamewindow/optionsmenudialogcontroller.h
    gamewindow/fpslatencyindicatorcontroller.cpp gamewindow/fpslatencyindicatorcontroller.h
    gamewindow/aboutdialogcontroller.cpp gamewindow/aboutdialogcontroller.h
    game/gameservergamesession.cpp game/gameservergamesession.h
    game/sessiondumpgamesession.cpp game/sessiondumpgamesession.h
    game/sessiondumpdmpd.cpp game/sessiondumpdmpd.h
    tutorial/tutorialciptypes.cpp tutorial/tutorialciptypes.h
    tutorial/sessiondumphintsprocessor.cpp tutorial/sessiondumphintsprocessor.h
    sound/soundprotocolmessagehandler.cpp sound/soundprotocolmessagehandler.h
    sound/soundstorage.cpp sound/soundstorage.h
    sound/miniaudio_impl.cpp
    game/playerstates.cpp game/playerstates.h
    game/playerstatestracker.cpp game/playerstatestracker.h
    game/skillswidgetentrydata.cpp game/skillswidgetentrydata.h
    cyclopedia/cyclopediaciptypes.cpp cyclopedia/cyclopediaciptypes.h
    cyclopedia/creaturetrackerstorage.cpp cyclopedia/creaturetrackerstorage.h
    cyclopedia/combatstatsqmldata.cpp cyclopedia/combatstatsqmldata.h
    game/statusbarstorage.cpp game/statusbarstorage.h
    gamewindow/appearanceinstancelistmodel.cpp gamewindow/appearanceinstancelistmodel.h
    gamewindow/battlelistcontroller.cpp gamewindow/battlelistcontroller.h
    gamewindow/buttonbarcontroller.cpp gamewindow/buttonbarcontroller.h
    gamewindow/containercontroller.cpp gamewindow/containercontroller.h
    gamewindow/containerstoragecontroller.cpp gamewindow/containerstoragecontroller.h
    gamewindow/contextmenucontroller.cpp gamewindow/contextmenucontroller.h
    gamewindow/dialogmanager.cpp gamewindow/dialogmanager.h
    gamewindow/draganddropcontroller.cpp gamewindow/draganddropcontroller.h
    gamewindow/edittextcontroller.cpp gamewindow/edittextcontroller.h
    gamewindow/genericentertextdialogcontroller.cpp gamewindow/genericentertextdialogcontroller.h
    gamewindow/hitpointmanabarcontroller.cpp gamewindow/hitpointmanabarcontroller.h
    gamewindow/highscoresdialogcontroller.cpp gamewindow/highscoresdialogcontroller.h
    gamewindow/informationdialogs.cpp gamewindow/informationdialogs.h
    gamewindow/mapwindowcontroller.cpp gamewindow/mapwindowcontroller.h
    gamewindow/appearancetypelistmodel.cpp gamewindow/appearancetypelistmodel.h
    gamewindow/marketcontroller.cpp gamewindow/marketcontroller.h
    market/marketstorage.cpp market/marketstorage.h
    gamewindow/imbuingdialogcontroller.cpp gamewindow/imbuingdialogcontroller.h
    imbuing/imbuingstorage.cpp imbuing/imbuingstorage.h
    exaltation/exaltationstorage.cpp exaltation/exaltationstorage.h
    gamewindow/exaltationforgedialogcontroller.cpp gamewindow/exaltationforgedialogcontroller.h
    skillwheel/skillwheeldata.cpp skillwheel/skillwheeldata.h
    skillwheel/skillwheelstorage.cpp skillwheel/skillwheelstorage.h
    skillwheel/skillwheelmodels.cpp skillwheel/skillwheelmodels.h
    skillwheel/skillwheelcontrollers.cpp skillwheel/skillwheelcontrollers.h
    skillwheel/skillwheelmanager.cpp skillwheel/skillwheelmanager.h
    gamewindow/minimapcontroller.cpp gamewindow/minimapcontroller.h
    gamewindow/npcdialogcontroller.cpp gamewindow/npcdialogcontroller.h
    gamewindow/uifocusmanager.cpp gamewindow/uifocusmanager.h
    gamewindow/npckeywordbuttonmodel.cpp gamewindow/npckeywordbuttonmodel.h
    gamewindow/npctradecontroller.cpp gamewindow/npctradecontroller.h
    gamewindow/outfitdialogcontroller.cpp gamewindow/outfitdialogcontroller.h
    gamewindow/configurecreaturepodiumdialogcontroller.cpp gamewindow/configurecreaturepodiumdialogcontroller.h
    gamewindow/playerinventoryandstatuscontroller.cpp gamewindow/playerinventoryandstatuscontroller.h
    gamewindow/preydialogcontroller.cpp gamewindow/preydialogcontroller.h
    gamewindow/killtrackerwidgetcontroller.cpp gamewindow/killtrackerwidgetcontroller.h
    gamewindow/loottrackingwidgetcontroller.cpp gamewindow/loottrackingwidgetcontroller.h
    gamewindow/rewardwalldialogcontroller.cpp gamewindow/rewardwalldialogcontroller.h
    gamewindow/taskboarddialogcontroller.cpp gamewindow/taskboarddialogcontroller.h
    gamewindow/taskboardmodels.cpp gamewindow/taskboardmodels.h
    gamewindow/bountytaskscontroller.cpp gamewindow/bountytaskscontroller.h
    gamewindow/weeklytaskscontroller.cpp gamewindow/weeklytaskscontroller.h
    gamewindow/taskboardshopcontroller.cpp gamewindow/taskboardshopcontroller.h
    gamewindow/spelllistwidgetcontroller.cpp gamewindow/spelllistwidgetcontroller.h
    gamewindow/unjustifiedpointswidgetcontroller.cpp gamewindow/unjustifiedpointswidgetcontroller.h
    gamewindow/imbuementtrackerwidgetcontroller.cpp gamewindow/imbuementtrackerwidgetcontroller.h
    gamewindow/creaturetrackerwidgetcontroller.cpp gamewindow/creaturetrackerwidgetcontroller.h
    gamewindow/bosstrackerwidgetcontroller.cpp gamewindow/bosstrackerwidgetcontroller.h
    gamewindow/depotsearchwidgetcontroller.cpp gamewindow/depotsearchwidgetcontroller.h
    gamewindow/playerguidewidgetcontroller.cpp gamewindow/playerguidewidgetcontroller.h
    gamewindow/cipsoftuieventbridge.cpp gamewindow/cipsoftuieventbridge.h
    analysers/analyserstorages.cpp analysers/analyserstorages.h
    analysers/progressstorage.cpp analysers/gainwastestorage.cpp analysers/impactstorage.cpp
    analysers/analyserwidgets.cpp analysers/analyserwidgets.h
    analysers/impactwidgets.cpp analysers/impactwidgets.h
    proficiency/proficiencystorage.cpp proficiency/proficiencystorage.h
    proficiency/proficiencyperks.cpp proficiency/proficiencyperks.h
    proficiency/weaponproficiencydialogcontroller.cpp proficiency/weaponproficiencydialogcontroller.h
    proficiency/proficiencycatalogue.cpp proficiency/proficiencycatalogue.h
    proficiency/weaponproficiencyshapedialogs.cpp proficiency/weaponproficiencyshapedialogs.h
    gamewindow/cyclopediadialogcontroller.cpp gamewindow/cyclopediadialogcontroller.h
    gamewindow/characterinfodialogcontroller.cpp gamewindow/characterinfodialogcontroller.h
    gamewindow/characterinfomodels.cpp gamewindow/characterinfomodels.h
    cyclopedia/bestiarystorage.cpp cyclopedia/bestiarystorage.h
    gamewindow/monsterdialogcontroller.cpp gamewindow/monsterdialogcontroller.h
    gamewindow/monsterbonuseffectsdialogcontroller.cpp gamewindow/monsterbonuseffectsdialogcontroller.h
    cyclopedia/bosstiarystorage.cpp cyclopedia/bosstiarystorage.h
    gamewindow/bosstiarydialogcontroller.cpp gamewindow/bosstiarydialogcontroller.h
    cyclopedia/iteminfostorage.cpp cyclopedia/iteminfostorage.h
    cyclopedia/housesstorage.cpp cyclopedia/housesstorage.h
    gamewindow/housesinfodialogcontroller.cpp gamewindow/housesinfodialogcontroller.h
    cyclopedia/cyclopediamapdata.cpp cyclopedia/cyclopediamapdata.h
    gamewindow/cyclopediamapdialogcontroller.cpp gamewindow/cyclopediamapdialogcontroller.h
    gamewindow/magicalarchivedialogcontroller.cpp gamewindow/magicalarchivedialogcontroller.h
    qmlcomponents/worldmapcameraviewport.cpp qmlcomponents/worldmapcameraviewport.h
    qmlcomponents/worldmapqmltypes.cpp qmlcomponents/worldmapqmltypes.h
    gamewindow/iteminfodialogcontroller.cpp gamewindow/iteminfodialogcontroller.h
    gamewindow/questlogcontroller.cpp gamewindow/questlogcontroller.h
    gamewindow/questtrackerwidgetcontroller.cpp gamewindow/questtrackerwidgetcontroller.h
    gamewindow/servermodaldialogcontroller.cpp gamewindow/servermodaldialogcontroller.h
    gamewindow/playertradecontroller.cpp gamewindow/playertradecontroller.h
    gamewindow/selectobjectamountdialogcontroller.cpp gamewindow/selectobjectamountdialogcontroller.h
    gamewindow/blessingsdialogcontroller.cpp gamewindow/blessingsdialogcontroller.h
    gamewindow/tutorialhintdialogcontroller.cpp gamewindow/tutorialhintdialogcontroller.h
    gamewindow/vocationselectiondialogcontroller.cpp gamewindow/vocationselectiondialogcontroller.h
    gamewindow/editminimapmarkerdialogcontroller.cpp gamewindow/editminimapmarkerdialogcontroller.h
    gamewindow/editvipdialogcontroller.cpp gamewindow/editvipdialogcontroller.h
    gamewindow/premiumpanelcontroller.cpp gamewindow/premiumpanelcontroller.h
    gamewindow/vipstorage.cpp gamewindow/vipstorage.h
    gamewindow/vipwidgetcontroller.cpp gamewindow/vipwidgetcontroller.h
    exivaoptions/exivaoptionsstorage.cpp exivaoptions/exivaoptionsstorage.h
    exivaoptions/exivaoptionsdialogcontroller.cpp exivaoptions/exivaoptionsdialogcontroller.h
    gamewindow/sidebarpanelsmanager.cpp gamewindow/sidebarpanelsmanager.h
    gamewindow/sidebarwidgetsmanager.cpp gamewindow/sidebarwidgetsmanager.h
    gamewindow/skillswidgetcontroller.cpp gamewindow/skillswidgetcontroller.h
    gamewindow/stashdialogcontroller.cpp gamewindow/stashdialogcontroller.h
    gamewindow/managelootcontainerdialogcontroller.cpp gamewindow/managelootcontainerdialogcontroller.h
    quickloot/managedcontainerstorage.cpp quickloot/managedcontainerstorage.h
    gamewindow/statusbarcontroller.cpp gamewindow/statusbarcontroller.h
    gamewindow/storeandresourcebalancehelpercontroller.cpp gamewindow/storeandresourcebalancehelpercontroller.h
    gamewindow/storedialogcontroller.cpp gamewindow/storedialogcontroller.h
    gamewindow/bossdifficultyselectiondialogcontroller.cpp gamewindow/bossdifficultyselectiondialogcontroller.h
    input/gameaction.cpp input/gameaction.h
    input/gameactionhandlers.cpp input/gameactionhandlers.h
    input/gameactionmapper.cpp input/gameactionmapper.h
    input/genericobjectgameactionprovider.cpp input/genericobjectgameactionprovider.h
    minimap/minimapciptypes.cpp minimap/minimapciptypes.h
    minimap/minimapdiskio.cpp minimap/minimapdiskio.h
    minimap/minimapgameactionprovider.cpp minimap/minimapgameactionprovider.h
    minimap/minimapmanager.cpp minimap/minimapmanager.h
    minimap/minimapmarkerstorage.cpp minimap/minimapmarkerstorage.h
    minimap/minimaprenderprovider.cpp minimap/minimaprenderprovider.h
    minimap/minimaptile.cpp minimap/minimaptile.h
    minimap/minimaptilestorage.cpp minimap/minimaptilestorage.h
    minimap/minimapvisiblearea.cpp minimap/minimapvisiblearea.h
    minimap/minimapworldfeed.cpp minimap/minimapworldfeed.h
    minimap/waypointcosts.cpp minimap/waypointcosts.h
    network/extendedopcoderouter.cpp network/extendedopcoderouter.h
    network/gameserverconnection.cpp network/gameserverconnection.h
    network/gameserverdualconnection.cpp network/gameserverdualconnection.h
    appearances/appearanceinstance.cpp appearances/appearanceinstance.h
    appearances/genericstorage.h
    appearances/spriteinfo.cpp appearances/spriteinfo.h
    appearances/worldmaphighlightbaseplate.cpp appearances/worldmaphighlightbaseplate.h
    animation/animator.cpp animation/animator.h
    effects/numericaleffect.cpp effects/numericaleffect.h
    sprites/sprite.cpp sprites/sprite.h
    shared/cip_typed.h
    cip_ports/all_ports.cpp
    cip_ports/native_port.h
    cip_ports/animation_ports.h
    cip_ports/appearances_ports.h
    cip_ports/assets_ports.h
    cip_ports/authentication_ports.h
    cip_ports/bossbestiary_ports.h
    cip_ports/chat_ports.h
    cip_ports/client_ports.h
    cip_ports/common_ports.h
    cip_ports/config_ports.h
    cip_ports/container_ports.h
    cip_ports/creatures_ports.h
    cip_ports/cyclopedia_ports.h
    cip_ports/dailyreward_ports.h
    cip_ports/effects_ports.h
    cip_ports/exaltation_ports.h
    cip_ports/exivaoptions_ports.h
    cip_ports/friends_ports.h
    cip_ports/game_ports.h
    cip_ports/gameWidnow_ports.h
    cip_ports/gamewindow_ports.h
    cip_ports/highscores_ports.h
    cip_ports/imbuing_ports.h
    cip_ports/input_ports.h
    cip_ports/market_ports.h
    cip_ports/minimap_ports.h
    cip_ports/network_ports.h
    cip_ports/news_ports.h
    cip_ports/prey_ports.h
    cip_ports/proficiency_ports.h
    cip_ports/protocol_ports.h
    cip_ports/qmlcomponents_ports.h
    cip_ports/questlog_ports.h
    cip_ports/quickloot_ports.h
    cip_ports/renderer_ports.h
    cip_ports/reporting_ports.h
    cip_ports/screenshot_ports.h
    cip_ports/sessiondump_ports.h
    cip_ports/shared_ports.h
    cip_ports/skillwheel_ports.h
    cip_ports/sound_ports.h
    cip_ports/spells_ports.h
    cip_ports/sprites_ports.h
    cip_ports/store_ports.h
    cip_ports/systemsurvey_ports.h
    cip_ports/taskboard_ports.h
    cip_ports/teamfinder_ports.h
    cip_ports/trade_ports.h
    cip_ports/tutorial_ports.h
    cip_ports/worldmap_ports.h
    qmlcomponents/appearanceinstancerenderer.cpp qmlcomponents/appearanceinstancerenderer.h
    qmlcomponents/minimapquickitem.cpp qmlcomponents/minimapquickitem.h
    qmlcomponents/minimaprendererquickitem.cpp qmlcomponents/minimaprendererquickitem.h
    qmlcomponents/numericaleffectoverlay.cpp qmlcomponents/numericaleffectoverlay.h
    qmlcomponents/singleobjectrenderer.cpp qmlcomponents/singleobjectrenderer.h
    qmlcomponents/speechbubbleoverlay.cpp qmlcomponents/speechbubbleoverlay.h
    renderer/childrenscenegraphnode.h
    renderer/highlightedspritenode.cpp renderer/highlightedspritenode.h
    renderer/lightmodel.cpp renderer/lightmodel.h
    renderer/worldgeometry.cpp renderer/worldgeometry.h
    renderer/weatherframe.h
    renderer/lightmappainteditem.cpp renderer/lightmappainteditem.h
    renderer/nextframeupdater.cpp renderer/nextframeupdater.h
    world/walkcalculator.cpp world/walkcalculator.h
    renderer/rendervolumeprovider.cpp renderer/rendervolumeprovider.h
    renderer/scenegraphnodeprovider.cpp renderer/scenegraphnodeprovider.h
    renderer/spritetexturecache.cpp renderer/spritetexturecache.h
    renderer/spritetextureprovider.cpp renderer/spritetextureprovider.h
    renderer/worldmapquickitem.cpp renderer/worldmapquickitem.h
    renderer/worldmaprenderprovider.cpp renderer/worldmaprenderprovider.h
    renderer/worldview.cpp renderer/worldview.h
    store/storeciptypes.cpp store/storeciptypes.h
    store/dynamicallyloadedimage.cpp store/dynamicallyloadedimage.h
    store/qmlstoreobjects.cpp store/qmlstoreobjects.h
    store/storestorage.cpp store/storestorage.h
    trade/tradeciptypes.cpp trade/tradeciptypes.h
    trade/npctradestorage.cpp trade/npctradestorage.h
    trade/playertradestorage.cpp trade/playertradestorage.h
    worldmap/ambientlightstorage.cpp worldmap/ambientlightstorage.h
    worldmap/cacheforworldmapstoragelookupmembers.cpp worldmap/cacheforworldmapstoragelookupmembers.h
    worldmap/coordinates.h
    worldmap/creaturehud.cpp worldmap/creaturehud.h
    worldmap/creaturehudcontroller.cpp worldmap/creaturehudcontroller.h
    worldmap/creaturehudrenderprovider.cpp worldmap/creaturehudrenderprovider.h
    worldmap/lightmap.cpp worldmap/lightmap.h
    worldmap/nextframetimestamppredictor.cpp worldmap/nextframetimestamppredictor.h
    worldmap/numericaleffectstorage.cpp worldmap/numericaleffectstorage.h
    worldmap/worldmapfield.h
    worldmap/worldmapgameactionhandler.cpp worldmap/worldmapgameactionhandler.h
    worldmap/worldmapgameactionprovider.cpp worldmap/worldmapgameactionprovider.h
    worldmap/worldmappicker.cpp worldmap/worldmappicker.h
    worldmap/worldmapprotocolmessagehandler.cpp worldmap/worldmapprotocolmessagehandler.h
    worldmap/worldmapstorage.cpp worldmap/worldmapstorage.h
    worldmap/worldmaptutorialmarkeroverlaycontroller.cpp worldmap/worldmaptutorialmarkeroverlaycontroller.h
    worldmap/worldmapviewport.cpp worldmap/worldmapviewport.h)
  target_sources(tibia PRIVATE ${TIBIA_ENGINE_SOURCES}
    runtime/host.cpp
    schedule/qtschedule.cpp)
  # Engine translation units expect the old framework preamble (fmt, numbers,
  # asio). Force-include it here so it does not apply to Qt sources.
  set(_engine_cpp ${TIBIA_ENGINE_SOURCES})
  list(FILTER _engine_cpp INCLUDE REGEX "\\.cpp$")
  if(MSVC)
    set_property(SOURCE ${_engine_cpp} APPEND PROPERTY COMPILE_OPTIONS "/FIframework/pch.h")
  else()
    set_property(SOURCE ${_engine_cpp} APPEND PROPERTY COMPILE_OPTIONS "-include" "framework/pch.h")
  endif()
  target_link_libraries(tibia PRIVATE qtclient_deps protobuf)
  add_dependencies(tibia protocol_dispatch_check)
  # miniaudio (CIP music/ambience/UI); Ogg decode uses stb_vorbis inside
  # miniaudio_impl.cpp — no libvorbis.
  find_package(Threads REQUIRED)
  target_link_libraries(tibia PRIVATE Threads::Threads)
  target_compile_definitions(tibia PRIVATE TIBIA_WITH_GAMECORE)
  # Ensure Phase-5 session-dump / sound protocol units are always on the link
  # line (some cmake generators dropped them from an earlier configure).
  target_sources(tibia PRIVATE
    game/sessiondumpgamesession.cpp game/sessiondumpgamesession.h
    game/sessiondumpdmpd.cpp game/sessiondumpdmpd.h
    tutorial/sessiondumphintsprocessor.cpp tutorial/sessiondumphintsprocessor.h
    sound/soundprotocolmessagehandler.cpp sound/soundprotocolmessagehandler.h)
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT TIBIA_WITH_GAMECORE)
  target_compile_definitions(tibia PRIVATE TIBIA_SOURCE_DATA_DIR="${TIBIA_ROOT}/data")
endif()

if(TIBIA_QT_STATIC)
  # Static Qt (wasm): plugins are not loaded from disk. qt_import_qml_plugins on
  # the executable only sees tibia's own QML (none) — CIP UI lives in the
  # qmlcomponents static plugin — so link every Qt module CIP/mod import.
  set(_tibia_static_qml_plugins
    Qt6::qtquick2plugin
    Qt6::qmlplugin
    Qt6::modelsplugin
    Qt6::workerscriptplugin
    Qt6::qquicklayoutsplugin
    Qt6::quickwindow
    Qt6::qtquickcontrols2plugin
    Qt6::qtquickcontrols2implplugin
    Qt6::qtquicktemplates2plugin
    Qt6::qtquickcontrols2basicstyleplugin
    Qt6::qtquickcontrols2basicstyleimplplugin
    Qt6::qtquickcontrols2universalstyleplugin
    Qt6::qtquickcontrols2universalstyleimplplugin
    Qt6::qtgraphicaleffectsplugin
    Qt6::qtgraphicaleffectsprivate
    Qt6::effectsplugin
    Qt6::qtquickdialogsplugin
    Qt6::qtquickdialogs2quickimplplugin)
  foreach(_plugin IN LISTS _tibia_static_qml_plugins)
    if(TARGET ${_plugin})
      target_link_libraries(tibia PRIVATE ${_plugin})
    else()
      message(WARNING "tibia static: missing QML plugin target ${_plugin}")
    endif()
  endforeach()
  qt_import_qml_plugins(tibia)
  target_compile_definitions(tibia PRIVATE TIBIA_QT_STATIC)
endif()

# Options defaults, client strings and shaders.
file(GLOB cip_shaders CONFIGURE_DEPENDS "${CIP_ROOT}/client/upstream/shaders/*.qsb")
qt_add_resources(tibia cip_data PREFIX / BASE "${CIP_ROOT}/client/upstream" FILES
  "${CIP_ROOT}/client/upstream/conf/clientoptions.json"
  "${CIP_ROOT}/client/upstream/strings/client.en.qm"
  ${cip_shaders})

# Classic light-map multiply overlay (stock CIP lighting path).
if(TIBIA_WITH_GAMECORE)
  set(_stock_shader_glsl "300 es")
  if(NOT EMSCRIPTEN)
    set(_stock_shader_glsl "100 es,300 es,330,440")
  endif()
  set(_stock_shader_base "${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders")
  qt_add_shaders(tibia tibia_lightmap_shaders
    PREFIX /tibia/shaders
    BASE "${_stock_shader_base}"
    GLSL "${_stock_shader_glsl}"
    FILES
      "${_stock_shader_base}/lightmap_overlay.frag"
      "${_stock_shader_base}/lightmap_overlay.vert")
endif()

# CIP's default hotkey presets and the spell table from `make extract`.
qt_add_resources(tibia tibia_hotkeys PREFIX / BASE "${CMAKE_CURRENT_SOURCE_DIR}" FILES config/defaulthotkeys.json)
set(tibia_spells_json "${TIBIA_ROOT}/../../packages/asset-pipeline/data/tibia-client/game-tables/spells.json")
set(tibia_spells_previews_json "${TIBIA_ROOT}/../../packages/asset-pipeline/data/tibia-client/game-tables/spells-previews.json")
if(EXISTS "${tibia_spells_json}")
  set_source_files_properties("${tibia_spells_json}" PROPERTIES QT_RESOURCE_ALIAS spells.json)
  set(tibia_spells_files "${tibia_spells_json}")
  if(EXISTS "${tibia_spells_previews_json}")
    set_source_files_properties("${tibia_spells_previews_json}" PROPERTIES QT_RESOURCE_ALIAS spells-previews.json)
    list(APPEND tibia_spells_files "${tibia_spells_previews_json}")
  endif()
  qt_add_resources(tibia tibia_spells PREFIX /spells FILES ${tibia_spells_files})
endif()


if(EMSCRIPTEN)
  # CipSoft launcher stage in the same Module as the game (no /play remount).
  if(NOT TARGET Qt6::Widgets)
    message(FATAL_ERROR "tibia wasm CipSoft launcher stage needs Qt6::Widgets")
  endif()
  set(_tibia_launcher_dir "${TIBIA_ROOT}/launcher/src")
  set(_launcher_chrome "${TIBIA_ROOT}/launcher/upstream")
  set(_launcher_overrides "${TIBIA_ROOT}/launcher/overrides")
  # First launcher pack with images/launcher wins (not hard-coded to mod).
  set(_launcher_mod_art "")
  file(GLOB _launcher_mod_dirs LIST_DIRECTORIES true "${TIBIA_ROOT}/launcher/mods/*")
  foreach(_d IN LISTS _launcher_mod_dirs)
    if(IS_DIRECTORY "${_d}" AND EXISTS "${_d}/images/launcher")
      set(_launcher_mod_art "${_d}")
      break()
    endif()
  endforeach()
  target_sources(tibia PRIVATE
    "${_tibia_launcher_dir}/brand.cpp" "${_tibia_launcher_dir}/brand.h"
    "${_tibia_launcher_dir}/stage.cpp" "${_tibia_launcher_dir}/stage.h"
    "${_tibia_launcher_dir}/package.cpp" "${_tibia_launcher_dir}/package.h"
    "${_tibia_launcher_dir}/unpack.cpp" "${_tibia_launcher_dir}/unpack.h"
    "${_tibia_launcher_dir}/window.cpp" "${_tibia_launcher_dir}/window.h"
    "${_tibia_launcher_dir}/lzma/LzmaDec.c")
  target_link_libraries(tibia PRIVATE qtclient_shared)
  target_include_directories(tibia PRIVATE "${_tibia_launcher_dir}")
  # CipSoft chrome QRC (same assets as the standalone launcher binary).
  file(GLOB_RECURSE _embed_chrome CONFIGURE_DEPENDS "${_launcher_chrome}/*")
  set(_embed_launcher_resources)
  set(_product_art_names "BackgroundImage.jpg;TibiaLogo_Classic.png;CipSoftLogo.png")
  foreach(_file IN LISTS _embed_chrome)
    file(RELATIVE_PATH _rel "${_launcher_chrome}" "${_file}")
    get_filename_component(_name "${_file}" NAME)
    set(_src "${_file}")
    if(_name IN_LIST _product_art_names)
      set(_override "${_launcher_overrides}/images/launcher/${_name}")
      if(NOT EXISTS "${_override}" AND NOT _launcher_mod_art STREQUAL "")
        set(_override "${_launcher_mod_art}/images/launcher/${_name}")
      endif()
      if(EXISTS "${_override}")
        set(_src "${_override}")
      endif()
    endif()
    set_source_files_properties("${_src}" PROPERTIES QT_RESOURCE_ALIAS "${_rel}")
    list(APPEND _embed_launcher_resources "${_src}")
  endforeach()
  foreach(_name IN ITEMS BackgroundImage.jpg TibiaLogo_Classic.png CipSoftLogo.png)
    set(_override "${_launcher_overrides}/images/launcher/${_name}")
    if(NOT EXISTS "${_override}" AND NOT _launcher_mod_art STREQUAL "")
      set(_override "${_launcher_mod_art}/images/launcher/${_name}")
    endif()
    if(EXISTS "${_override}")
      set_source_files_properties("${_override}" PROPERTIES QT_RESOURCE_ALIAS "images/launcher/${_name}")
      list(APPEND _embed_launcher_resources "${_override}")
    endif()
  endforeach()
  set(_hints_json "${TIBIA_ROOT}/data/hints.json")
  if(EXISTS "${_hints_json}")
    set_source_files_properties("${_hints_json}" PROPERTIES QT_RESOURCE_ALIAS "hints.json")
    list(APPEND _embed_launcher_resources "${_hints_json}")
  endif()
  if(_embed_launcher_resources)
    qt_add_resources(tibia tibia_launcher_assets PREFIX "/" FILES ${_embed_launcher_resources})
  endif()
  target_include_directories(tibia PRIVATE
    "${_tibia_launcher_dir}/lzma"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_tibia_launcher_dir}")
  target_compile_definitions(tibia PRIVATE
    TIBIA_WASM_EMBED_LAUNCHER=1
    "TIBIA_LAUNCHER_VERSION=\"embedded\"")
  target_link_libraries(tibia PRIVATE Qt6::Widgets)
  target_link_options(tibia PRIVATE "SHELL:-sFETCH=1")
  if(TIBIA_WASM_MAIN_MODULE)
    # Relocatable host so ModLoader can dlopen pack native/*.wasm (SIDE_MODULE).
    # MAIN_MODULE=2 links on normal CI RAM; keepalive exports (host_exports.json)
    # stop DCE of symbols IdleOT SIDE_MODULE imports. Do not use MAIN_MODULE=1
    # (OOM) or EXPORT_ALL (wasm-ld cmdline blowup). -fPIC is project-wide.
    set(_tibia_host_exports "${TIBIA_ROOT}/cmake/host_exports.json")
    if(NOT EXISTS "${_tibia_host_exports}")
      message(FATAL_ERROR "tibia wasm: missing ${_tibia_host_exports} (run scripts/lifecycle/lib/gen-host-exports.sh)")
    endif()
    target_link_options(tibia PRIVATE
      "SHELL:-sMAIN_MODULE=2"
      "SHELL:-sERROR_ON_UNDEFINED_SYMBOLS=0"
      "SHELL:-sEXPORTED_FUNCTIONS=@${_tibia_host_exports}")
    message(STATUS "tibia wasm: MAIN_MODULE=2 + host_exports.json (SIDE_MODULE keepalive)")
  endif()

  # Dev shell: the art pack is preloaded; release builds get it from the CDN sync.
  set(TIBIA_WASM_GRAPHICS_RCC "" CACHE FILEPATH "graphics_resources.rcc to preload into the wasm build")
  if(TIBIA_WASM_GRAPHICS_RCC)
    target_link_options(tibia PRIVATE "SHELL:--preload-file ${TIBIA_WASM_GRAPHICS_RCC}@/data/graphics_resources.rcc")
  endif()
  # Workdir via data/setup.otml (same files as desktop
  # TIBIA_RUNTIME_FILES). Must live in MEMFS at `/` — /play has no disk tree.
  foreach(_rel IN ITEMS
      "data/setup.otml"
      "data/images/game/console/typing.png"
      "config.ini")
    if(EXISTS "${TIBIA_ROOT}/${_rel}")
      target_link_options(tibia PRIVATE "SHELL:--preload-file ${TIBIA_ROOT}/${_rel}@/${_rel}")
    else()
      message(WARNING "tibia wasm: missing runtime file ${TIBIA_ROOT}/${_rel}")
    endif()
  endforeach()
  # Net uses Emscripten's websocket.js (Crystal WS :7174).
  # Qt wasm_multithread has QT_FEATURE_wasm_exceptions off — match SJLJ ABI
  # (wasm32-emscripten triplet: SUPPORT_LONGJMP=emscripten).
  if(TIBIA_WITH_GAMECORE)
    # Compile with the same Emscripten EH as the link (-fexceptions, SJLJ).
    # Without it, throw does not unwind into catch and aborts the page.
    target_compile_options(tibia PRIVATE "SHELL:-fexceptions")
    target_link_options(tibia PRIVATE
      "SHELL:-lwebsocket.js"
      "SHELL:-fexceptions"
      "SHELL:-sSUPPORT_LONGJMP=emscripten")
  endif()
  # Qt default 256MB is tight once CipSoft launcher + art are linked.
  # ENV/FS: CipSoft AppData under /home/web_user (portal mounts OPFS; no IDBFS).
  # FETCH for tip downloads.
  set_target_properties(tibia PROPERTIES
    QT_WASM_INITIAL_MEMORY "512MB"
    QT_WASM_EXTRA_EXPORTED_METHODS "ENV,FS")
endif()

# Mobile / HiDPI scaffold (Phase 6): same QML binary; touch + orientation drive
# THudLayout density. Full store packaging is follow-up polish.
if(ANDROID)
  set_target_properties(tibia PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${TIBIA_ROOT}/android/tibia"
    QT_ANDROID_APP_NAME "Tibia"
    QT_ANDROID_VERSION_NAME "0.1.0"
    QT_ANDROID_VERSION_CODE 1)
  target_compile_definitions(tibia PRIVATE TIBIA_MOBILE=1)
endif()
if(IOS)
  set_target_properties(tibia PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.tibia.client
    MACOSX_BUNDLE_BUNDLE_NAME Tibia
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "16.0")
  target_compile_definitions(tibia PRIVATE TIBIA_MOBILE=1)
endif()

# Fat host import ABI for runtime packs (desktop MODULE + wasm SIDE_MODULE).
qtclient_host_finalize()
