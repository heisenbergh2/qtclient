#include "app/bootstrap.h"
#include "mods/modloader.h"

#include <exception>
#include "app/clientoptions.h"
#include "app/crashlog.h"
#include "app/launcherenv.h"
#include "app/portalurls.h"
#include "app/sessionbootstrap.h"
#ifdef TIBIA_WITH_GAMECORE
#include "app/qtplatformwindow.h"
#include "client/gameclient.h"
#include "client/gamecore.h"
#include <client/const.h>

#include <tibia/world/game.h>
#include "game/gameservergamesession.h"
#include "game/sessiondumpgamesession.h"
#include "gamewindow/actionbarmanager.h"
#include "gamewindow/cooldownbarcontroller.h"
#include "gamewindow/buttonbarcontroller.h"
#include "config/hotkeyoptions.h"
#include "gamewindow/chatcontroller.h"
#include "gamewindow/containerstoragecontroller.h"
#include "gamewindow/contextmenucontroller.h"
#include "gamewindow/dialogmanager.h"
#include "gamewindow/draganddropcontroller.h"
#include "gamewindow/cyclopediadialogcontroller.h"
#include "cyclopedia/iteminfostorage.h"
#include "gamewindow/informationdialogs.h"
#include "gamewindow/remainingdialogs.h"
#include "gamewindow/newsdialogcontroller.h"
#include "gamewindow/killtrackerwidgetcontroller.h"
#include "gamewindow/loottrackingwidgetcontroller.h"
#include "gamewindow/rewardwalldialogcontroller.h"
#include "gamewindow/taskboarddialogcontroller.h"
#include "gamewindow/spelllistwidgetcontroller.h"
#include "gamewindow/unjustifiedpointswidgetcontroller.h"
#include "gamewindow/imbuementtrackerwidgetcontroller.h"
#include "gamewindow/creaturetrackerwidgetcontroller.h"
#include "gamewindow/bosstrackerwidgetcontroller.h"
#include "gamewindow/depotsearchwidgetcontroller.h"
#include "gamewindow/playerguidewidgetcontroller.h"
#include "gamewindow/cipsoftuieventbridge.h"
#include "gamewindow/questlogcontroller.h"
#include "cyclopedia/creaturetrackerstorage.h"
#include "bossbestiary/bosstrackerstorage.h"
#include "analysers/analyserstorages.h"
#include "analysers/analyserwidgets.h"
#include "proficiency/proficiencystorage.h"
#include "proficiency/weaponproficiencydialogcontroller.h"
#include "proficiency/weaponproficiencyshapedialogs.h"
#include "gamewindow/preydialogcontroller.h"
#include "gamewindow/mapwindowcontroller.h"
#include "gamewindow/marketcontroller.h"
#include "gamewindow/imbuingdialogcontroller.h"
#include "gamewindow/exaltationforgedialogcontroller.h"
#include "skillwheel/skillwheelmanager.h"
#include "gamewindow/minimapcontroller.h"
#include "gamewindow/mousecursorcontroller.h"
#include "gamewindow/npcdialogcontroller.h"
#include "gamewindow/fpslatencyindicatorcontroller.h"
#include "gamewindow/optionsmenudialogcontroller.h"
#include "gamewindow/blessingsdialogcontroller.h"
#include "gamewindow/tutorialhintdialogcontroller.h"
#include "gamewindow/battlelistcontroller.h"
#include "gamewindow/playerinventoryandstatuscontroller.h"
#include "gamewindow/playertradecontroller.h"
#include "gamewindow/premiumpanelcontroller.h"
#include "gamewindow/selectobjectamountdialogcontroller.h"
#include "gamewindow/sidebarpanelsmanager.h"
#include "gamewindow/sidebarwidgetsmanager.h"
#include "gamewindow/stashdialogcontroller.h"
#include "gamewindow/managelootcontainerdialogcontroller.h"
#include "quickloot/managedcontainerstorage.h"
#include "gamewindow/statusbarcontroller.h"
#include "gamewindow/storeandresourcebalancehelpercontroller.h"
#include "gamewindow/storedialogcontroller.h"
#include "gamewindow/vipwidgetcontroller.h"
#include "gamewindow/bugreportdialogcontroller.h"
#include "gamewindow/deathdialogcontroller.h"
#include "gamewindow/messagedialogcontroller.h"
#include "gamewindow/naggingmessagedialogcontroller.h"
#include "gamewindow/reportviolationcontroller.h"
#include "exivaoptions/exivaoptionsdialogcontroller.h"
#include "mods/registry.h"
#include "mods/modloader.h"
#include "store/dynamicallyloadedimage.h"
#include "input/domainhandlers.h"
#include "input/eventfilters.h"
#include "input/gameactionhandlers.h"
#include "input/keyboardshortcuthandler.h"
#include "input/playermovementgameactionhandler.h"
#include "input/playermovementinputinterpreter.h"
#include "minimap/minimapmanager.h"
#include "qmlcomponents/soundhelper.h"
#include "renderer/worldview.h"
#endif
#include "controllers/characterselectioncontroller.h"
#include "gamewindow/gamewindowcontroller.h"
#include "gamewindow/sidebarmanager.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QEventLoop>
#include <QSaveFile>
#if defined(TIBIA_WASM_EMBED_LAUNCHER)
#include <QApplication>
// launcher/src is on the include path (TIBIA_WASM_EMBED_LAUNCHER).
#include "brand.h"
#include "stage.h"
#include "window.h"
#else
#include <QGuiApplication>
#endif
#include <QStandardPaths>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickGraphicsConfiguration>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUrlQuery>
#include <QtQml/qqmlextensionplugin.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#ifdef Q_OS_WASM
#include <emscripten/val.h>
#endif

Q_IMPORT_QML_PLUGIN(QtQuick_LegacyControlsPlugin)
Q_IMPORT_QML_PLUGIN(QtWebEnginePlugin)
Q_IMPORT_QML_PLUGIN(QtWebChannelPlugin)
#ifdef TIBIA_QT_STATIC
Q_IMPORT_QML_PLUGIN(QtQuickControls2UniversalStylePlugin)
#endif

using namespace tibia;

namespace {

/// --renderer must be known before QGuiApplication exists (the graphics API is
/// process-wide), so it is read straight from argv.
int rendererFromArgs(const int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strncmp(arg, "--renderer=", 11) == 0)
            return app::parseRendererIndex(QString::fromLocal8Bit(arg + 11), -1);
        if (std::strcmp(arg, "--renderer") == 0 && i + 1 < argc)
            return app::parseRendererIndex(QString::fromLocal8Bit(argv[i + 1]), -1);
    }
    return app::parseRendererIndex(qEnvironmentVariable("QTCLIENT_RENDERER"), -1);
}

QStringList commandLine()
{
    QStringList args = QCoreApplication::arguments();
#ifdef Q_OS_WASM
    // The page URL carries the options: tibia.html?preview&renderer=software
    // Portal-only keys (launcher → game handoff) are not client flags.
    const auto search = QString::fromStdString(emscripten::val::global("location")["search"].as<std::string>());
    const QUrlQuery query(search.mid(1));
    for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
        if (key == QLatin1String("game"))
            continue;
        args << (value.isEmpty() ? QStringLiteral("--") + key : QStringLiteral("--%1=%2").arg(key, value));
    }
#endif
    return args;
}

constexpr auto ClientWindowSection = "clientWindowOptions";
constexpr auto ClientWindowBorderlessKey = "clientWindowIsBorderless";

void applyClientWindowBorderless(QQuickWindow* window, const bool borderless)
{
    if (!window)
        return;
    const bool isFrameless = window->flags().testFlag(Qt::FramelessWindowHint);
    if (borderless == isFrameless)
        return;
    // CipSoft borderless = frameless windowed chrome (distinct from TOGGLE_FULLSCREEN).
    window->setFlag(Qt::FramelessWindowHint, borderless);
}

void applyClientWindowBorderlessFromOptions(QQuickWindow* window)
{
    if (!window)
        return;
    const QJsonObject opts = app::ClientOptions::load().root().value(QLatin1String(ClientWindowSection)).toObject();
    applyClientWindowBorderless(window, opts.value(QLatin1String(ClientWindowBorderlessKey)).toBool(false));
}

