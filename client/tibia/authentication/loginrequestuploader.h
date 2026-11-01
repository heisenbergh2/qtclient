#pragma once

#include <cip_gen.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace tibia::authentication {

/// The login service's "session" (CipSoft: TPlaySessionData — no email/ssoswitch).
struct PlaySessionData
{
    QString sessionKey;
    qint64 premiumUntil = 0;
    bool isPremium = false;
};

/// One "playdata.worlds" entry (CIP: TWorldList).
struct LoginWorld
{
    int id = 0;
    QString name;
    QString host;
    /// Game port: TCP on desktop, Crystal's WebSocket port in the browser.
    int port = 0;
    bool previewState = false;
    int pvpType = 0;
};

/// One "playdata.characters" entry (CIP: TCharacterList).
struct LoginCharacter
{
    QString name;
    int worldId = 0;
    int level = 0;
    QString vocation;
    int outfitId = 0;
    int headColor = 0;
    int torsoColor = 0;
    int legsColor = 0;
    int detailColor = 0;
    int addonsFlags = 0;
    bool isMale = true;
    bool isMainCharacter = false;
    bool isHidden = false;
    bool isOnline = false;
    bool tutorial = false;
    int dailyRewardState = 0;
};

struct LoginResult
{
    PlaySessionData session;
    QList<LoginWorld> worlds;
    QList<LoginCharacter> characters;

    const LoginWorld* world(int id) const;
};

/// POSTs the login service's JSON (CIP's own uploader; the product's portal speaks
/// the qtclient login format): CIP account/password login via
/// /clientservices/loginservice.php.
class TLoginRequestUploader final : public cip::tibia::authentication::TLoginRequestUploader
{
    Q_OBJECT
public:
    explicit TLoginRequestUploader(QObject* parent = nullptr);

    void login(const QUrl& url, const QString& email, const QString& password, const QString& authenticatorToken, const QString& clientRevision);
    /// Drop an in-flight login. The Abort button on CIP's connecting dialog.
    void abort();

    void onFinished(bool UploadSuccessful, const QJsonDocument& ReplyFromServerJD) override;

    static LoginResult parse(const QJsonObject& reply);

Q_SIGNALS:
    void loginSuccessful(const tibia::authentication::LoginResult& result);

private:
    void post(const QUrl& url, QJsonObject body, const QString& clientRevision);

    QNetworkAccessManager* m_network;
    QNetworkReply* m_reply = nullptr;
};

}
