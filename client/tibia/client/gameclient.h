#pragma once

#include "sessiondump/sessiondumpciptypes.h"

#include "app/launcherenv.h"
#include "authentication/loginrequestuploader.h"
#include "game/gameservergamesession.h"

#include <cip_gen.h>

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>

class QQuickWindow;

namespace tibia::renderer {
class TNextFrameUpdater;
}
namespace tibia::gamewindow {
class TCharacterSelectionController;
struct CharacterEntry;
}
namespace tibia::game {
class TGameserverGameSession;
class TSessiondumpGameSession;
class TStatusbarStorage;
}
namespace tibia::network {
class ExtendedOpcodeRouter;
class TGameserverDualConnection;
}
namespace tibia::sound {
class TSoundProtocolMessageHandler;
class TSoundStorage;
}
namespace tibia::qmlcomponents {
class TWorldMapRenderProvider;
}
namespace tibia::config {
class TSidebarOptions;
}
namespace tibia::minimap {
class TMinimapManager;
}
namespace tibia::worldmap {
class TAmbientLightStorage;
class TFrameDurationRing;
class TNextFrameTimestampPredictor;
class TNumericalEffectStorage;
class TWorldMapStorage;
class TWorldmapProtocolMessageHandler;
}
namespace tibia::chat {
class TGameMessageHandler;
class TWhiteBlacklist;
class TTextStorage;
}
namespace tibia::effects {
class TEffectStorage;
class TEffectGameActionHandler;
class TEffectProtocolMessageHandler;
}

namespace tibia::client {

/// The client's top level (CIP: TGameClient): engine pump, assets, account
/// login, character selection and the game server session.
class TGameClient final : public cip::tibia::client::TGameClient
{
    Q_OBJECT
public:
    struct Options
    {
        app::LauncherEnv env;
        /// Account login without SSO (dev).
        QString account;
        QString password;
        /// Picks this character as soon as the list arrives.
        QString autoCharacter;
        /// Logs out and quits this many seconds after entering the world (smoke tests); 0 keeps playing.
        int playForSeconds = 0;
        /// Said one per second after entering the world (smoke tests).
        QStringList say;
        /// Records the game session to this file.
        QString recordTo;
        /// Plays this recorded session instead of logging in.
        QString replay;
    };

    explicit TGameClient(QObject* parent = nullptr);
    ~TGameClient() override;

    /// Starts the engine, loads assets and logs in. False when the engine
    /// cannot start (no work dir).
    bool start(const QString& argv0, const QString& workDir, const Options& options);
    void shutdown();
    /// Paces the game by this window's frames.
    void setWindow(QQuickWindow* window);

    gamewindow::TCharacterSelectionController* characterSelection() const { return m_characterSelection.get(); }
    game::TGameserverGameSession* session() const { return m_session.get(); }
    /// CIP-shaped replay session when `--replay` is set; null for live login.
    game::TSessiondumpGameSession* sessiondump() const { return m_sessiondump.get(); }
    network::ExtendedOpcodeRouter* opcodes() const { return m_opcodes.get(); }
    qmlcomponents::TWorldMapRenderProvider* worldMapRenderProvider() const { return m_worldMapRenderProvider.get(); }
    /// Tick-to-swap durations for the FPS indicator's currentFrameCount.
    worldmap::TFrameDurationRing* frameDurations() const { return m_frameDurations.get(); }
    worldmap::TAmbientLightStorage* ambientLight() const { return m_ambientLight.get(); }
    worldmap::TWorldMapStorage* worldMapStorage() const { return m_worldMapStorage.get(); }
    worldmap::TWorldmapProtocolMessageHandler* worldmapProtocol() const { return m_worldmapProtocol.get(); }
    /// Speech bubbles and screen messages; null before start().
    chat::TTextStorage* textStorage() const { return m_textStorage.get(); }
    /// Wire the chat pane's ignore list into map bubbles (CIP handler +0x98).
    void setMessageIgnoreList(chat::TWhiteBlacklist* list);
    worldmap::TNumericalEffectStorage* numericalEffects() const { return m_numericalEffects.get(); }
    effects::TEffectStorage* effectStorage() const { return m_effectStorage.get(); }
    /// The character's sidebars.json, loaded on entering the world.
    config::TSidebarOptions* sidebarOptions() const { return m_sidebarOptions.get(); }
    /// The character's statusBarData.json, loaded on entering the world.
    game::TStatusbarStorage* statusbarStorage() const { return m_statusbarStorage.get(); }
    /// The session's minimap; null outside the world.
    minimap::TMinimapManager* minimap() const { return m_minimap.get(); }
    /// The chosen world's EWorldType (CIP TGameSessionBase +0x34, set by
    /// v147 0x100b2e430 with worldTypeSet): the login service's raw pvptype,
    /// 0 open, 1 optional, 2 hardcore PvP; -1 before a character is chosen.
    int worldType() const { return m_worldType; }

