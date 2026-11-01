#include "gamecore.h"
#include "tibia/runtime/host.h"

#include <client/client.h>
#include <tibia/world/game.h>
#include <client/gameconfig.h>
#include <tibia/assets/loader.h>
#include <tibia/assets/things.h>
#include <framework/core/clock.h>
#include <framework/core/eventdispatcher.h>
#include <framework/core/gameevents.h>
#include <framework/core/resourcemanager.h>
#include <framework/graphics/texturemanager.h>
#include "network/gameserverconnection.h"
#include "sound/soundstorage.h"
#include <framework/stdext/thread.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>

#include <csignal>
#include <map>

#ifndef TIBIA_CLIENT_PACKAGE_DIR
#define TIBIA_CLIENT_PACKAGE_DIR "Tibia.app"
#endif

namespace tibia::client::gamecore {

namespace {

bool running = false;
bool forceExit = false;
int exitHook = 0;
volatile std::sig_atomic_t exitSignaled = 0;
std::function<void()> forceExitHandler;

void onExitSignal(int)
{
    exitSignaled = 1;
}

/// Runs on the GUI thread, after the event that noticed the signal has returned.
/// The handler shows Exit / Logout / Cancel. Quitting here would close the
/// window from the signal and skip that prompt. Doing this from afterAnimating
/// is inside QPlatformWindow::deliverUpdateRequest; closing the window there
/// frees it and the timer's hasPendingUpdateRequest() then segfaults.
void finishExit()
{
    if (forceExitHandler)
        forceExitHandler();
    else
        QCoreApplication::quit();
}

/// modules/game_features/features.lua: the qtclient features of a protocol
/// version, plus the Crystal OTCR features the server expects before 0x43.
void enableFeatures(const int version)
{
    using namespace Otc;
    const auto on = [](const GameFeature f) { g_game.enableFeature(f); };
    const auto off = [](const GameFeature f) { g_game.disableFeature(f); };

    on(GameFormatCreatureName);
    on(GameAllowPreWalk);
    on(GameMapCache);

    // disable has no `= {}`: MSVC rejects brace defaults on initializer_list (C2797).
    struct Step
    {
        int version;
        std::initializer_list<GameFeature> enable;
        std::initializer_list<GameFeature> disable;
    };
    static const Step steps[] = {
        { 750, { GameSoul }, {} },
        { 760, { GameLevelU16 }, {} },
        { 770, { GameLooktypeU16, GameMessageStatements, GameLoginPacketEncryption }, {} },
        { 780, { GamePlayerAddons, GamePlayerStamina, GameNewFluids, GameMessageLevel, GamePlayerStateU16, GameNewOutfitProtocol }, {} },
        { 790, { GameWritableDate }, {} },
        { 840, { GameProtocolChecksum, GameAccountNames, GameDoubleFreeCapacity }, {} },
        { 841, { GameChallengeOnLogin, GameMessageSizeCheck, GameTileAddThingWithStackpos }, {} },
        { 854, { GameCreatureEmblems }, {} },
        { 860, { GameAttackSeq }, {} },
        { 862, { GamePenalityOnDeath }, {} },
        { 870, { GameDoubleExperience, GamePlayerMounts, GameSpellList }, {} },
        { 910, { GameNameOnNpcTrade, GameTotalCapacity, GameSkillsBase, GamePlayerRegenerationTime, GameChannelPlayerList, GameEnvironmentEffect, GameItemAnimationPhase }, {} },
        { 940, { GamePlayerMarket }, {} },
        { 953, { GamePurseSlot, GameClientPing }, {} },
        { 960, { GameSpritesU32, GameOfflineTrainingTime }, {} },
        { 963, { GameAdditionalVipInfo }, {} },
        { 972, { GameDoublePlayerGoodsMoney }, {} },
        { 980, { GamePreviewState, GameClientVersion }, {} },
        { 981, { GameLoginPending, GameNewSpeedLaw }, {} },
        { 984, { GameContainerPagination, GameBrowseField }, {} },
        { 1000, { GameThingMarks, GamePVPMode }, {} },
        { 1035, { GameDoubleSkills, GameBaseSkillU16 }, {} },
        { 1036, { GameCreatureIcons, GameHideNpcNames }, {} },
        { 1038, { GamePremiumExpiration }, {} },
        { 1050, { GameEnhancedAnimations }, {} },
        { 1053, { GameUnjustifiedPoints }, {} },
        { 1054, { GameExperienceBonus }, {} },
        { 1055, { GameDeathType }, {} },
        { 1057, { GameIdleAnimations }, {} },
        { 1061, { GameOGLInformation }, {} },
        { 1071, { GameContentRevision }, {} },
        { 1072, { GameAuthenticator }, {} },
        { 1074, { GameSessionKey }, {} },
        { 1080, { GameIngameStore }, {} },
        { 1092, { GameIngameStoreServiceType }, {} },
        { 1093, { GameIngameStoreHighlights }, {} },
        { 1094, { GameAdditionalSkills, GameLeechAmount }, {} },
        { 1100, { GamePrey }, {} },
        { 1200, { GameColorizedLootValue, GameThingQuickLoot, GameTournamentPackets, GameVipGroups, GameEnterGameShowAppearance }, {} },
        { 1260, { GameThingQuiver }, {} },
        { 1264, { GameThingPodium }, {} },
        { 1272, { GameThingUpgradeClassification }, {} },
        { 1281, { GameForgeSkillStats, GamePlayerFamiliars }, { GameEnvironmentEffect, GameItemAnimationPhase } },
        { 1290, { GameSequencedPackets, GameBosstiary, GameThingClock, GameThingCounter, GameThingPodiumItemType, GameDoubleShopSellAmount }, {} },
        { 1300, { GameDoubleHealth, GameUshortSpell, GameConcotions, GameAnthem }, {} },
        { 1314, { GameDynamicForgeVariables }, { GameTournamentPackets } },
        { 1320, { GameEffectU16, GameContainerTypes, GameBosstiaryTracker, GamePlayerStateCounter, GameItemAugment, GameDynamicBugReporter }, { GameLeechAmount } },
        { 1321, { GameWrapKit, GameContainerFilter }, {} },
        { 1332, { GameForgeConvergence }, {} },
        { 1410, { GameCharacterSkillStats }, { GameAdditionalSkills, GameForgeSkillStats } },
        { 1500, { GameVocationMonk }, {} },
        { 1510, { GameProficiency }, {} },
        { 1513, { GameNpcWindowRedesign }, {} },
        { 1514, { GameEffectSource }, {} },
        { 1520, { GameLevelPercentU16, GameTaskboard }, {} },
        { 1525, { GameTacticsWithoutFightMode }, {} },
    };
    for (const auto& step : steps) {
        if (version < step.version)
            break;
        for (const auto f : step.enable)
            on(f);
        for (const auto f : step.disable)
            off(f);
    }

    on(GameItemShader);
    on(GameCreatureAttachedEffect);
    on(GameCreatureShader);
    on(GameWingsAurasEffectsShader);
}

int gameStartHook = 0;

}

bool init(const QString& argv0, const QString& workDir)
{
    if (running)
        return true;

    const QByteArray argv0Bytes = argv0.toLocal8Bit();
    std::vector<std::string> args{ argv0Bytes.toStdString() };
    tibia::files::data().init(argv0Bytes.constData());

    const QString previousDir = QDir::currentPath();
    QDir::setCurrent(workDir);
    const bool found = tibia::files::data().discoverWorkDir("data/setup.otml");
    QDir::setCurrent(previousDir);
    if (!found) {
        qCritical().noquote() << "engine: no data/setup.otml under" << workDir;
        return false;
    }
    // Virtual paths resolve under workDir. CIP assets use one Resources tree
    // (see assetsRoot). workDir "/" must become "/data", not "//data"
    // (wasm MEMFS will not open the latter).
    std::string dataRoot = tibia::files::data().getWorkDir();
    while (!dataRoot.empty() && (dataRoot.back() == '/' || dataRoot.back() == '\\'))
        dataRoot.pop_back();
    tibia::files::data().addSearchPath(dataRoot, true);
    tibia::files::data().addSearchPath(dataRoot + "/data", true);

    g_mainThreadId = g_eventThreadId = stdext::getThreadId();

    std::signal(SIGTERM, onExitSignal);
    std::signal(SIGINT, onExitSignal);
    g_client.init(args);

    tibia::files::data().setupUserWriteDir("tibia/");
    tibia::network::installGameServerConnection();

    // SIGINT/SIGTERM and engine-requested exits end the Qt event loop.
    exitHook = g_gameEvents.subscribe("g_app", "onExit", [](LuaObject*, const GameEvents::Args&) {
        QCoreApplication::quit();
        return std::any{};
    });
    // features.lua re-applies the table on game start as well.
    gameStartHook = g_gameEvents.subscribe("g_game", "onGameStart", [](LuaObject*, const GameEvents::Args&) {
        if (const int version = g_game.getClientVersion(); version > 0)
            enableFeatures(version);
        return std::any{};
    });

    running = true;
    return true;
}

void terminate()
{
    if (!running)
        return;
    running = false;

    if (g_game.isOnline())
        g_game.forceLogout();
    g_gameEvents.unsubscribe(exitHook);
    g_gameEvents.unsubscribe(gameStartHook);

    g_client.terminate();
    // Graphics never starts here, so nothing else drops the textures the engine
    // cached; at static destruction ~Texture would lock ResourceRegistry's
    // already destroyed mutex.
    g_textures.terminate();
    g_eventThreadId = -1;
}

bool isRunning()
{
    return running;
}

bool isForceExit()
{
    return forceExit;
}

void setForceExitHandler(std::function<void()> handler)
{
    forceExitHandler = std::move(handler);
}

void poll()
{
    if (!running)
        return;
    if (exitSignaled) {
        exitSignaled = 0;
        // Queue it: this poll often runs from afterAnimating, still inside the
        // window update timer (see finishExit). Do not mark a force-exit —
        // that used to close the window without Exit / Logout / Cancel.
        // Cancel leaves the process up, so a later signal can ask again.
        if (QCoreApplication* app = QCoreApplication::instance())
            QMetaObject::invokeMethod(app, finishExit, Qt::QueuedConnection);
        else
            finishExit();
    }
}

qint64 nowMicros()
{
    return g_clock.realMicros();
}

void advanceTo(const qint64 micros)
{
    if (!running)
        return;
    g_clock.set(std::max<qint64>(micros, g_clock.realMicros()));
}

void resumeRealTime()
{
    g_clock.update();
}

void setClientVersion(const int version)
{
    // gamelib getClientProtocolVersion: only 980-1002 clients speak an older protocol.
    static const std::map<int, int> olderProtocol{
        { 980, 971 }, { 981, 973 }, { 982, 974 }, { 983, 975 }, { 984, 976 },
        { 985, 977 }, { 986, 978 }, { 1001, 979 }, { 1002, 980 },
    };
    const auto it = olderProtocol.find(version);
    // gamelib: the newest entry of its supported-clients list.
    if (g_gameConfig.getLastSupportedVersion() < version)
        g_gameConfig.setLastSupportedVersion(static_cast<uint16_t>(version));
    g_game.setClientVersion(static_cast<uint16_t>(version));
    g_game.setProtocolVersion(static_cast<uint16_t>(it != olderProtocol.end() ? it->second : version));
    enableFeatures(version);
}

namespace {

/// CipSoft `TTibiaFileSystemHelper` / FUN_101013730:
///   QDir(applicationDirPath()); cdUp(); cd("./Resources");
/// Same as `applicationDirPath()/../Resources`. Package trees use
/// `Contents/Resources` (macOS) or `Resources` (win/linux) under the install root.
bool resourcesDirUsable(const QString& cand)
{
    if (cand.isEmpty())
        return false;
    // Qt QFile sees desktop + wasm MEMFS; also accept tibia::files when the
    // catalog is already under the CipSoft short name.
    if (QDir(cand).exists())
        return true;
    const std::string abs = cand.toStdString();
    return tibia::files::data().directoryExists(abs + "/")
           || tibia::files::data().fileExists(abs + "/assets/catalog-content.json");
}

bool catalogAt(const QString& assetsDir)
{
    return QFileInfo::exists(QDir(assetsDir).filePath(QStringLiteral("catalog-content.json")));
}

QString resolveCipResourcesDir()
{
    // One Cip Resources layout everywhere — only the install root differs:
    // bundled app, AppData tip package, or monorepo extract (`data/` == Resources).
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QString viaApp = QDir(QDir(appDir).absoluteFilePath(QStringLiteral("..")))
                                   .filePath(QStringLiteral("Resources"));
        if (resourcesDirUsable(viaApp))
            return QDir(viaApp).absolutePath();
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        const QString tip = QDir(appData).filePath(
            QLatin1String("packages/") + QLatin1String(TIBIA_CLIENT_PACKAGE_DIR)
            + QLatin1String("/Contents/Resources"));
        if (resourcesDirUsable(tip))
            return QDir(tip).absolutePath();
    }

