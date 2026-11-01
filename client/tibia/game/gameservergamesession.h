#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <QString>

#include <vector>

namespace tibia::game {

/// What the game server needs to log a character in.
struct GameserverCredentials
{
    QString account;
    QString password;
    QString sessionKey;
    QString authenticatorToken;
    QString worldName;
    QString host;
    int port = 0;
    QString characterName;
    /// Records the session's packets to this file (CIP's packet log), for replay().
    QString recordTo;
};

/// A live game server session (CIP: TGameserverGameSession) on CIP's /// protocol and game model: g_game's connection, with its engine hooks turned
/// into CIP's session signals.
class TGameserverGameSession final : public cip::tibia::game::TGameserverGameSession
{
    Q_OBJECT
public:
    explicit TGameserverGameSession(QObject* parent = nullptr);
    ~TGameserverGameSession() override;

    /// Connects and logs in; progress arrives as signals.
    void connectToGameserver(const GameserverCredentials& credentials);
    /// Asks the server to log the character out (it may refuse, e.g. in fight).
    void requestLogout();
    /// Drops the connection without waiting for the server.
    void disconnectNow();

    bool isOnline() const;
    QString characterName() const;
    QString worldName() const;

Q_SIGNALS:
    /// CIP's carry an ELoginErrorAction / TSeconds the generator does not map.
    void gameLoginErrorMessage(const QString& ErrorMessage);
    void gameLoginWaitMessage(const QString& WaitMessage, int WaitSecondsUntilNextTry);
    void sessionEndInformation(int Reason);
    /// The server wants a newer client (UpdateNeeded).
    void clientOutOfDate();

private:
    std::vector<client::gamecore::Hook> m_hooks;
};

}
