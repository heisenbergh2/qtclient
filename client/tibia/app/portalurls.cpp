#include "portalurls.h"

#include "input/gameaction.h"

#include <QDesktopServices>
#include <QUrlQuery>

namespace tibia::app {

namespace {

using A = input::EGameAction;

QUrl withPath(const QUrl& base, const QString& path)
{
    QUrl url = base;
    if (url.isEmpty())
        return {};
    const QString cleaned = path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path;
    url.setPath(cleaned);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QUrl withPathAndQuery(const QUrl& base, const QString& path, const QUrlQuery& query)
{
    QUrl url = withPath(base, path);
    if (url.isEmpty())
        return {};
    url.setQuery(query);
    return url;
}

QUrl resolveOverride(const LauncherEnv& env, const char* key)
{
    const QString raw = env.urlOverride(key);
    if (raw.isEmpty())
        return {};
    const QUrl absolute(raw, QUrl::TolerantMode);
    if (absolute.isValid() && !absolute.scheme().isEmpty())
        return absolute;
    return withPath(env.loginBase, raw);
}

}

QUrl PortalUrls::urlFor(const QUrl& loginBase, const A action)
{
    LauncherEnv env;
    env.loginBase = loginBase;
    return urlFor(env, action);
}

QUrl PortalUrls::urlFor(const LauncherEnv& env, const A action)
{
    if (env.loginBase.isEmpty())
        return {};

    const auto tryOverride = [&](const char* key) -> QUrl {
        return resolveOverride(env, key);
    };

    switch (action) {
    case A::OPEN_URL_TIBIA_PAGE: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_TIBIA_PAGE"); !o.isEmpty())
            return o;
        return withPath(env.loginBase, QStringLiteral("/"));
    }
    case A::OPEN_URL_CIPSOFT: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_CIPSOFT"); !o.isEmpty())
            return o;
        return withPath(env.loginBase, QStringLiteral("/"));
    }
    case A::OPEN_URL_MANUAL: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_MANUAL"); !o.isEmpty())
            return o;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("manual"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/gameguides"), q);
    }
    case A::OPEN_URL_FAQ: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_FAQ"); !o.isEmpty())
            return o;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("faq"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/support"), q);
    }
    case A::OPEN_URL_GET_PREMIUM: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_GET_PREMIUM"); !o.isEmpty())
            return o;
        if (!env.paymentsBase.isEmpty())
            return env.paymentsBase;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("redirectlogin"));
        q.addQueryItem(QStringLiteral("clienttarget"), QStringLiteral("payment"));
        q.addQueryItem(QStringLiteral("clientselection"), QStringLiteral("SC"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/account"), q);
    }
    case A::OPEN_URL_PREMIUM_FEATURES: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_PREMIUM_FEATURES"); !o.isEmpty())
            return o;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("premiumfeatures"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/abouttibia"), q);
    }
    case A::OPEN_URL_GET_COINS: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_GET_COINS"); !o.isEmpty())
            return o;
        if (!env.paymentsBase.isEmpty())
            return env.paymentsBase;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("redirectlogin"));
        q.addQueryItem(QStringLiteral("clienttarget"), QStringLiteral("payment"));
        q.addQueryItem(QStringLiteral("clientselection"), QStringLiteral("SC3"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/account"), q);
    }
    case A::OPEN_URL_CREATE_ACCOUNT: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_CREATE_ACCOUNT"); !o.isEmpty())
            return o;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("createaccount"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/account"), q);
    }
    case A::OPEN_URL_ACCESS_ACCOUNT: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_ACCESS_ACCOUNT"); !o.isEmpty())
            return o;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("accountmanagement"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/account"), q);
    }
    case A::OPEN_URL_LOST_ACCOUNT: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_LOST_ACCOUNT"); !o.isEmpty())
            return o;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("subtopic"), QStringLiteral("lostaccount"));
        return withPathAndQuery(env.loginBase, QStringLiteral("/account"), q);
    }
    case A::OPEN_URL_SURVEY: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_SURVEY"); !o.isEmpty())
            return o;
        return withPath(env.loginBase, QStringLiteral("/survey"));
    }
    case A::OPEN_URL_TWITCH_TIBIA: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_TWITCH"); !o.isEmpty())
            return o;
        return withPath(env.loginBase, QStringLiteral("/"));
    }
    case A::OPEN_URL_YOUTUBE_TIBIA: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_YOUTUBE"); !o.isEmpty())
            return o;
        return withPath(env.loginBase, QStringLiteral("/"));
    }
    case A::OPEN_NEWS_DIALOG:
    case A::OPEN_NEWS_CATEGORY:
    case A::REFRESH_COMPENDIUM_NEWS: {
        if (const QUrl o = tryOverride("QTCLIENT_URL_NEWS_ARCHIVE"); !o.isEmpty())
            return o;
        return withPath(env.loginBase, QStringLiteral("/news/archive"));
    }
    default:
        return {};
    }
}

QUrl PortalUrls::clientServicesUrl(const QUrl& loginBase, const QString& clientServicesPath)
{
    if (loginBase.isEmpty())
        return {};
    QUrl url = loginBase;
    QString path = clientServicesPath;
    if (!path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void PortalUrls::open(const QUrl& url)
{
    if (url.isEmpty() || !url.isValid())
        return;
    QDesktopServices::openUrl(url);
}

}