    static constexpr int ClientVersion = 1530;

    /// The login service's answer for this account; empty before login.
    const authentication::LoginResult& account() const { return m_account; }
    /// The password of a password login (empty after a launcher login): CIP's
    /// game session login data (+0x18) the chat refuses to send (0x100b9e320).
    const QString& accountPassword() const { return m_options.password; }
    /// The account changed (a character was created): `reply` is the login
    /// service's document with the new character list. Logs in again when
    /// it has none.
    void refreshAccount(const QJsonObject& reply);
    /// Password login from the product login dialog (`make client-start` without SSO).
    void submitCredentials(const QString& account, const QString& password);
    /// CIP Change Character: show CharacterSelection while online (optional lobby alt Online flags).
    void openCharacterSelectionForSwitch(const QJsonArray& alts = {});
    /// Hide CharacterSelection without logging out / quitting (in-game Switch toggle).
    void closeCharacterSelection();
    /// CIP Ctrl/Cmd+G / sidebar Switch: toggle in-game CharacterSelection while online.
    void toggleCharacterSelectionForSwitch();
    /// Start screen must show the character list or the login mask, never neither.
    void ensureLoginSurface();
    /// Abort on CIP's connecting dialog: drop the login HTTP request, or the
    /// game connection if the world is not entered yet.
    void abortPendingEnter();
    /// Send C 0x0D character switch; server runs Alts.switchAlt (same-socket quick switch).
    bool requestCharacterSwitch(const QString& characterName);
    /// End a tutorial sessiondump cinematic and connect to the pending live world.
    void finishTutorialCinematic();

Q_SIGNALS:
    /// The login service or the game server wants a newer client.
    void clientOutOfDate();
    /// No launcher SSO / --account: show the product login dialog.
    void loginCredentialsNeeded();
    /// A login HTTP request was just sent (password or launcher session).
    void loginStarted();
    /// The account's characters are in characterSelection(); show CharacterSelection.qml.
    void characterSelectionRequested();
    void characterSelectionClosed();
    /// Cancel on the login-page character list: show the start-screen login mask again.
    void returnToLoginRequested();
    /// C 0x0D switch request accepted locally — overlay may show until S 0x0D completes.
    void characterSwitchRequested(const QString& characterName);
    /// The session ended; mainWindowInitialized follows the next world entry.
    void worldLeft();
    /// Account email after CipSoft password login / CLI (for remember-email).
    void sessionEmailAvailable(const QString& email);

private:
    void login();
    void onLoginSuccessful(const authentication::LoginResult& result);
    void onCharacterChosen(const gamewindow::CharacterEntry& character);
    void onWorldEntered();
    /// Writes the session's minimap and character options.
    void onWorldLeft();
    void startTutorialThenLive(const game::GameserverCredentials& live, const QString& dumpPath);
    void connectLive(const game::GameserverCredentials& credentials);
    void fail(const QString& message);
    /// Once per frame, before the scene graph syncs (CIP: FUN_100907560).
    void tick();
    /// The frame-rate limiter; false when this frame comes too early.
    bool frameSlotReached(qint64 nowMicros);