    QString work = QString::fromStdString(tibia::files::data().getWorkDir());
    while (work.endsWith(QLatin1Char('/')) || work.endsWith(QLatin1Char('\\')))
        work.chop(1);
    if (!work.isEmpty()) {
        // Monorepo local: apps/qtclient/data is the CipSoft Resources tree
        // (assets/ + sounds/ siblings) when the app-bundle symlink is missing.
        const QString dataResources = QDir(work).filePath(QStringLiteral("data"));
        if (catalogAt(QDir(dataResources).filePath(QStringLiteral("assets"))))
            return QDir(dataResources).absolutePath();
    }
    return {};
}

} // namespace

QString cipResourcesDir()
{
    return resolveCipResourcesDir();
}

std::string assetsRoot(const int /*version*/)
{
    const QString resources = resolveCipResourcesDir();
    if (resources.isEmpty())
        return {};
    // Search root = Resources so CIP short names `assets/…` and `sounds/…` resolve.
    tibia::files::data().addSearchPath(resources.toStdString(), true);
    return "assets/";
}

std::string soundsRoot(const int /*version*/)
{
    const QString resources = resolveCipResourcesDir();
    if (!resources.isEmpty())
        tibia::files::data().addSearchPath(resources.toStdString(), true);
    return "sounds/";
}

