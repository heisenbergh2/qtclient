#include "brand.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {

LauncherBrand g_brand;

QString tipModsRoot()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mods"));
}

bool readBrandJson(const QString& path, LauncherBrand* out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj.isEmpty())
        return false;
    out->packRoot = QFileInfo(path).absolutePath();
    out->id = obj.value(QStringLiteral("id")).toString();
    if (out->id.isEmpty())
        out->id = QFileInfo(out->packRoot).fileName();
    const auto str = [&](const char* key, QString* dest) {
        const QString v = obj.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            *dest = v;
    };
    str("productName", &out->productName);
    str("productOrg", &out->productOrg);
    str("productUrl", &out->productUrl);
    str("defaultCdn", &out->defaultCdn);
    str("clientPackageDir", &out->clientPackageDir);
    out->clientPackageVariants.clear();
    const QJsonArray variants = obj.value(QStringLiteral("clientPackageVariants")).toArray();
    for (const QJsonValue& v : variants) {
        const QString s = v.toString();
        if (!s.isEmpty())
            out->clientPackageVariants.append(s);
    }
    out->loaded = true;
    return true;
}

bool tryLoadBrandDir(const QString& packDir)
{
    const QString brandPath = QDir(packDir).filePath(QStringLiteral("brand.json"));
    if (!readBrandJson(brandPath, &g_brand))
        return false;
    qInfo().noquote() << "launcher brand pack:" << g_brand.id << g_brand.packRoot;
    return true;
}

QString resolveEnvBrandPack(const QString& envPath)
{
    if (envPath.isEmpty())
        return {};
    if (QFile::exists(QDir(envPath).filePath(QStringLiteral("brand.json"))))
        return envPath;
    if (QFile::exists(QDir(envPath).filePath(QStringLiteral("mod.json"))))
        return envPath; // client pack without brand — no chrome override
    QDir dir(envPath);
    if (!dir.exists())
        return {};
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& info : entries) {
        if (info.fileName().startsWith(QLatin1Char('_')))
            continue;
        if (QFile::exists(QDir(info.absoluteFilePath()).filePath(QStringLiteral("brand.json"))))
            return info.absoluteFilePath();
    }
    return {};
}

} // namespace

LauncherBrand& LauncherBrand::instance()
{
    return g_brand;
}

void LauncherBrand::load(int argc, char* argv[])
{
    g_brand = LauncherBrand{};
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--no-mods") == 0)
            g_brand.noMods = true;
        else if (qstrcmp(argv[i], "--default-mod") == 0)
            g_brand.defaultMod = true;
    }
    if (g_brand.noMods) {
        qInfo("--no-mods: stock qtclient launcher (Play → CipSoft client)");
        return;
    }

    if (g_brand.defaultMod) {
        const QString def = QDir(tipModsRoot()).filePath(QStringLiteral("default"));
        if (tryLoadBrandDir(def))
            return;
        qInfo("--default-mod: no tip mods/default/brand.json (CIP chrome; client still loads default urls)");
        return;
    }

    const QString env = qEnvironmentVariable("QTCLIENT_MODS_DIR");
    if (!env.isEmpty()) {
        const QString pack = resolveEnvBrandPack(env);
        if (!pack.isEmpty() && tryLoadBrandDir(pack))
            return;
        // Exclusive env may be a client-only pack (mod.json, no brand) — keep CIP chrome.
        qInfo().noquote() << "QTCLIENT_MODS_DIR set; no brand.json under" << env;
        return;
    }

    const QDir mods(tipModsRoot());
    if (!mods.exists())
        return;

    // First tip pack with brand.json wins (product tips ship one mods/<id>/).
    const QFileInfoList entries = mods.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& info : entries) {
        if (info.fileName().startsWith(QLatin1Char('_')))
            continue;
        if (tryLoadBrandDir(info.absoluteFilePath()))
            return;
    }
}

QString LauncherBrand::chromePath(const QString& fileName) const
{
    if (!packRoot.isEmpty()) {
        const QString disk = QDir(packRoot).filePath(QStringLiteral("images/launcher/") + fileName);
        if (QFileInfo::exists(disk))
            return disk;
    }
    return {};
}

QString LauncherBrand::chromeUrl(const QString& fileName) const
{
    const QString disk = chromePath(fileName);
    if (!disk.isEmpty())
        return QUrl::fromLocalFile(disk).toString();
    return QStringLiteral(":/images/launcher/") + fileName;
}