void restoreClientWindowGeometry(QQuickWindow* window)
{
    if (!window)
        return;
    const QJsonObject opts = app::ClientOptions::load().root().value(QLatin1String(ClientWindowSection)).toObject();
    const int width = opts.value(QLatin1String("clientWindowWidth")).toInt(0);
    const int height = opts.value(QLatin1String("clientWindowHeight")).toInt(0);
    const int x = opts.value(QLatin1String("clientWindowPositionX")).toInt(std::numeric_limits<int>::min());
    const int y = opts.value(QLatin1String("clientWindowPositionY")).toInt(std::numeric_limits<int>::min());
    const bool maximized = opts.value(QLatin1String("clientWindowIsMaximized")).toBool(false);
    applyClientWindowBorderless(window, opts.value(QLatin1String(ClientWindowBorderlessKey)).toBool(false));
    if (width > 0 && height > 0)
        window->resize(width, height);
    if (x != std::numeric_limits<int>::min() && y != std::numeric_limits<int>::min()) {
        if (QScreen* screen = window->screen()) {
            const QRect available = screen->availableGeometry();
            const int clampedX = std::clamp(x, available.left(), available.right() - 100);
            const int clampedY = std::clamp(y, available.top(), available.bottom() - 100);
            window->setPosition(clampedX, clampedY);
        } else {
            window->setPosition(x, y);
        }
    }
    if (maximized)
        window->showMaximized();
}

/// CIP quit-game prompt on window close (Exit / Logout / Cancel).
class QuitCloseFilter final : public QObject
{
public:
    QuitCloseFilter(gamewindow::TGameWindowController* controller, QObject* parent = nullptr)
        : QObject(parent)
        , m_controller(controller)
    {
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Close || !m_controller)
            return QObject::eventFilter(watched, event);
        // SIGINT/SIGTERM is handled by asking quitPressed(), same as this close.
        // Only an intentional relaunch (graphics-engine restart) skips the prompt.
        if (gamewindow::TGameWindowController::processExitAllowed())
            m_controller->setAllowWindowClose(true);
        if (m_controller->allowWindowClose())
            return QObject::eventFilter(watched, event);
        event->ignore();
        m_controller->quitPressed();
        return true;
    }

private:
    QPointer<gamewindow::TGameWindowController> m_controller;
};

void saveClientWindowGeometry(QQuickWindow* window)
{
    if (!window)
        return;
    const QRect geo = window->geometry();
    QJsonObject section = app::ClientOptions::load().root().value(QLatin1String(ClientWindowSection)).toObject();
    section.insert(QStringLiteral("clientWindowWidth"), geo.width());
    section.insert(QStringLiteral("clientWindowHeight"), geo.height());
    section.insert(QStringLiteral("clientWindowPositionX"), geo.x());
    section.insert(QStringLiteral("clientWindowPositionY"), geo.y());
    section.insert(QStringLiteral("clientWindowIsMaximized"),
                   static_cast<bool>(window->windowStates() & Qt::WindowMaximized));
    // Preserve clientWindowIsBorderless — geometry save must not wipe the option.
    if (QScreen* screen = window->screen())
        section.insert(QStringLiteral("clientWindowScreenName"), screen->name());
    QJsonObject changes;
    changes.insert(QString::fromLatin1(ClientWindowSection), section);
    app::ClientOptions::storeAll(changes);
}

QStringList dataDirs(const QString& explicitDir)
{
#ifndef TIBIA_CLIENT_PACKAGE_DIR
#define TIBIA_CLIENT_PACKAGE_DIR "Tibia.app"
#endif
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList dirs{
        explicitDir,
        qEnvironmentVariable("QTCLIENT_DATA_DIR"),
        QDir::current().filePath(QStringLiteral("data")),
        appDir + QStringLiteral("/../Resources/data"),
        appDir + QStringLiteral("/data"),
        QStringLiteral("/data"),
#ifdef TIBIA_SOURCE_DATA_DIR
        QStringLiteral(TIBIA_SOURCE_DATA_DIR),
#endif
    };
    // Tip package (portal OPFS / desktop AppData): RCC + fonts live under
    // Contents/Resources/assets, not ./data. Alias Soft Tibia.app ↔ IdleOT.app
    // is planted before callMain on /play.
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        const QString pkg = QDir(appData).filePath(
            QLatin1String("packages/") + QLatin1String(TIBIA_CLIENT_PACKAGE_DIR));
        dirs << pkg;
        dirs << QDir(pkg).filePath(QStringLiteral("Contents/Resources"));
        dirs << QDir(pkg).filePath(QStringLiteral("Contents/Resources/assets"));
    }
    return dirs;
}

/// Compiles every CIP QML file (and override) the app ships; the count of
/// files that fail is the exit code.
int checkQml(QQmlEngine& engine)
{
    int failed = 0;
    int total = 0;
    QDirIterator it(QStringLiteral(":/qt/qml"), { QStringLiteral("*.qml") }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QLatin1String(".override.qml")))
            continue;
        ++total;
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc") + path), QQmlComponent::PreferSynchronous);
        if (component.isError()) {
            ++failed;
            qWarning().noquote() << component.errorString().trimmed();
        }
    }
    qInfo("qml check: %d of %d files compile", total - failed, total);
    return failed;
}

QList<gamewindow::CharacterEntry> previewCharacters()
{
    const auto entry = [](const char* name, int level, const char* vocation, int outfit, bool main = false) {
        gamewindow::CharacterEntry c;
        c.name = QString::fromLatin1(name);
        c.level = level;
        c.vocation = QString::fromLatin1(vocation);
        c.world = QCoreApplication::applicationName();
        c.worldRules = QStringLiteral("Optional PvP");
        c.outfitId = outfit;
        c.headColor = QColor(0x9a, 0x7d, 0x5a);
        c.torsoColor = QColor(0x3e, 0x5e, 0x9a);
        c.legsColor = QColor(0x6a, 0x4a, 0x2a);
        c.detailColor = QColor(0x9a, 0x2a, 0x2a);
        c.isMainCharacter = main;
        return c;
    };
    return {
        entry("Aria Stormblade", 312, "Elite Knight", 131, true),
        entry("Brom Ironhide", 87, "Royal Paladin", 129),
        entry("Cyra Nightshade", 154, "Master Sorcerer", 130),
        entry("Dorn Mossbeard", 42, "Elder Druid", 144),
        entry("Eli Ashwalker", 8, "None", 128),
    };
}

}