bool loadAssets(const int version, QString* error)
{
    const std::string dir = assetsRoot(version);
    QStringList errors;
    if (!tibia::assets::load(dir))
        errors << QStringLiteral("Couldn't load assets from %1").arg(QString::fromStdString(dir));
    if (!tibia::assets::loadStaticData(dir))
        errors << QStringLiteral("Couldn't load static data from %1").arg(QString::fromStdString(dir));
    if (g_game.getFeature(Otc::GameProficiency) && !tibia::assets::resolveProficiencies(dir))
        qWarning().noquote() << "gamecore: no proficiencies-<hash>.json in" << QString::fromStdString(dir);

    const std::string soundsDir = soundsRoot(version);
    if (auto* storage = sound::TSoundStorage::instance(); storage && storage->loadBank(soundsDir))
        qInfo().noquote() << "gamecore: loaded sound bank from" << QString::fromStdString(soundsDir);
    // Missing bank is optional (make extract); stay quiet on CipSoft.
    if (error)
        *error = errors.join(u'\n');
    return errors.isEmpty();
}

Hook::Hook(const std::string_view scope, const std::string_view field, Handler handler)
    : m_id(g_gameEvents.subscribe(scope, field, std::move(handler)))
{}

Hook::Hook(Hook&& other) noexcept
    : m_id(std::exchange(other.m_id, 0))
{}

