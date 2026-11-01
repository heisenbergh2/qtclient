#pragma once

#include <QDir>
#include <QHash>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

namespace tibia::app {

/// Login / AAC config for the tibia client.
/// Precedence: process env > mod.json `urls` > release_env.lua > real CipSoft fallbacks.
/// No pack / `--no-mods`: https://www.tibia.com + https://static.tibia.com.
/// Tip `mods/default` = myAAC; IdleOT pack = portal CIP urls.
struct LauncherEnv
{
    /// Started by the desktop launcher (QTCLIENT_LAUNCHER=1).
    bool fromLauncher = false;
    /// Client revision sent to the portal: the launcher's QTCLIENT_CLIENT_REVISION, else the
    /// release build's QTCLIENT_REVISION; empty for dev builds.
    QString revision;
    /// AAC origin (from QTCLIENT_LOGIN_* / mod urls.loginBase / release_env / www.tibia.com).
    QUrl loginBase;
    /// loginWebService path (CIP fallback: /clientservices/loginservice.php).
    QString loginPath;
    /// clientWebService path (CIP fallback: /clientservices/clientservices.php).
    QString clientServicesPath;
    /// accountCreationClientServices path.
    QString createAccountPath;
    /// Public asset origin (QTCLIENT_ASSET_BASE), defaulting to https://static.tibia.com.
    QUrl assetBase;
    /// Coin webshop origin (QTCLIENT_PAYMENTS_URL / QTCLIENT_PAYMENTS_BASE); empty = compose on loginBase.
    QUrl paymentsBase;
    /// Optional full URL or path overrides keyed by QTCLIENT_URL_* env names (e.g. QTCLIENT_URL_GET_COINS).
    QHash<QString, QString> urlOverrides;

    /// Bring the launcher window forward. Writes QTCLIENT_SHOW_FILE when the
    /// launcher started this process; otherwise starts QTCLIENT_LAUNCHER_BIN.
    static void showLauncher();

    /// CIP `checkForPatchMarkerAndStartLauncherIfItExists`: an installed
    /// client whose package version is behind the CDN starts the launcher
    /// with `--patch` and exits. A dev binary is not that package.
    static bool startLauncherIfUpdateNeeded();

    /// Path the launcher writes so the client can find it. CIP keeps the
    /// launcher beside the client; ours is a separate binary.
    static QString launcherPathFile()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Tibia");
        QDir().mkpath(dir);
        return QDir(dir).filePath(QStringLiteral("launcher.path"));
    }

    bool isRelease() const { return !revision.isEmpty(); }
    /// True when started by the desktop launcher or as WASM (/play).
    bool ssoHosted() const;
    QUrl loginUrl() const;
    QUrl clientServicesUrl() const;
    QUrl createAccountUrl() const;

    /// Override string for an OPEN_URL_* action env key, or empty.
    QString urlOverride(const char* envKey) const;

    /// Reads release_env.lua from workDir (if present) and the environment.
    static LauncherEnv load(const QString& workDir);
};

}
