#pragma once

#include "client/gamecore.h"

#include <cip_gen.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

namespace tibia::tutorial {
class TSessiondumpHintsProcessor;
}

namespace tibia::game {

/// CIP-shaped session that replays a recorded game world (CIP: TSessiondumpGameSession).
///
/// Supports:
/// - qtclient text hex dumps (`--replay`: `< millis hex>` / `> millis hex`), converted
///   into the same decoded messages the live socket parses
/// - CIP `dmpd` containers (magic `dmpd`): mappable opcodes become classic packets
///   on that same parser; unknown protobuf types are skipped with a log
/// - Sibling `.hints` JSON via `TSessiondumpHintsProcessor` (tutorial cues)
///
/// Live `--record` writes the decoded stream from the primary game connection.
class TSessiondumpGameSession final : public cip::tibia::game::TSessiondumpGameSession
{
    Q_OBJECT
public:
    explicit TSessiondumpGameSession(QObject* parent = nullptr);
    ~TSessiondumpGameSession() override;

    /// Plays a recorded OTC packet log or CIP `dmpd` dump through g_game.
    void replay(const QString& file);
    /// Drops the replay without waiting for the recording to finish.
    void disconnectNow();
    /// No server logout in replay; same as disconnectNow for UI wiring.
    void requestLogout();

    bool isOnline() const;
    QString characterName() const;
    QString worldName() const;

Q_SIGNALS:
    void gameLoginErrorMessage(const QString& ErrorMessage);
    void gameLoginWaitMessage(const QString& WaitMessage, int WaitSecondsUntilNextTry);
    void sessionEndInformation(int Reason);
    void clientOutOfDate();
    /// Named cue from CIP SessionDumpMarker (S 0x05) during a live dump or replay.
    void sessionDumpMarker(const QString& marker);
    /// CIP SessionDumpStart (S 0x04) when a server begins a dump (Crystal TBD).
    void sessionDumpStart(const QString& sessionKey);

protected Q_SLOTS:
    void onRequestResetOfStorages();
    void onSessiondumpFinished();

private:
    struct ReplayBody
    {
        qint64 atMs = 0;
        QByteArray bytes;
    };

    void replayDmpd(const QString& file);
    void replayHexDump(const QString& file);
    void startReplay(std::vector<ReplayBody> packets);
    void pumpReplay();
    void stopReplay();
    void startHintsForDump(const QString& dumpPath);
    void stopHints();

    QTimer m_replayTimer;
    QElapsedTimer m_replayClock;
    std::vector<ReplayBody> m_replayPackets;
    int m_replayIndex = 0;
    bool m_replaying = false;

    std::vector<client::gamecore::Hook> m_hooks;
    std::unique_ptr<tutorial::TSessiondumpHintsProcessor> m_hints;
};

}
