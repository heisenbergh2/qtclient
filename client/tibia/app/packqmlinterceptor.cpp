#include "packqmlinterceptor.h"

#include "mods/registry.h"

#include <QDir>
#include <QFileInfo>

namespace tibia::app {

namespace {

constexpr QLatin1StringView kQmlPrefix("/qt/qml/");

bool isCipResourcePath(const QString& path)
{
    return path.startsWith(QLatin1String("/images/"))
        || path.startsWith(QLatin1String("/animations/"));
}

/// Pack FS QML resolves absolute /images/… as file:///images/… — map into
/// graphics_resources.rcc (same as DiskQmlInterceptor; required on wasm/release
/// where TIBIA_QML_DISK is off).
QUrl rewriteCipResourceUrl(const QUrl& url)
{
    QString path;
    if (url.scheme() == QLatin1String("file")) {
        path = url.toLocalFile();
#if defined(Q_OS_WIN)
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (path.size() >= 2 && path.at(1) == QLatin1Char(':'))
            path = path.mid(2);
#endif
    } else if (url.scheme().isEmpty()) {
        path = url.path();
    } else {
        return {};
    }
    if (!isCipResourcePath(path))
        return {};
    QUrl qrc;
    qrc.setScheme(QStringLiteral("qrc"));
    qrc.setPath(path);
    return qrc;
}

} // namespace

QUrl PackQmlInterceptor::intercept(const QUrl& url, const DataType type)
{
    if (const QUrl rewritten = rewriteCipResourceUrl(url); rewritten.isValid()) {
        if (type == UrlString || url.scheme() == QLatin1String("file")
            || url.scheme().isEmpty())
            return rewritten;
    }

    if (url.scheme() != QLatin1String("qrc"))
        return url;
    if (type != QmlFile && type != JavaScriptFile && type != UrlString)
        return url;

    const QString path = url.path();
    if (!path.startsWith(kQmlPrefix))
        return url;

    const QString rel = path.mid(kQmlPrefix.size()); // e.g. idleot/TopBar.qml or qmlcomponents/qml/X.qml
    if (rel.isEmpty() || rel.startsWith(QLatin1String("QtQuick/")))
        return url;

    // CIP stock-path forks from pack overrides/ (later packs win via load order).
    if (rel.startsWith(QLatin1String("qmlcomponents/"))) {
        const auto& roots = tibia::mods::packOverrideRoots();
        for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
            const QString file = QDir(QString::fromStdString(*it)).filePath(rel);
            if (QFileInfo::exists(file))
                return QUrl::fromLocalFile(file);
        }
        return url;
    }

    for (const std::string& root : tibia::mods::packQmlImportPaths()) {
        const QString file = QDir(QString::fromStdString(root)).filePath(rel);
        if (QFileInfo::exists(file))
            return QUrl::fromLocalFile(file);
    }
    return url;
}

} // namespace tibia::app
