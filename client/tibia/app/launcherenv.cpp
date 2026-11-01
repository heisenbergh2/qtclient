#include "launcherenv.h"

#include "mods/registry.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace tibia::app {

namespace {

// release_env.lua as written by tools/publish/package_desktop.py:
//   return {
//       KEY = "json string",
//   }
QHash<QString, QString> readReleaseEnv(const QString& workDir)
{
    QHash<QString, QString> values;
    QFile file(QDir(workDir).filePath(QStringLiteral("release_env.lua")));
    if (!file.open(QIODevice::ReadOnly))
        return values;
    static const QRegularExpression entry(QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*("(?:[^"\\]|\\.)*")\s*,?\s*$)"),
                                          QRegularExpression::MultilineOption);
    const QString text = QString::fromUtf8(file.readAll());
    for (auto it = entry.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const auto doc = QJsonDocument::fromJson("[" + m.captured(2).toUtf8() + "]");
        values.insert(m.captured(1), doc.array().first().toString());
    }
    return values;
}

}

bool LauncherEnv::ssoHosted() const
{
#ifdef Q_OS_WASM
    return true;
#else
    return fromLauncher;
#endif
}

bool LauncherEnv::startLauncherIfUpdateNeeded()
{
#ifndef Q_OS_WASM
    if (!qEnvironmentVariable("QTCLIENT_LAUNCHER").isEmpty())
        return false;
    QDir dir(QCoreApplication::applicationDirPath());
    QString local;
    for (int i = 0; i < 6; ++i) {
        QFile version(dir.filePath(QStringLiteral("package.json.version")));
        if (version.open(QIODevice::ReadOnly)) {
            local = QString::fromUtf8(version.readAll()).trimmed();
            break;
        }
        if (!dir.cdUp())
            return false;
    }
    if (local.isEmpty())
        return false;

    const LauncherEnv env = load(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS)
    const QString package = QStringLiteral("tibiaclient-macos-current");
#elif defined(Q_OS_WIN)
    const QString package = QStringLiteral("tibiaclient-windows-current");
#else
    const QString package = QStringLiteral("tibiaclient-linux-current");
#endif
    QNetworkAccessManager net;
    QNetworkReply* reply = net.get(QNetworkRequest(QUrl(env.assetBase.toString() + QStringLiteral("/launcher/") + package + QStringLiteral("/package.json.version"))));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(4000);
    loop.exec();
    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
        reply->abort();
        reply->deleteLater();
        return false;
    }
    const QString remote = QString::fromUtf8(reply->readAll()).trimmed();
    reply->deleteLater();
    if (remote.isEmpty() || remote == local)
        return false;

    QFile pathFile(launcherPathFile());
    if (!pathFile.open(QIODevice::ReadOnly))
        return false;
    const QString bin = QString::fromUtf8(pathFile.readAll()).trimmed();
    if (bin.isEmpty() || !QFileInfo::exists(bin))
        return false;
    QProcess process;
    process.setProgram(bin);
    process.setArguments({ QStringLiteral("--patch") });
    QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
    penv.insert(QStringLiteral("QTCLIENT_CDN_URL"), env.assetBase.toString());
    process.setProcessEnvironment(penv);
    return process.startDetached();
#else
    return false;
#endif
}

namespace {

QString findDevLauncherBinary()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#if defined(Q_OS_MACOS)
        appDir + QStringLiteral("/../../../launcher/launcher.app/Contents/MacOS/launcher"),
        appDir + QStringLiteral("/../../../../launcher/src/launcher.app/Contents/MacOS/launcher"),
#endif
        appDir + QStringLiteral("/launcher"),
        appDir + QStringLiteral("/../launcher/launcher"),
        appDir + QStringLiteral("/../../launcher/launcher"),
    };
    for (const QString& path : candidates) {
        const QFileInfo info(path);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    // client-start / cmake presets: build/<preset>/launcher/src/...
    QDir dir(appDir);
    for (int i = 0; i < 6 && dir.cdUp(); ++i) {
#if defined(Q_OS_MACOS)
        const QString mac = dir.filePath(QStringLiteral("launcher/src/launcher.app/Contents/MacOS/launcher"));
        if (QFileInfo::exists(mac))
            return QFileInfo(mac).canonicalFilePath();
#endif
        // Do not name this `linux` — glibc defines linux as a macro (=1).
        const QString desktop = dir.filePath(QStringLiteral("launcher/src/launcher"));
        if (QFileInfo::exists(desktop))
            return QFileInfo(desktop).canonicalFilePath();
    }
    return {};
}

}