Hook& Hook::operator=(Hook&& other) noexcept
{
    if (this != &other) {
        if (m_id)
            g_gameEvents.unsubscribe(m_id);
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

Hook::~Hook()
{
    if (m_id)
        g_gameEvents.unsubscribe(m_id);
}

QString argString(const std::vector<std::any>& args, const size_t index)
{
    if (index >= args.size())
        return {};
    const auto& a = args[index];
    if (const auto* s = std::any_cast<std::string>(&a))
        return QString::fromStdString(*s);
    if (const auto* v = std::any_cast<std::string_view>(&a))
        return QString::fromUtf8(v->data(), static_cast<qsizetype>(v->size()));
    if (const auto* c = std::any_cast<const char*>(&a))
        return QString::fromUtf8(*c);
    return {};
}

qint64 argInt(const std::vector<std::any>& args, const size_t index)
{
    if (index >= args.size())
        return 0;
    const auto& a = args[index];
    if (const auto* v = std::any_cast<int>(&a)) return *v;
    if (const auto* v = std::any_cast<uint8_t>(&a)) return *v;
    if (const auto* v = std::any_cast<uint16_t>(&a)) return *v;
    if (const auto* v = std::any_cast<uint32_t>(&a)) return *v;
    if (const auto* v = std::any_cast<int64_t>(&a)) return *v;
    if (const auto* v = std::any_cast<uint64_t>(&a)) return static_cast<qint64>(*v);
    if (const auto* v = std::any_cast<int8_t>(&a)) return *v;
    if (const auto* v = std::any_cast<int16_t>(&a)) return *v;
    if (const auto* v = std::any_cast<bool>(&a)) return *v ? 1 : 0;
    return 0;
}

bool argBool(const std::vector<std::any>& args, const size_t index)
{
    if (index < args.size())
        if (const auto* v = std::any_cast<bool>(&args[index]))
            return *v;
    return argInt(args, index) != 0;
}

}