int main(int argc, char** argv)
{
    tibia::app::installCrashLog();

    // Identity defaults before QApplication; packs may override after ModLoader.
    QCoreApplication::setApplicationName(QStringLiteral("Tibia"));
    QCoreApplication::setOrganizationName(QStringLiteral("Tibia"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tibia.local"));

    const auto options = app::ClientOptions::load();
    app::configureProcess(options, rendererFromArgs(argc, argv));

#if defined(TIBIA_WASM_EMBED_LAUNCHER)
    // CipSoft /play: QApplication so the qtclient launcher Widgets stage can run;
    // Play remounts into a game-only Module (QTCLIENT_WASM_SKIP_LAUNCHER).
    QApplication application(argc, argv);
#else
    QGuiApplication application(argc, argv);
#endif
    // Runtime packs: single tip pack after QApp. --no-mods → CipSoft; --default-mod → mods/default.
    bool skipMods = false;
    bool defaultMod = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--no-mods") == 0)
            skipMods = true;
        else if (qstrcmp(argv[i], "--default-mod") == 0)
            defaultMod = true;
    }
    if (skipMods) {
        tibia::mods::ModLoader::setSkipLoad(true);
        qInfo("--no-mods: stock CipSoft client (no pack)");
    } else {
        if (defaultMod)
            tibia::mods::ModLoader::setForceDefaultMod(true);
        tibia::mods::ModLoader::loadAll();
    }
    tibia::mods::ensureModsRegistered();
    if (const auto* id = tibia::mods::primaryIdentity(); id && !id->displayName.empty()) {
        application.setApplicationName(QString::fromStdString(id->displayName));
        application.setOrganizationName(QString::fromStdString(
            id->organization.empty() ? id->displayName : id->organization));
        application.setOrganizationDomain(QString::fromStdString(
            id->organizationDomain.empty() ? std::string("tibia.local") : id->organizationDomain));
    }
    application.setApplicationDisplayName(QCoreApplication::applicationName());

#if defined(TIBIA_WASM_EMBED_LAUNCHER)
    // CipSoft launcher UI first (skip with QTCLIENT_WASM_SKIP_LAUNCHER=1 after Play
    // remount, or for smoke tests). Must not nest QEventLoop::exec — WASM main
    // thread aborts without Asyncify, and Asyncify is incompatible with Qt
    // wasm_multithread.
    //
    // Do NOT build the QML/WebGL game UI while the launcher is up. Mobile Safari
    // kills the tab (reload loop / "A problem repeatedly occurred") when both
    // the Widgets soft-stage and a hidden QQuickWindow share one Module. Play
    // remounts into a game-only boot (portal sets QTCLIENT_WASM_SKIP_LAUNCHER).
    if (!qEnvironmentVariableIsSet("QTCLIENT_WASM_SKIP_LAUNCHER")) {
        LauncherBrand::load(argc, argv);
        QString cdn = qEnvironmentVariable("QTCLIENT_CDN_URL");
        if (cdn.startsWith(QLatin1Char('/'))) {
#ifdef Q_OS_WASM
            const emscripten::val loc = emscripten::val::global("location");
            cdn = QString::fromStdString(loc["origin"].as<std::string>()) + cdn;
#endif
        }
        if (cdn.isEmpty()) {
            const QString brandCdn = LauncherBrand::instance().defaultCdn;
            if (!brandCdn.isEmpty())
                cdn = brandCdn;
            else if (const auto* id = tibia::mods::primaryIdentity(); id && !id->cdnBaseUrl.empty())
                cdn = QString::fromStdString(id->cdnBaseUrl);
        }
        while (cdn.endsWith(QLatin1Char('/')))
            cdn.chop(1);
        LauncherWindow* embedLauncher = tibia::launcher::createEmbeddedLauncher(application, cdn);
        application.processEvents(QEventLoop::ExcludeUserInputEvents);
        QObject::connect(embedLauncher, &QObject::destroyed, &application, [] {
            QCoreApplication::exit(0);
        });
        QTimer::singleShot(0, embedLauncher, &LauncherWindow::start);
        return application.exec();
    }
#endif

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({ QStringLiteral("renderer"), QStringLiteral("auto, software, d3d11, vulkan, opengl, metal, d3d12 or a CIP renderer index."), QStringLiteral("renderer") });
    parser.addOption({ QStringLiteral("data"), QStringLiteral("Directory with graphics_resources.rcc (or things/<version>/)."), QStringLiteral("dir") });
    parser.addOption({ QStringLiteral("preview"), QStringLiteral("Show character selection with sample characters.") });
    parser.addOption({ QStringLiteral("screenshot"), QStringLiteral("Save the window to file once it has rendered, then quit."), QStringLiteral("file") });
    parser.addOption({ QStringLiteral("check-qml"), QStringLiteral("Compile every bundled QML file and exit (non-zero on errors).") });
    parser.addOption({ QStringLiteral("check-hotkeys"),
                       QStringLiteral("Round-trip THotkeyOptions load/switchSet/setMapping/save/reload and exit (non-zero on failure).") });
    parser.addOption({ QStringLiteral("account"), QStringLiteral("Account email for a login without the launcher (or QTCLIENT_ACCOUNT)."), QStringLiteral("email") });
    parser.addOption({ QStringLiteral("password"), QStringLiteral("Account password (or QTCLIENT_PASSWORD)."), QStringLiteral("password") });
    parser.addOption({ QStringLiteral("character"), QStringLiteral("Enter the game with this character without showing the list."), QStringLiteral("name") });
    parser.addOption({ QStringLiteral("play-for"), QStringLiteral("Log out and quit this many seconds after entering the world."), QStringLiteral("seconds") });
    parser.addOption({ QStringLiteral("say"), QStringLiteral("Say this a second after entering the world (smoke tests; repeatable)."), QStringLiteral("text") });
    parser.addOption({ QStringLiteral("record"), QStringLiteral("Record the game session to this file."), QStringLiteral("file") });
    parser.addOption({ QStringLiteral("replay"), QStringLiteral("Play a recorded game session instead of logging in."), QStringLiteral("file") });
    parser.addOption({ QStringLiteral("screenshot-delay"), QStringLiteral("Milliseconds after entering the world before --screenshot (default 3000)."), QStringLiteral("ms") });
    parser.addOption({ QStringLiteral("no-mods"), QStringLiteral("Do not load mods/<id> packs (stock qtclient).") });
    // Launcher settings may carry arguments of the Lua client; they are not fatal.
    if (!parser.parse(commandLine()))
        qWarning().noquote() << parser.errorText();
    if (parser.isSet(QStringLiteral("help")))
        parser.showHelp();

    const auto env = app::LauncherEnv::load(QDir::currentPath());
    if (!parser.isSet(QStringLiteral("preview")) && !parser.isSet(QStringLiteral("replay"))
        && !parser.isSet(QStringLiteral("check-qml")) && !parser.isSet(QStringLiteral("check-hotkeys"))
        && !parser.isSet(QStringLiteral("account"))
        && app::LauncherEnv::startLauncherIfUpdateNeeded())
        return 0;

    const QString graphics = app::mountGraphicsResources(dataDirs(parser.value(QStringLiteral("data"))));
    if (graphics.isEmpty())
        qWarning("graphics_resources.rcc not found; run make extract or pass --data");
    app::installTranslations();
    app::installUiFonts(dataDirs(parser.value(QStringLiteral("data"))));

    auto engine = app::createEngine();
    // Neutral placeholders; packs replace via ModContext / suite wire.
    engine->rootContext()->setContextProperty(QStringLiteral("modOptions"), QVariant::fromValue<QObject*>(nullptr));
    engine->rootContext()->setContextProperty(QStringLiteral("modHudLayout"), QVariant::fromValue<QObject*>(nullptr));
    engine->rootContext()->setContextProperty(QStringLiteral("modSideRail"), QVariant::fromValue<QObject*>(nullptr));
    if (parser.isSet(QStringLiteral("check-qml")))
        return checkQml(*engine) == 0 ? 0 : 1;
#ifdef TIBIA_WITH_GAMECORE
    if (parser.isSet(QStringLiteral("check-hotkeys"))) {
        const QString error = tibia::config::THotkeyOptions::selfCheckRoundTrip();
        if (!error.isEmpty()) {
            qCritical().noquote() << "check-hotkeys:" << error;
            return 1;
        }
        qInfo("check-hotkeys: ok (clientoptions.json hotkeyOptions)");
        return 0;
    }
#endif

    QQmlComponent windowComponent(engine.get(), QUrl(QStringLiteral("qrc:/qt/qml/qmlcomponents/qml/clientwindow.qml")));
    std::unique_ptr<QObject> windowObject(windowComponent.create());
    auto* window = qobject_cast<QQuickWindow*>(windowObject.get());
    if (!window) {
        qCritical().noquote() << windowComponent.errorString();
        return 1;
    }
    // QRhi::Timestamps for lastCompletedGpuTime (raycast pass GPU). Must be set before the
    // scene graph creates the RHI. weather.perf also uses rhi->finish() stalls when debug.
    if (!qEnvironmentVariableIsEmpty("QTCLIENT_PERF") || !qEnvironmentVariableIsEmpty("QTCLIENT_GPU_PERF")) {
        QQuickGraphicsConfiguration config = window->graphicsConfiguration();
        config.setTimestamps(true);
        window->setGraphicsConfiguration(config);
    }
#ifdef Q_OS_WASM
    // clientwindow.qml pins width/height to CIP minimum onCompleted. On WASM the
    // QScreen is the host element — keep the window filling it so the world is
    // not left-aligned with a black gutter.
    const auto fillHost = [window] {
        if (QScreen* screen = window->screen())
            window->resize(screen->size());
    };
    fillHost();
    if (QScreen* screen = window->screen())
        QObject::connect(screen, &QScreen::geometryChanged, window, fillHost);
#else
    restoreClientWindowGeometry(window);
    QObject::connect(&application, &QCoreApplication::aboutToQuit, window, [window] { saveClientWindowGeometry(window); });
    QObject::connect(app::ClientOptionsNotifier::instance(), &app::ClientOptionsNotifier::optionChanged, window,
                     [window](const QString& name) {
                         if (name == QLatin1String(ClientWindowSection)
                             || name == QLatin1String(ClientWindowBorderlessKey))
                             applyClientWindowBorderlessFromOptions(window);
                     });
#endif
#ifdef TIBIA_WITH_GAMECORE
    app::qtWindow().attach(window);
#endif
    qInfo().noquote() << "renderer:" << window->rendererInterface()->graphicsApi() << "graphics:" << graphics;
    app::alignTextHinting(engine.get());

    QQuickItem* placeholder = window->findChild<QQuickItem*>(QStringLiteral("placeholder"));
    placeholder->setProperty("windowTitle", QCoreApplication::applicationName());

    if (env.isRelease() && !env.ssoHosted()) {
        qCritical().noquote() << QStringLiteral("Start %1 from the %1 launcher. It installs and updates the game.").arg(QCoreApplication::applicationName());
        return 1;
    }

    const bool preview = parser.isSet(QStringLiteral("preview"));
    gamewindow::TCharacterSelectionController previewController;
    gamewindow::TCharacterSelectionController* controller = &previewController;
    QObject::connect(&previewController, &gamewindow::TCharacterSelectionController::characterChosen, [](const gamewindow::CharacterEntry& c) {
        qInfo().noquote() << "character selected:" << c.name << c.world;
    });
    QObject::connect(&previewController, &gamewindow::TCharacterSelectionController::cancelled, &application, &QCoreApplication::quit);
    if (preview)
        previewController.setAccount(QStringLiteral("Premium Account (365 days left)"), true, previewCharacters(), 0);

#ifdef TIBIA_WITH_GAMECORE
    client::TGameClient gameClient;
    gameClient.setWindow(window);
    if (!preview)
        controller = gameClient.characterSelection();

    // CIP ShowClientOutOfDate: caption "info", body client_too_old, single Restart
    // (YES/Return). Restart writes QTCLIENT_RESTART_FILE so the launcher updates.
    using OutOfDateBox = gamewindow::TMessageDialogController;
    OutOfDateBox clientOutOfDateDialog;
    clientOutOfDateDialog.setCloseOnButtonClicked(true);
    clientOutOfDateDialog.setButtons(
        { new gamewindow::TMessageDialogButton(qtTrId("restart"), OutOfDateBox::Button::YES,
                                               QKeySequence(QStringLiteral("Return"))) });
    QObject::connect(&gameClient, &client::TGameClient::clientOutOfDate, &clientOutOfDateDialog, [&] {
        if (!clientOutOfDateDialog.dialog()->isInStack())
            clientOutOfDateDialog.showMessage(qtTrId("info"), qtTrId("client_too_old"));
    });
    QObject::connect(&clientOutOfDateDialog, &OutOfDateBox::acceptClicked, &application, [] {
        const QString marker = qEnvironmentVariable("QTCLIENT_RESTART_FILE");
        if (!marker.isEmpty()) {
            QSaveFile file(marker);
            if (file.open(QIODevice::WriteOnly) && file.write("1") == 1)
                file.commit();
        }
        QCoreApplication::quit();
    });
#endif

    // CIP's game window: the start screen until the world is entered, then the
    // map, sidebars, chat and bars. Dialogs go into its dialogPlaceholder.
    gamewindow::TGameWindowController gameWindowController(placeholder);
#ifdef TIBIA_WITH_GAMECORE
    // kill / SIGTERM / Ctrl+C: same Exit / Logout / Cancel prompt as the window close.
    client::gamecore::setForceExitHandler([&gameWindowController] { gameWindowController.quitPressed(); });
    gamewindow::TStatusBarController statusBarController;
    gameWindowController.setStatusBarController(&statusBarController);
#endif
    auto* quitCloseFilter = new QuitCloseFilter(&gameWindowController, window);
    window->installEventFilter(quitCloseFilter);
    QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::quitGameExitRequested, window,
                     [window, &gameWindowController
#ifdef TIBIA_WITH_GAMECORE
                      ,
                      &gameClient
#endif
    ] {
#ifdef TIBIA_WITH_GAMECORE
                         // Close of the last window can exit() from AppKit before aboutToQuit.
                         gameClient.shutdown();
#endif
                         gameWindowController.setAllowWindowClose(true);
                         window->close();
                         QCoreApplication::quit();
                     });
#ifdef TIBIA_WITH_GAMECORE
    QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::quitGameLogoutRequested, &gameClient,
                     [&gameClient, &gameWindowController] {
                         if (game::TGameserverGameSession* session = gameClient.session())
                             session->requestLogout();
                         else if (game::TSessiondumpGameSession* dump = gameClient.sessiondump())
                             dump->requestLogout();
                         else
                             gameWindowController.showStartScreen();
                     });