    Options m_options;
    /// Ticks while no frames come (a hidden window).
    QTimer m_idleTick;
    QTimer m_frameLimitTimer;
    std::unique_ptr<worldmap::TFrameDurationRing> m_frameDurations;
    std::unique_ptr<worldmap::TNextFrameTimestampPredictor> m_predictor;
    std::unique_ptr<renderer::TNextFrameUpdater> m_updater;
    bool m_frameRateLimitEnabled = false;
    int m_frameRateLimit = 60;
    int m_activeFrameRateLimit = 0;
    qint64 m_frameGridBase = 0;
    qint64 m_frameSlot = 0;
    qint64 m_frameInterval = 0;
    std::unique_ptr<authentication::TLoginRequestUploader> m_login;
    bool m_suppressConnectError = false;
    std::unique_ptr<gamewindow::TCharacterSelectionController> m_characterSelection;
    /// True while CharacterSelection is open for an in-game Change Character switch.
    bool m_characterSelectionOpen = false;
    std::unique_ptr<game::TGameserverGameSession> m_session;
    std::unique_ptr<game::TSessiondumpGameSession> m_sessiondump;
    std::unique_ptr<network::ExtendedOpcodeRouter> m_opcodes;
    std::unique_ptr<sound::TSoundProtocolMessageHandler> m_soundProtocol;
    std::unique_ptr<sound::TSoundStorage> m_soundStorage;
    std::unique_ptr<network::TGameserverDualConnection> m_dualConnection;
    std::unique_ptr<qmlcomponents::TWorldMapRenderProvider> m_worldMapRenderProvider;
    std::unique_ptr<config::TSidebarOptions> m_sidebarOptions;
    std::unique_ptr<game::TStatusbarStorage> m_statusbarStorage;
    std::unique_ptr<worldmap::TAmbientLightStorage> m_ambientLight;
    std::unique_ptr<worldmap::TWorldMapStorage> m_worldMapStorage;
    std::unique_ptr<worldmap::TWorldmapProtocolMessageHandler> m_worldmapProtocol;
    std::unique_ptr<chat::TTextStorage> m_textStorage;
    std::unique_ptr<worldmap::TNumericalEffectStorage> m_numericalEffects;
    std::unique_ptr<effects::TEffectStorage> m_effectStorage;
    std::unique_ptr<effects::TEffectGameActionHandler> m_effectGameActionHandler;
    std::unique_ptr<effects::TEffectProtocolMessageHandler> m_effectProtocolMessageHandler;
    std::unique_ptr<chat::TGameMessageHandler> m_messageHandler;
    std::unique_ptr<minimap::TMinimapManager> m_minimap;
    // CIP sessiondump surfaces (live replay is TSessiondumpGameSession).
    sessiondump::TClientSessiondumpRecorder m_sessiondumpRecorder;
    sessiondump::TLightweightSessiondumpMessageProcessor m_lightweightSessiondumpMessageProcessor;
    sessiondump::TSessiondumpAsyncLoader m_sessiondumpAsyncLoader;
    sessiondump::TSessiondumpCatalogLoader m_sessiondumpCatalogLoader;
    sessiondump::TSessiondumpFactory m_sessiondumpFactory;
    sessiondump::TSessiondumpMessageProcessor m_sessiondumpMessageProcessor;
    sessiondump::TSessiondumpMessageReader m_sessiondumpMessageReader;
    sessiondump::TSessiondumpMetadataReader m_sessiondumpMetadataReader;
    sessiondump::TSessiondumpPacketProcessor m_sessiondumpPacketProcessor;
    sessiondump::TSessiondumpPacketReader m_sessiondumpPacketReader;
    sessiondump::TSessiondumpPlayer m_sessiondumpPlayer;
    sessiondump::TSessiondumpProcessor m_sessiondumpProcessor;
    sessiondump::TSessiondumpProgressClockService m_sessiondumpProgressClockService;
    sessiondump::TSessiondumpReader m_sessiondumpReader;
    sessiondump::TSessiondumpReadersFactory m_sessiondumpReadersFactory;
    authentication::LoginResult m_account;
    std::optional<game::GameserverCredentials> m_pendingLiveCredentials;
    int m_worldType = -1;
    bool m_started = false;
    bool m_tutorialCinematic = false;
};

}