void LauncherEnv::showLauncher()
{
#ifndef Q_OS_WASM
    const QString marker = qEnvironmentVariable("QTCLIENT_SHOW_FILE");
    if (!marker.isEmpty()) {
        QFile file(marker);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write("1");
        return;
    }
    QString bin = qEnvironmentVariable("QTCLIENT_LAUNCHER_BIN");
    if (bin.isEmpty()) {
        QFile pathFile(launcherPathFile());
        if (pathFile.open(QIODevice::ReadOnly))
            bin = QString::fromUtf8(pathFile.readAll()).trimmed();
    }
    if (bin.isEmpty() || !QFileInfo::exists(bin))
        bin = findDevLauncherBinary();
    if (!bin.isEmpty() && QFileInfo::exists(bin))
        QProcess::startDetached(bin, {});
#else
    // The browser session has no desktop launcher to raise.
#endif
}

namespace {

QString ensureLeadingSlash(QString path)
{
    if (!path.startsWith(u'/'))
        path.prepend(u'/');
    return path;
}

QUrl withPath(const QUrl& base, const QString& path)
{
    QUrl url = base;
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QString fromStd(const std::string& s)
{
    return QString::fromStdString(s);
}

} // namespace

QUrl LauncherEnv::loginUrl() const
{
    return withPath(loginBase, loginPath);
}

QUrl LauncherEnv::clientServicesUrl() const
{
    return withPath(loginBase, clientServicesPath);
}

QUrl LauncherEnv::createAccountUrl() const
{
    return withPath(loginBase, createAccountPath);
}

LauncherEnv LauncherEnv::load(const QString& workDir)
{
    const auto release = readReleaseEnv(workDir);
    const tibia::mods::ModUrls* modUrls = tibia::mods::primaryUrls();

    // Precedence: process env > mod.json urls > release_env.lua > real CipSoft.
    const auto pick = [&](const char* name, const QString& modVal, const QString& cipFallback) {
        QString v = qEnvironmentVariable(name);
        if (!v.isEmpty())
            return v;
        if (!modVal.isEmpty())
            return modVal;
        v = release.value(QString::fromLatin1(name));
        return v.isEmpty() ? cipFallback : v;
    };

    LauncherEnv e;
    e.revision = qEnvironmentVariable("QTCLIENT_CLIENT_REVISION", release.value(QStringLiteral("QTCLIENT_REVISION")));
    e.fromLauncher = !qEnvironmentVariableIsEmpty("QTCLIENT_LAUNCHER");

    const QString modLoginBase = modUrls ? fromStd(modUrls->loginBase) : QString();
    QString scheme = qEnvironmentVariable("QTCLIENT_LOGIN_SCHEME");
    QString host = qEnvironmentVariable("QTCLIENT_LOGIN_HOST");
    QString portStr = qEnvironmentVariable("QTCLIENT_LOGIN_PORT");
    if (scheme.isEmpty() && host.isEmpty() && !modLoginBase.isEmpty()) {
        const QUrl base(modLoginBase, QUrl::TolerantMode);
        if (base.isValid() && !base.host().isEmpty()) {
            scheme = base.scheme().isEmpty() ? QStringLiteral("https") : base.scheme();
            host = base.host();
            if (base.port() > 0)
                portStr = QString::number(base.port());
        }
    }
    if (scheme.isEmpty())
        scheme = release.value(QStringLiteral("QTCLIENT_LOGIN_SCHEME"), QStringLiteral("https"));
    if (host.isEmpty())
        host = release.value(QStringLiteral("QTCLIENT_LOGIN_HOST"), QStringLiteral("www.tibia.com"));
    if (portStr.isEmpty())
        portStr = release.value(QStringLiteral("QTCLIENT_LOGIN_PORT"));

    e.loginBase.setScheme(scheme);
    e.loginBase.setHost(host);
    bool portOk = false;
    const int port = portStr.toInt(&portOk);
    if (portOk)
        e.loginBase.setPort(port);

    e.loginPath = ensureLeadingSlash(pick(
        "QTCLIENT_LOGIN_PATH",
        modUrls ? fromStd(modUrls->loginPath) : QString(),
        QStringLiteral("/clientservices/loginservice.php")));
    e.clientServicesPath = ensureLeadingSlash(pick(
        "QTCLIENT_CLIENTSERVICES_PATH",
        modUrls ? fromStd(modUrls->clientServicesPath) : QString(),
        QStringLiteral("/clientservices/clientservices.php")));
    e.createAccountPath = ensureLeadingSlash(pick(
        "QTCLIENT_CREATE_ACCOUNT_PATH",
        modUrls ? fromStd(modUrls->createAccountPath) : QString(),
        QStringLiteral("/clientservices/accountcreationclientservices.php")));

    QString assetBase = pick(
        "QTCLIENT_ASSET_BASE",
        modUrls ? fromStd(modUrls->assetBase) : QString(),
        QString());
    if (assetBase.isEmpty())
        assetBase = release.value(QStringLiteral("TIBIA_ASSET_BASE"));
    if (assetBase.isEmpty())
        assetBase = QStringLiteral("https://static.tibia.com");
    while (assetBase.endsWith(u'/'))
        assetBase.chop(1);
    e.assetBase = QUrl(assetBase);

    QString payments = pick(
        "QTCLIENT_PAYMENTS_URL",
        modUrls ? fromStd(modUrls->paymentsBase) : QString(),
        QString());
    if (payments.isEmpty())
        payments = qEnvironmentVariable("QTCLIENT_PAYMENTS_BASE");
    if (payments.isEmpty())
        payments = release.value(QStringLiteral("QTCLIENT_PAYMENTS_BASE"));
    while (payments.endsWith(u'/'))
        payments.chop(1);
    if (!payments.isEmpty())
        e.paymentsBase = QUrl(payments);

    static const char* const kUrlKeys[] = {
        "QTCLIENT_URL_TIBIA_PAGE",       "QTCLIENT_URL_CIPSOFT",
        "QTCLIENT_URL_MANUAL",           "QTCLIENT_URL_FAQ",
        "QTCLIENT_URL_GET_PREMIUM",      "QTCLIENT_URL_PREMIUM_FEATURES",
        "QTCLIENT_URL_GET_COINS",        "QTCLIENT_URL_CREATE_ACCOUNT",
        "QTCLIENT_URL_ACCESS_ACCOUNT",   "QTCLIENT_URL_LOST_ACCOUNT",
        "QTCLIENT_URL_SURVEY",           "QTCLIENT_URL_NEWS_ARCHIVE",
        "QTCLIENT_URL_TWITCH",           "QTCLIENT_URL_YOUTUBE",
    };
    for (const char* key : kUrlKeys) {
        QString v = qEnvironmentVariable(key);
        if (v.isEmpty() && modUrls) {
            const auto it = modUrls->pages.find(key);
            if (it != modUrls->pages.end())
                v = fromStd(it->second);
        }
        if (v.isEmpty())
            v = release.value(QString::fromLatin1(key));
        if (!v.isEmpty())
            e.urlOverrides.insert(QString::fromLatin1(key), v);
    }
    return e;
}

QString LauncherEnv::urlOverride(const char* envKey) const
{
    return urlOverrides.value(QString::fromLatin1(envKey));
}

}