#else
    QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::quitGameLogoutRequested, &application,
                     &QCoreApplication::quit);
#endif
    QQmlComponent gameWindowComponent(engine.get(), QUrl(QStringLiteral("qrc:/qt/qml/qmlcomponents/qml/gamewindow.qml")));
    std::unique_ptr<QObject> gameWindow(gameWindowComponent.create());
    auto* gameWindowItem = qobject_cast<QQuickItem*>(gameWindow.get());
    if (!gameWindowItem) {
        qCritical().noquote() << gameWindowComponent.errorString();
        return 1;
    }
    gameWindowItem->setParentItem(placeholder);
    gameWindowItem->setProperty("controller", QVariant::fromValue<QObject*>(&gameWindowController));
    gameWindowController.configureClientServices(env.clientServicesUrl(), env.revision);
    gameWindowController.showStartScreen();
    auto* dialogPlaceholder = gameWindowItem->findChild<QQuickItem*>(QStringLiteral("dialogPlaceholder"));
    auto* mapWindowPane = gameWindowItem->findChild<QQuickItem*>(QStringLiteral("mapWindowPane"));
    gamewindow::TSidebarManager sidebarManager(gameWindowItem->findChild<QQuickItem*>(QStringLiteral("rightSidebarPlaceholder")),
                                               gameWindowItem->findChild<QQuickItem*>(QStringLiteral("rightSidebarManager")),
                                               gameWindowItem->findChild<QQuickItem*>(QStringLiteral("leftSidebarPlaceholder")),
                                               gameWindowItem->findChild<QQuickItem*>(QStringLiteral("leftSidebarManager")));
    // CIP clientwindow.qml: minimum width grows with each sidebar so the 15×11
    // map is not crushed. The manager already emitted before this connection.
    const auto applySidebarCount = [window, &sidebarManager] {
        window->setProperty("numberOfSidebars", static_cast<int>(sidebarManager.classicSidebarIds().size()));
        if (QObject* split = window->findChild<QObject*>(QStringLiteral("mainFrame")))
            QTimer::singleShot(0, split, [split] { QMetaObject::invokeMethod(split, "correctLayout"); });
    };
    applySidebarCount();
    QObject::connect(&sidebarManager, &gamewindow::TSidebarManager::numberOfSidebarsChanged, window, applySidebarCount);

    QQmlComponent dialogComponent(engine.get(), QUrl(QStringLiteral("qrc:/qt/qml/qmlcomponents/qml/CharacterSelection.qml")));
    std::unique_ptr<QObject> dialog(dialogComponent.beginCreate(engine->rootContext()));
    auto* dialogItem = qobject_cast<QQuickItem*>(dialog.get());
    if (!dialogItem) {
        qCritical().noquote() << dialogComponent.errorString();
        return 1;
    }
    dialogComponent.setInitialProperties(dialogItem, { { QStringLiteral("controller"), QVariant::fromValue<QObject*>(controller) } });
    dialogItem->setParentItem(dialogPlaceholder);

    QObject productHost;
    {
        tibia::mods::ModContext earlyCtx;
        earlyCtx.engine = engine.get();
        earlyCtx.rootItem = window->contentItem();
        earlyCtx.gameClient = &gameClient;
        earlyCtx.host = &productHost;
        earlyCtx.launcherEnv = &env;
        earlyCtx.services["gameWindowController"] = &gameWindowController;
        tibia::mods::earlyBootstrapAll(earlyCtx);
    }
    dialogComponent.completeCreate();
    dialogItem->setVisible(controller == &previewController);

#ifdef TIBIA_WITH_GAMECORE
    client::TGameClient::Options clientOptions;
    if (!preview) {
        QObject::connect(&gameClient, &client::TGameClient::characterSelectionRequested, dialogItem, [dialogItem, &gameWindowController] {
            gameWindowController.setAuthenticated(true);
            dialogItem->setVisible(true);
            if (g_game.isOnline()) {
                if (auto* stack = gamewindow::TDialogStackController::instance())
                    stack->setCharacterListOpen(true);
            }
            // CIP initialFocusItem is the character table: Up/Down move the row, Return confirms.
            QTimer::singleShot(0, dialogItem, [dialogItem] {
                QObject* focus = dialogItem->property("initialFocusItem").value<QObject*>();
                if (auto* item = qobject_cast<QQuickItem*>(focus))
                    item->forceActiveFocus(Qt::PopupFocusReason);
                else
                    dialogItem->forceActiveFocus(Qt::PopupFocusReason);
            });
        });
        QObject::connect(&gameClient, &client::TGameClient::characterSelectionClosed, dialogItem, [dialogItem] {
            dialogItem->setVisible(false);
            if (auto* stack = gamewindow::TDialogStackController::instance())
                stack->setCharacterListOpen(false);
        });
        QObject::connect(&gameClient, &client::TGameClient::returnToLoginRequested, &gameWindowController, [&gameWindowController] {
            gameWindowController.setAuthenticated(false);
        });
        // Start screen with the account still "authenticated" hides the login mask.
        // If the character list is also closed (dropped switch, refused reconnect),
        // put one of those surfaces back. Deferred so a logout that opens the list
        // in the same turn is left alone.
        QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::gameWindowStateChanged, dialogItem,
                         [dialogItem, &gameClient, &gameWindowController] {
                             if (!gameWindowController.property("gameWindowState").toString().isEmpty())
                                 return;
                             QTimer::singleShot(0, dialogItem, [dialogItem, &gameClient, &gameWindowController] {
                                 if (g_game.isOnline() || g_game.isSwitchingCharacter() || dialogItem->isVisible())
                                     return;
                                 if (!gameWindowController.property("isAuthenticated").toBool())
                                     return;
                                 gameClient.ensureLoginSurface();
                             });
                         });
        QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::accountLoginRequested, &gameClient,
                         &client::TGameClient::submitCredentials);

        clientOptions.env = env;
        clientOptions.account = parser.value(QStringLiteral("account"));
        if (clientOptions.account.isEmpty())
            clientOptions.account = qEnvironmentVariable("QTCLIENT_ACCOUNT");
        clientOptions.password = parser.value(QStringLiteral("password"));
        if (clientOptions.password.isEmpty())
            clientOptions.password = qEnvironmentVariable("QTCLIENT_PASSWORD");
        clientOptions.autoCharacter = parser.value(QStringLiteral("character"));
        clientOptions.playForSeconds = parser.value(QStringLiteral("play-for")).toInt();
        clientOptions.say = parser.values(QStringLiteral("say"));
        clientOptions.recordTo = parser.value(QStringLiteral("record"));
        if (clientOptions.recordTo.isEmpty())
            clientOptions.recordTo = qEnvironmentVariable("QTCLIENT_RECORD");
        clientOptions.replay = parser.value(QStringLiteral("replay"));
        // CLI credentials / session replay: hide CIP's start-screen login mask.
        if (!clientOptions.account.isEmpty() || !clientOptions.replay.isEmpty())
            gameWindowController.setAuthenticated(true);
        // start() runs after the dialog stack + bootstrap so login errors can show.
    } else {
        gameWindowController.setAuthenticated(true);
    }
    const bool softwareRenderer = window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    auto* mapWindowController = new gamewindow::TMapWindowController(mapWindowPane, softwareRenderer, mapWindowPane);
    mapWindowController->setRenderProvider(gameClient.worldMapRenderProvider());
    mapWindowPane->setProperty("mapWindowController", QVariant::fromValue<QObject*>(mapWindowController));
    mapWindowController->syncFov();
    QObject::connect(mapWindowController, &gamewindow::TMapWindowController::unusedWidthChanged, &sidebarManager,
                     &gamewindow::TSidebarManager::onUnusedWidthChanged);
    mapWindowController->onUnusedWidthChanged();

    gamewindow::TContextMenuController contextMenu(engine.get(), window->contentItem());
    gamewindow::TDialogStackController dialogStack(engine.get(), dialogPlaceholder);
    // CIP session chrome (WaitDialog, errors, create account/character, keep_renderer).
    // Must run after the dialog stack so MessageDialog / WaitDialog can show.
    std::unique_ptr<app::TSessionBootstrap> sessionBootstrap;
    if (!preview) {
        sessionBootstrap = std::make_unique<app::TSessionBootstrap>(&gameClient, env, &gameWindowController, &productHost);
    }
    QObject::connect(&gameClient, &client::TGameClient::sessionEmailAvailable, &gameWindowController,
                     &gamewindow::TGameWindowController::rememberLoginEmailIfEnabled);
    if (!preview) {
        if (!gameClient.start(QCoreApplication::applicationFilePath(), QDir::currentPath(), clientOptions))
            return 1;
    }
    // CIP 0x100b63590 wires these controller signals to gamewindow.qml; TEventFilterManager
    // (0x100e87ae0 / 0x100e87b50) emits them while a dialog is up, so dialog inputs get the focus.
    QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::bindFocusToChatInput, gameWindowItem,
                     [gameWindowItem] { QMetaObject::invokeMethod(gameWindowItem, "bindFocusToChatInput"); });
    QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::releaseFocusFromChatInput, gameWindowItem,
                     [gameWindowItem] { QMetaObject::invokeMethod(gameWindowItem, "releaseFocusFromChatInput"); });
    QObject::connect(&dialogStack, &gamewindow::TDialogStackController::dialogVisible, &gameWindowController, [&gameWindowController](bool visible) {
        if (visible)
            Q_EMIT gameWindowController.releaseFocusFromChatInput();
        else
            Q_EMIT gameWindowController.bindFocusToChatInput();
    });
    QObject::connect(&dialogStack, &gamewindow::TDialogStackController::chatInputFocusChanged, &gameWindowController, [&gameWindowController](bool bound) {
        if (bound)
            Q_EMIT gameWindowController.bindFocusToChatInput();
        else
            Q_EMIT gameWindowController.releaseFocusFromChatInput();
    });
    gamewindow::TSelectObjectAmountDialogController splitStackDialog;
    gamewindow::TInformationDialogs informationDialogs;
    gamewindow::TDragAndDropController dragAndDrop(window->contentItem(),
                                                   gameWindowItem->findChild<QQuickItem*>(QStringLiteral("dragApperanceInstanceIcon")));
    engine->rootContext()->setContextProperty(QStringLiteral("tibiaDragAndDropController"), &dragAndDrop);
    store::TStoreStorage storeStorage;
    store::TStoreProtocolMessageHandler storeMessages;
    gamewindow::TStoreAndResourceBalanceHelperController storeBalance(&storeStorage);
    engine->rootContext()->setContextProperty(QStringLiteral("storeAndResourceBalanceHelper"), &storeBalance);
    gamewindow::TStoreDialogController storeDialog(&storeStorage, &storeMessages);
    gamewindow::TStoreButtonController storeButton(&storeDialog);
    market::TMarketStorage marketStorage;
    gamewindow::TMarketController marketController(&marketStorage);
    marketController.setOpenStore([&storeDialog] { storeDialog.open(); });
    gamewindow::TStashDialogController stashDialog;
    quickloot::TManagedContainerStorage managedContainers;
    gamewindow::TManageLootContainerDialogController manageLootDialog;
    imbuing::TImbuingStorage imbuingStorage;
    gamewindow::TImbuingDialogController imbuingDialog(&imbuingStorage);
    exaltation::TExaltationStorage exaltationStorage;
    gamewindow::TExaltationForgeDialogController exaltationForgeDialog(&exaltationStorage);
    storeBalance.setExaltationStorage(&exaltationStorage);
    skillwheel::TSkillWheelManager skillWheel;
    input::setActionHandler(input::EGameAction::OPEN_STORE, [&storeDialog](const input::TGameAction& action) {
        storeDialog.openWith(action.parameter, action.instance, action.text);
        return true;
    });
    // Reward-wall open/toggle is owned by TRewardWallDialogController (hooks +
    // REQUEST_OPEN_REWARD_WALL / OPEN_OR_UPDATE_REWARD_WALL_DIALOG).
    input::setActionHandler(input::EGameAction::REQUEST_DAILY_REWARD_HISTORY, [](const input::TGameAction&) {
        g_game.requestOpenRewardHistory();
        return true;
    });
    input::setActionHandler(input::EGameAction::COLLECT_FIXED_ITEMS_DAILY_REWARD, [](const input::TGameAction& action) {
        g_game.requestGetRewardDaily(static_cast<uint8_t>(std::max(0, action.parameter)), {});
        return true;
    });
    input::setActionHandler(input::EGameAction::COLLECT_PICK_ITEMS_DAILY_REWARD, [](const input::TGameAction& action) {
        g_game.requestGetRewardDaily(static_cast<uint8_t>(std::max(0, action.parameter)), {});
        return true;
    });
    input::setActionHandler(input::EGameAction::LENSHELP_ACTIVATE, [](const input::TGameAction&) {
        if (gamewindow::TMouseCursorController* cursor = gamewindow::TMouseCursorController::instance())
            cursor->setLenshelp(true);
        return true;
    });
    input::setActionHandler(input::EGameAction::LOAD_DEFAULT_MINIMAP_TILES_FROM_RESOURCES, [&gameClient](const input::TGameAction&) {
        if (minimap::TMinimapManager* minimap = gameClient.minimap())
            minimap->loadDefaultTilesFromResources();
        return true;
    });
    const auto automaticScreenshotAllowed = [](const QJsonObject& opts, const int eventType) -> bool {
        if (!opts.value(QLatin1String("screenshotsAutoScreenshotsEnabled")).toBool(true))
            return false;
        // Otc::ClientEvent_t (simple screenshot events) + product valuable-loot (52).
        const char* key = nullptr;
        switch (eventType) {
        case Otc::CLIENT_EVENT_BOSSDEFEATED: key = "screenshotsBossKillEnabled"; break;
        case Otc::CLIENT_EVENT_DEATHPVE: key = "screenshotsDeathPvEEnabled"; break;
        case Otc::CLIENT_EVENT_DEATHPVP: key = "screenshotsDeathPvPEnabled"; break;
        case Otc::CLIENT_EVENT_PLAYERKILLASSIST: key = "screenshotsPlayerKillAssistEnabled"; break;
        case Otc::CLIENT_EVENT_PLAYERKILL: key = "screenshotsPlayerKillFullEnabled"; break;
        case Otc::CLIENT_EVENT_PLAYERATTACKING: key = "screenshotsPvPAttackEnabled"; break;
        case Otc::CLIENT_EVENT_TREASUREFOUND: key = "screenshotsRewardChestEnabled"; break;
        case Otc::CLIENT_EVENT_GIFTOFLIFE: key = "screenshotsGiftOfLifeEnabled"; break;
        case 52: key = "screenshotsTrackedLootEnabled"; break; // valuable loot (CIP)
        // Synthetic codes for 15.21+ ClientEventType screenshot gates (200 + type).
        case 200 + Otc::CLIENT_EVENT_TYPE_ACHIEVEMENT: key = "screenshotsAchievementEnabled"; break;
        case 200 + Otc::CLIENT_EVENT_TYPE_LEVEL: key = "screenshotsLevelUpEnabled"; break;
        case 200 + Otc::CLIENT_EVENT_TYPE_SKILL: key = "screenshotsSkillUpEnabled"; break;
        case 200 + Otc::CLIENT_EVENT_TYPE_BESTIARY: key = "screenshotsBestiaryFullEnabled"; break;
        case 200 + Otc::CLIENT_EVENT_TYPE_BOSSTIARY: key = "screenshotsBossKillEnabled"; break;
        case 101: key = "screenshotsBestiaryPartlyEnabled"; break;
        case 102: key = "screenshotsLowHitPointsEnabled"; break;
        case 103: key = "screenshotsNewMaxDamageEnabled"; break;
        case 104: key = "screenshotsNewMaxHealingEnabled"; break;
        default:
            return true; // unknown event: keep master switch only
        }
        return opts.value(QLatin1String(key)).toBool(true);
    };
    const auto takeScreenshot = [window, mapWindowPane, automaticScreenshotAllowed](const input::TGameAction& action) {
        const bool automatic = action.type == input::EGameAction::TAKE_AUTOMATIC_SCREENSHOT;
        const QJsonObject opts = app::ClientOptions::load().options();
        if (automatic && !automaticScreenshotAllowed(opts, action.parameter))
            return true;
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/screenshots");
        QDir().mkpath(directory);
        const QString path = directory + QStringLiteral("/screenshot-%1.png")
                                           .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")));
        QImage image = window->grabWindow();
        if (automatic && opts.value(QLatin1String("screnshotsOnlyGameWindow")).toBool(false) && mapWindowPane) {
            const QRect crop = mapWindowPane->mapRectToScene(mapWindowPane->boundingRect()).toAlignedRect();
            if (crop.isValid() && image.rect().intersects(crop))
                image = image.copy(crop.intersected(image.rect()));
        }
        image.save(path);
        return true;
    };
    input::setActionHandler(input::EGameAction::TAKE_MANUAL_SCREENSHOT, takeScreenshot);
    input::setActionHandler(input::EGameAction::TAKE_AUTOMATIC_SCREENSHOT, takeScreenshot);
    input::setActionHandler(input::EGameAction::SHOW_BOSSTIARY_FREEPODIUM_DIALOG, [](const input::TGameAction&) {
        auto open = input::mutableClone(*input::makeAction(input::EGameAction::OPEN_CYCLOPEDIA));
        open->parameter = 6; // Bosstiary tab (CIP OPEN_CYCLOPEDIA)
        input::publish(open);
        return true;
    });
    input::setActionHandler(input::EGameAction::SHOW_OFFLINE_TRAINING_SKILLS, [](const input::TGameAction& action) {
        // CIP: parameter selects a skill immediately; otherwise open MultiOfflineTrainingDialog.
        if (action.parameter > 0) {
            g_game.sendStartOfflineTraining(static_cast<uint8_t>(action.parameter));
            return true;
        }
        if (auto* offline = gamewindow::TMultiOfflineTrainingDialogController::instance())
            offline->showSkillsDialog();
        return true;
    });
    qmlcomponents::installSoundActionHandler();

    gamewindow::TSidebarPanelsManager sidebarPanels(&sidebarManager, engine.get());
    gamewindow::TSidebarWidgetsManager sidebarWidgets(&sidebarManager, gameWindowItem, engine.get());
    const int controlScheme = options.options().value(QLatin1String("controlSchemeIndex")).toInt(0);
    const int lootScheme = options.options().value(QLatin1String("lootSchemeIndex")).toInt(1);
    gamewindow::TMinimapController* minimapController = sidebarPanels.minimap();
    if (minimapController)
        minimapController->setControlScheme(controlScheme);
    if (gamewindow::TPlayerInventoryAndStatusController* inventory = sidebarPanels.inventory()) {
        inventory->setControlScheme(controlScheme);
        inventory->setLootScheme(lootScheme);
    }
    gamewindow::TContainerStorageController containers(&sidebarManager, gameWindowItem, engine.get());
    containers.setControlScheme(controlScheme);
    containers.setLootScheme(lootScheme);
    sidebarWidgets.setContainerStorage(&containers);
    sidebarWidgets.setControlScheme(controlScheme);
    sidebarWidgets.battleLists()->setWorldMapRenderProvider(gameClient.worldMapRenderProvider());
    gamewindow::TVipWidgetManager vipWidget(gameWindowItem, engine.get());
    vipWidget.setControlScheme(controlScheme);
    informationDialogs.setControlScheme(controlScheme);
    exivaoptions::TExivaOptionsDialogController exivaOptionsDialog;
    exivaOptionsDialog.setStorage(vipWidget.exivaOptions(), [&gameClient] { return gameClient.worldType(); });
    gamewindow::TReportViolationController reportViolation;
    gamewindow::TBugReportDialogController bugReport;
    gamewindow::TNaggingMessageDialogController naggingDialog;
    gamewindow::TDeathDialogController deathDialog;
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::Vip, { [&vipWidget] { return vipWidget.open(); }, [&vipWidget] { vipWidget.close(); },
                                                                             [&vipWidget] { return vipWidget.current(); } });
    gamewindow::TKillTrackerWidgetManager preyWidget(informationDialogs.prey()->storage(), gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::Prey, { [&] { return preyWidget.open(gameClient.sidebarOptions()); },
                                                                              [&preyWidget] { preyWidget.close(); }, [&preyWidget] { return preyWidget.current(); } });
    gamewindow::TQuestTrackerWidgetManager questTrackerWidget(informationDialogs.questLog()->tracker(), gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::QuestTracker,
                                  { [&] { return questTrackerWidget.open(gameClient.sidebarOptions()); },
                                    [&questTrackerWidget] { questTrackerWidget.close(); }, [&questTrackerWidget] { return questTrackerWidget.current(); } });
    gamewindow::TLootTrackingWidgetManager lootTrackingWidget(cyclopedia::TItemInfoStorage::instance(), gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::LootTracker,
                                  { [&] { return lootTrackingWidget.open(gameClient.sidebarOptions()); }, [&lootTrackingWidget] { lootTrackingWidget.close(); },
                                    [&lootTrackingWidget] { return lootTrackingWidget.current(); } });
    gamewindow::TSpellListWidgetManager spellListWidget(gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::SpellList,
                                  { [&] { return spellListWidget.open(gameClient.sidebarOptions()); },
                                    [&spellListWidget] { spellListWidget.close(); },
                                    [&spellListWidget] { return spellListWidget.current(); } });
    gamewindow::TUnjustifiedPointsWidgetManager unjustifiedWidget(gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::UnjustifiedPoints,
                                  { [&] { return unjustifiedWidget.open(gameClient.sidebarOptions()); },
                                    [&unjustifiedWidget] { unjustifiedWidget.close(); },
                                    [&unjustifiedWidget] { return unjustifiedWidget.current(); } });
    gamewindow::TImbuementTrackerWidgetManager imbuementTrackerWidget(gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::ImbuementTracker,
                                  { [&] { return imbuementTrackerWidget.open(gameClient.sidebarOptions()); },
                                    [&imbuementTrackerWidget] { imbuementTrackerWidget.close(); },
                                    [&imbuementTrackerWidget] { return imbuementTrackerWidget.current(); } });
    cyclopedia::TCreatureTrackerStorage creatureTrackerStorage;
    bossbestiary::TBossTrackerStorage bossTrackerStorage;
    using CreatureMode = gamewindow::TCreatureTrackerWidgetController::EWidgetMode;
    gamewindow::TCreatureTrackerWidgetManager bestiaryTrackerWidget(CreatureMode::Bestiary, &creatureTrackerStorage, &bossTrackerStorage,
                                                                    gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::BestiaryTracker,
                                  { [&] { return bestiaryTrackerWidget.open(gameClient.sidebarOptions()); },
                                    [&bestiaryTrackerWidget] { bestiaryTrackerWidget.close(); },
                                    [&bestiaryTrackerWidget] { return bestiaryTrackerWidget.current(); } });
    gamewindow::TCreatureTrackerWidgetManager bosstiaryTrackerWidget(CreatureMode::Bosstiary, &creatureTrackerStorage, &bossTrackerStorage,
                                                                     gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::BosstiaryTracker,
                                  { [&] { return bosstiaryTrackerWidget.open(gameClient.sidebarOptions()); },
                                    [&bosstiaryTrackerWidget] { bosstiaryTrackerWidget.close(); },
                                    [&bosstiaryTrackerWidget] { return bosstiaryTrackerWidget.current(); } });
    gamewindow::TBossTrackerWidgetManager bossCooldownsWidget(&bossTrackerStorage, gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::BossCooldowns,
                                  { [&] { return bossCooldownsWidget.open(gameClient.sidebarOptions()); },
                                    [&bossCooldownsWidget] { bossCooldownsWidget.close(); },
                                    [&bossCooldownsWidget] { return bossCooldownsWidget.current(); } });
    // CipSoft DepotSearchWidget (CIP sidebar); owns onDepotSearch* hooks. Product dialog retired.
    gamewindow::TDepotSearchWidgetManager depotSearchWidget(&sidebarManager, gameWindowItem, engine.get());
    gamewindow::TPlayerGuideWidgetManager playerGuideWidget(gameWindowItem, engine.get());
    sidebarWidgets.registerWidget(gamewindow::TSidebarWidgetsManager::PlayerGuide,
                                  { [&] { return playerGuideWidget.open(gameClient.sidebarOptions()); },
                                    [&playerGuideWidget] { playerGuideWidget.close(); },
                                    [&playerGuideWidget] { return playerGuideWidget.current(); } });
    // CipSoft subscribers for gem atelier / helpers / RV channel / virtues yellow border.
    gamewindow::TCipSoftUiEventBridge cipSoftUiEventBridge;
    analysers::TAnalyserStorages analyserStorages;
    analysers::TAnalyserWidgets analyserWidgets(&analyserStorages, &sidebarWidgets, gameWindowItem, engine.get(),
                                                [&gameClient] { return gameClient.sidebarOptions(); });
    proficiency::TProficiencyStorage proficiencyStorage;
    proficiency::TWeaponProficiencyDialogController weaponProficiencyDialog(&proficiencyStorage);
    proficiency::TWeaponProficiencyShapeDialogController weaponProficiencyShapeDialog(&proficiencyStorage);
    proficiency::TWeaponProficiencyReshapeDialogController weaponProficiencyReshapeDialog(&proficiencyStorage,
                                                                                          &weaponProficiencyShapeDialog);
    proficiency::TWeaponProficiencyShapeOptionsDialogController weaponProficiencyShapeOptionsDialog;
    gamewindow::TBlessingsDialogController blessingsDialog;
    gamewindow::TRewardWallDialogController rewardWallDialog;
    gamewindow::TTaskboardDialogController taskboardDialog;
    gamewindow::TTutorialShell tutorialShell;
    QObject::connect(&tutorialShell, &gamewindow::TTutorialShell::tutorialFinished, &gameClient,
                     &client::TGameClient::finishTutorialCinematic);
    if (gamewindow::TButtonBarController* buttonBar = sidebarPanels.buttonBar()) {
        QObject::connect(&sidebarWidgets, &gamewindow::TSidebarWidgetsManager::widgetVisible, buttonBar,
                         &gamewindow::TButtonBarController::onWidgetVisible);
        QObject::connect(&sidebarWidgets, &gamewindow::TSidebarWidgetsManager::widgetHidden, buttonBar,
                         &gamewindow::TButtonBarController::onWidgetHidden);
        QObject::connect(buttonBar, &gamewindow::TButtonBarController::logoutRequested, &gameClient, [&gameClient] {
            if (game::TGameserverGameSession* session = gameClient.session())
                session->requestLogout();
            else if (game::TSessiondumpGameSession* dump = gameClient.sessiondump())
                dump->requestLogout();
        });
        buttonBar->setStoreButtonController(&storeButton);
        buttonBar->setNewsButtonController(gameWindowController.newsButton());
        buttonBar->setCyclopediaButtonController(informationDialogs.cyclopediaButton());
        buttonBar->setBosstiaryButtonController(informationDialogs.bosstiaryButton());
        if (QQuickItem* buttonBarItem = sidebarPanels.buttonBarItem()) {
            // storeButtonController is constant: rebinding panelController re-reads it.
            buttonBarItem->setProperty("panelController", QVariant::fromValue<QObject*>(nullptr));
            buttonBarItem->setProperty("panelController", QVariant::fromValue<QObject*>(buttonBar));
            QObject::connect(buttonBar, &gamewindow::TButtonBarController::buttonControllersChanged, buttonBarItem,
                             [buttonBarItem, buttonBar] {
                                 buttonBarItem->setProperty("panelController", QVariant::fromValue<QObject*>(nullptr));
                                 buttonBarItem->setProperty("panelController", QVariant::fromValue<QObject*>(buttonBar));
                             });
        }
    }
    QObject::connect(app::ClientOptionsNotifier::instance(), &app::ClientOptionsNotifier::optionChanged, gameWindowItem, [&](const QString& name) {
        const QJsonObject opts = app::ClientOptions::load().options();
        if (name == QLatin1String("controlSchemeIndex")) {
            const int scheme = opts.value(name).toInt(0);
            if (minimapController)
                minimapController->setControlScheme(scheme);
            if (gamewindow::TPlayerInventoryAndStatusController* inventory = sidebarPanels.inventory())
                inventory->setControlScheme(scheme);
            containers.setControlScheme(scheme);
            sidebarWidgets.setControlScheme(scheme);
            vipWidget.setControlScheme(scheme);
        } else if (name == QLatin1String("lootSchemeIndex")) {
            const int scheme = opts.value(name).toInt(1);
            if (gamewindow::TPlayerInventoryAndStatusController* inventory = sidebarPanels.inventory())
                inventory->setLootScheme(scheme);
            containers.setLootScheme(scheme);
        }
    });

    gamewindow::TOptionsMenuDialogController optionsDialog;
    optionsDialog.setWindow(window);
    optionsDialog.setGameClient(&gameClient);
    optionsDialog.setPortalBase(env.loginBase);
    input::TOpenWebsiteGameActionHandler openWebsiteActions(env);
    QObject::connect(&gameWindowController, &gamewindow::TGameWindowController::optionsMenuRequested, &optionsDialog,
                     &gamewindow::TOptionsMenuDialogController::open);
    input::setActionHandler(input::EGameAction::OPEN_OPTIONS_MENU_DIALOG, [&optionsDialog](const input::TGameAction& action) {
        optionsDialog.open(action.parameter);
        return true;
    });
    input::setActionHandler(input::EGameAction::QUIT_GAME, [&gameWindowController](const input::TGameAction&) {
        gameWindowController.quitPressed();
        return true;
    });
    input::setActionHandler(input::EGameAction::TOGGLE_FULLSCREEN, [window](const input::TGameAction&) {
        if (!window)
            return true;
        if (window->visibility() == QWindow::FullScreen)
            window->showNormal();
        else
            window->showFullScreen();
        return true;
    });
    gamewindow::TFPSLatencyIndicatorController fpsLatency;
    fpsLatency.setWindow(window);
    fpsLatency.setFrameDurations(gameClient.frameDurations());
    fpsLatency.setRenderProvider(gameClient.worldMapRenderProvider());
    if (QQuickItem* fpsItem = gameWindowItem->findChild<QQuickItem*>(QStringLiteral("tibiaFpsLatencyIndicator")))
        fpsItem->setProperty("controller", QVariant::fromValue<QObject*>(&fpsLatency));

    gamewindow::TChatController chat(gameWindowItem->findChild<QQuickItem*>(QStringLiteral("chat")), gameClient);
    gameClient.setMessageIgnoreList(&chat.ignoreList());
    if (cyclopedia::TItemInfoStorage* itemInfo = cyclopedia::TItemInfoStorage::instance())
        itemInfo->setChatStorage(&chat.storage());

    gamewindow::TActionBarManager actionBars(gameWindowItem, mapWindowPane, engine.get(), [&chat] { return chat.chatMode(); });
    QObject::connect(&chat, &gamewindow::TChatController::chatModeChanged, &actionBars, &gamewindow::TActionBarManager::onChatModeChanged);
    // After actionBars so TCooldownStorage::instance() exists (CIP TCooldownBarController).
    gamewindow::TCooldownBarController cooldownBar(gameWindowItem);
    input::TKeyboardShortcutHandler keyboardShortcuts(window, gameWindowItem->findChild<QQuickItem*>(QStringLiteral("chat")),
                                                      [&chat] { return chat.chatMode(); });
    input::TPlayerMovementInputInterpreter playerMovement(window, gameWindowItem->findChild<QQuickItem*>(QStringLiteral("chat")),
                                                          [&chat] { return chat.chatMode(); });
    input::TPlayerMovementGameActionHandler playerMovementActions;
    input::TClipboardGameActionHandler clipboardActions;
    input::TGenericGameActionHandler genericActions;
    input::TGatherAllGameActionHandler gatherAllActions;
    [[maybe_unused]] input::TGatherAllProtocolMessageHandler gatherAllProtocol;
    input::TWindowFocusEventFilter windowFocus(window);
    input::TDiscardMouseWheelEventFilter discardWheel;
    input::TDiscardUnhandledMouseButtonsEventFilter discardMouseButtons;
    window->installEventFilter(&discardWheel);
    window->installEventFilter(&discardMouseButtons);

    trade::TNPCTradeStorage npcTrade;
    gamewindow::TNpcDialogController npcDialog(&npcTrade);
    npcDialog.setChatController(&chat);
    gamewindow::TPlayerTradeWidgetManager playerTrade(&sidebarManager, gameWindowItem, engine.get());

    {
        tibia::mods::ModContext ctx;
        ctx.engine = engine.get();
        ctx.rootItem = window->contentItem();
        ctx.mapWindowPane = mapWindowPane;
        ctx.gameWindowItem = gameWindowItem;
        ctx.gameClient = &gameClient;
        ctx.dialogStack = &dialogStack;
        ctx.host = &productHost;
        ctx.launcherEnv = &env;
        ctx.services["optionsDialog"] = &optionsDialog;
        ctx.services["informationDialogs"] = &informationDialogs;
        ctx.services["questTracker"] = &questTrackerWidget;
        ctx.services["sidebarPanels"] = &sidebarPanels;
        ctx.services["sidebarManager"] = &sidebarManager;
        ctx.services["sidebarWidgets"] = &sidebarWidgets;
        ctx.services["containers"] = &containers;
        ctx.services["storeDialog"] = &storeDialog;
        ctx.services["blessingsDialog"] = &blessingsDialog;
        ctx.services["npcDialog"] = &npcDialog;
        ctx.services["chat"] = &chat;
        ctx.services["storeMessages"] = &storeMessages;
        ctx.services["imbuingDialog"] = &imbuingDialog;
        ctx.services["imbuingStorage"] = &imbuingStorage;
        ctx.services["gameWindowController"] = &gameWindowController;
        ctx.services["minimapController"] = minimapController;
        ctx.services["keyboardShortcuts"] = &keyboardShortcuts;
        ctx.services["playerMovement"] = &playerMovement;
        if (sessionBootstrap)
            ctx.services["sessionBootstrap"] = sessionBootstrap.get();
        tibia::mods::bootstrapAll(ctx);
    }

    QPointer<qmlcomponents::TWorldView> worldView;
    QObject::connect(&gameClient, &client::TGameClient::mainWindowInitialized, gameWindowItem, [&] {
        // Show the game window before the heavier binds. A throw later must not
        // leave the login mask hidden and the game window still off.
        gameWindowController.showGameScreen();
        try {
            if (!worldView) {
                worldView = new qmlcomponents::TWorldView(mapWindowPane, gameClient.worldMapRenderProvider(), gameClient.textStorage(),
                                                          gameClient.numericalEffects());
                if (!worldView->isBound())
                    qCritical("MapWindowPane has no WorldMap named worldmap");
            }
            vipWidget.setSidebarOptions(gameClient.sidebarOptions());
            vipWidget.setWorldType(gameClient.worldType());
            sidebarWidgets.setSidebarOptions(gameClient.sidebarOptions());
            depotSearchWidget.setSidebarOptions(gameClient.sidebarOptions());
            sidebarPanels.setSidebarOptions(gameClient.sidebarOptions());
            if (gamewindow::TPremiumPanelController* premiumPanel = sidebarPanels.premiumPanel())
                premiumPanel->onSessionStarted(gameClient.account().session.isPremium, gameClient.account().session.premiumUntil);
            skillWheel.setSessionPremium(gameClient.account().session.isPremium);
            containers.setSidebarOptions(gameClient.sidebarOptions());
            containers.onLoginRampUpFinished();
            sidebarWidgets.onLoginRampUpFinished();
            statusBarController.setStorage(gameClient.statusbarStorage());
            informationDialogs.setAmbientLightStorage(gameClient.ambientLight());
            informationDialogs.setMinimapTileStorage(gameClient.minimap() ? gameClient.minimap()->tileStorage() : nullptr);
            if (minimapController) {
                minimapController->setAmbientLightStorage(gameClient.ambientLight());
                minimapController->setTileStorage(gameClient.minimap() ? gameClient.minimap()->tileStorage() : nullptr);
            }
            optionsDialog.setInGame(true);
            actionBars.onWorldEntered();
            qmlcomponents::applyOptionVolumes(); // stop login anthem; keep channel gains
        } catch (const std::exception& ex) {
            qCritical("tibia.client: entering the world failed: %s", ex.what());
        } catch (...) {
            qCritical("tibia.client: entering the world failed");
        }
    });
    QObject::connect(&gameClient, &client::TGameClient::worldLeft, gameWindowItem, [&] {
        if (minimapController)
            minimapController->setTileStorage(nullptr);
        informationDialogs.setMinimapTileStorage(nullptr);
        sidebarPanels.setSidebarOptions(nullptr);
        sidebarWidgets.setSidebarOptions(nullptr);
        vipWidget.setSidebarOptions(nullptr);
        containers.setSidebarOptions(nullptr);
        statusBarController.setStorage(nullptr);
        optionsDialog.setInGame(false);
        actionBars.onWorldLeft();
        dialogStack.hideAll();
        gameWindowController.showStartScreen();
        qmlcomponents::applyOptionVolumes(); // resume login anthem when offline
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &gameClient, [&] {
        if (worldView)
            worldView->unbind();
        if (minimapController)
            minimapController->setTileStorage(nullptr);
        informationDialogs.setMinimapTileStorage(nullptr);
        gameClient.shutdown();
    });
#endif

    if (const QString shot = parser.value(QStringLiteral("screenshot")); !shot.isEmpty()) {
        const auto takeShot = [window, shot] {
            const bool saved = window->grabWindow().save(shot);
            qInfo().noquote() << (saved ? "screenshot:" : "screenshot failed:") << shot;
            QCoreApplication::exit(saved ? 0 : 1);
        };
#ifdef TIBIA_WITH_GAMECORE
        // Logging in or replaying: shoot the world once it has had time to
        // arrive, or half a second after the last --say (numbers last a second).
        if (!preview && (parser.isSet(QStringLiteral("character")) || parser.isSet(QStringLiteral("replay")))) {
            const qsizetype says = parser.values(QStringLiteral("say")).size();
            int delay = says > 0 ? int(says) * 1000 + 500 : 3000;
            if (parser.isSet(QStringLiteral("screenshot-delay")))
                delay = parser.value(QStringLiteral("screenshot-delay")).toInt();
            QObject::connect(&gameClient, &client::TGameClient::mainWindowInitialized, window, [window, takeShot, delay] {
                QTimer::singleShot(delay, window, takeShot);
            }, Qt::SingleShotConnection);
        } else
#endif
            QTimer::singleShot(2000, window, takeShot);
    }

    return application.exec();
}
