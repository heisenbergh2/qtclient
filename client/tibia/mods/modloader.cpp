#include "mods/modloader.h"
#include "mods/mod_entry.h"
#include "mods/registry.h"

#include "qtclient/mod_api_v1.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(Q_OS_WASM)
#  include <dlfcn.h>
#endif

Q_LOGGING_CATEGORY(lcModLoader, "tibia.mods.loader")

namespace tibia::mods {
namespace {

// Process-lifetime native pack handles (desktop QLibrary; WASM uses dlopen).
std::vector<std::unique_ptr<QLibrary>> g_keptLibraries;

struct PackMeta {
    QString dir;
    QString id;
    QString nativeRel;
    QString qmlModule;
    QString hostBuildId;
    int api = 0;
    int priority = 100;
    bool autoload = true;
    bool urlsOnly = false; ///< true when mod.json omits native (config/identity pack)
    ModIdentity identity;
    ModUrls urls;
};

ModUrls readUrlsObject(const QJsonObject& urlsObj, int priority)
{
    ModUrls u;
    u.priority = priority;
    u.loginBase = urlsObj.value(QStringLiteral("loginBase")).toString().toStdString();
    u.loginPath = urlsObj.value(QStringLiteral("loginPath")).toString().toStdString();
    u.clientServicesPath = urlsObj.value(QStringLiteral("clientServicesPath")).toString().toStdString();
    u.createAccountPath = urlsObj.value(QStringLiteral("createAccountPath")).toString().toStdString();
    u.assetBase = urlsObj.value(QStringLiteral("assetBase")).toString().toStdString();
    u.paymentsBase = urlsObj.value(QStringLiteral("paymentsBase")).toString().toStdString();
    const QJsonObject pages = urlsObj.value(QStringLiteral("pages")).toObject();
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        const QString v = it.value().toString();
        if (!v.isEmpty())
            u.pages.emplace(it.key().toStdString(), v.toStdString());
    }
    return u;
}

bool readMeta(const QString& packDir, PackMeta* out)
{
    QFile f(QDir(packDir).filePath(QStringLiteral("mod.json")));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty())
        return false;
    out->dir = packDir;
    out->id = root.value(QStringLiteral("id")).toString();
    out->api = root.value(QStringLiteral("QTCLIENT_MOD_API")).toInt(0);
    out->priority = root.value(QStringLiteral("priority")).toInt(100);
    out->autoload = root.value(QStringLiteral("autoload")).toBool(true);
    out->nativeRel = root.value(QStringLiteral("native")).toString();
    out->urlsOnly = out->nativeRel.isEmpty();
    if (out->nativeRel.isEmpty())
        out->nativeRel = QStringLiteral("native/mod");
    out->qmlModule = root.value(QStringLiteral("qmlModule")).toString();
    out->hostBuildId = root.value(QStringLiteral("hostBuildId")).toString();
    const QJsonObject id = root.value(QStringLiteral("identity")).toObject();
    out->identity.id = out->id.toStdString();
    out->identity.displayName = id.value(QStringLiteral("displayName")).toString().toStdString();
    out->identity.organization = id.value(QStringLiteral("organization")).toString().toStdString();
    out->identity.organizationDomain =
        id.value(QStringLiteral("organizationDomain")).toString().toStdString();
    out->identity.bundleId = id.value(QStringLiteral("bundleId")).toString().toStdString();
    out->identity.cdnBaseUrl = id.value(QStringLiteral("cdnBaseUrl")).toString().toStdString();
    out->identity.priority = out->priority;
    out->urls = readUrlsObject(root.value(QStringLiteral("urls")).toObject(), out->priority);
    return !out->id.isEmpty();
}

bool nativeExists(const QString& native)
{
    return QFile::exists(native);
}

QString resolveNativePath(const PackMeta& meta)
{
    QDir dir(meta.dir);
    const QString base = dir.filePath(meta.nativeRel);
    if (QFile::exists(base))
        return base;
    if (QFile::exists(base + QStringLiteral(".so")))
        return base + QStringLiteral(".so");
    if (QFile::exists(base + QStringLiteral(".dylib")))
        return base + QStringLiteral(".dylib");
    if (QFile::exists(base + QStringLiteral(".dll")))
        return base + QStringLiteral(".dll");
    if (QFile::exists(base + QStringLiteral(".wasm")))
        return base + QStringLiteral(".wasm");
    return base;
}

void registerPackQmlPaths(const PackMeta& meta)
{
    const QDir dir(meta.dir);
    const QString qmlDir = dir.filePath(QStringLiteral("qml"));
    if (QDir(qmlDir).exists()) {
        addPackQmlImportPath(qmlDir.toStdString());
        const QString moduleName =
            meta.qmlModule.isEmpty() ? meta.id : meta.qmlModule;
        const QString moduleDir = QDir(qmlDir).filePath(moduleName);
        if (!QFile::exists(QDir(moduleDir).filePath(QStringLiteral("qmldir")))) {
            qCWarning(lcModLoader) << "mod" << meta.id
                                   << "missing FS QML module"
                                   << moduleDir + QStringLiteral("/qmldir");
        }
    }
    const QString overridesDir = dir.filePath(QStringLiteral("overrides"));
    if (QDir(overridesDir).exists())
        addPackOverrideRoot(overridesDir.toStdString());
}

bool loadNativeEntry(const QString& native, const ModContext* earlyCtx)
{
    auto lib = std::make_unique<QLibrary>(native);
    if (lib->load()) {
        auto* entry = reinterpret_cast<QtClientModEntryFn>(lib->resolve(QTCLIENT_MOD_ENTRY_NAME));
        if (!entry) {
            qCWarning(lcModLoader) << "missing" << QTCLIENT_MOD_ENTRY_NAME << "in" << native;
            return false;
        }
        entry(earlyCtx);
        g_keptLibraries.push_back(std::move(lib));
        return true;
    }

#if defined(Q_OS_WASM)
    void* handle = dlopen(native.toUtf8().constData(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        qCWarning(lcModLoader) << "failed to load" << native << lib->errorString()
                               << "dlopen:" << (dlerror() ? dlerror() : "");
        return false;
    }
    auto* entry = reinterpret_cast<QtClientModEntryFn>(dlsym(handle, QTCLIENT_MOD_ENTRY_NAME));
    if (!entry) {
        qCWarning(lcModLoader) << "missing" << QTCLIENT_MOD_ENTRY_NAME << "in" << native;
        return false;
    }
    entry(earlyCtx);
    return true;
#else
    qCWarning(lcModLoader) << "failed to load" << native << lib->errorString();
    return false;
#endif
}

bool& skipLoadFlag()
{
    static bool skip = false;
    return skip;
}

bool& forceDefaultFlag()
{
    static bool force = false;
    return force;
}

QString findNamedPack(const QStringList& tipRoots, const QString& name)
{
    for (const QString& root : tipRoots) {
        const QString path = QDir(root).filePath(name);
        if (QFile::exists(QDir(path).filePath(QStringLiteral("mod.json"))))
            return path;
    }
    return {};
}

/// Pick at most one pack (lowest mod.json priority wins).
QString pickTipPack(const QStringList& tipRoots)
{
    struct Candidate { QString path; int priority = 100; };
    QList<Candidate> found;
    for (const QString& root : tipRoots) {
        QDir dir(root);
        if (!dir.exists()) continue;
        for (const QFileInfo& info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (info.fileName().startsWith(QLatin1Char('_'))) continue;
            PackMeta meta;
            if (!readMeta(info.absoluteFilePath(), &meta) || !meta.autoload) continue;
            found.append({info.absoluteFilePath(), meta.priority});
        }
    }
    if (found.isEmpty()) return {};
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.path < b.path;
    });
    if (found.size() > 1)
        qCWarning(lcModLoader) << "multiple tip packs; using" << found.first().path;
    return found.first().path;
}

/// QTCLIENT_MODS_DIR: pack dir with mod.json, or parent with a single autoload child.
QString resolveEnvPack(const QString& envPath)
{
    if (envPath.isEmpty())
        return {};
    const QString cleaned = QDir::cleanPath(envPath);
    if (QFile::exists(QDir(cleaned).filePath(QStringLiteral("mod.json"))))
        return cleaned;
    QDir dir(cleaned);
    if (!dir.exists()) {
        qCWarning(lcModLoader) << "QTCLIENT_MODS_DIR missing" << envPath;
        return {};
    }
    QStringList found;
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& info : entries) {
        if (info.fileName().startsWith(QLatin1Char('_')))
            continue;
        PackMeta meta;
        if (!readMeta(info.absoluteFilePath(), &meta) || !meta.autoload)
            continue;
        found.append(info.absoluteFilePath());
    }
    if (found.size() == 1)
        return found.first();
    if (found.isEmpty()) {
        qCWarning(lcModLoader) << "QTCLIENT_MODS_DIR has no autoload pack" << envPath;
        return {};
    }
    qCWarning(lcModLoader) << "QTCLIENT_MODS_DIR has multiple packs; using" << found.first();
    return found.first();
}

} // namespace

void ModLoader::setSkipLoad(bool skip)
{
    skipLoadFlag() = skip;
}

bool ModLoader::skipLoad()
{
    return skipLoadFlag();
}

void ModLoader::setForceDefaultMod(bool force)
{
    forceDefaultFlag() = force;
}

bool ModLoader::forceDefaultMod()
{
    return forceDefaultFlag();
}

std::vector<std::unique_ptr<QLibrary>>& ModLoader::keptLibraries()
{
    return g_keptLibraries;
}

QStringList ModLoader::tipModsRoots()
{
    QStringList roots;
    if (QCoreApplication::instance()) {
        const QDir app(QCoreApplication::applicationDirPath());
        // cleanPath: on WASM applicationDirPath is often "/", so
        // absoluteFilePath("../mods") becomes "/../mods". That still finds
        // files via Qt but dlopen(SIDE_MODULE) fails on the non-normalized
        // path (tip pack → stock login logo). Dedup after clean.
        const QStringList raw = {
            app.filePath(QStringLiteral("mods")),
            app.absoluteFilePath(QStringLiteral("../Resources/mods")),
            app.absoluteFilePath(QStringLiteral("../mods")),
        };
        for (const QString& r : raw) {
            const QString cleaned = QDir::cleanPath(r);
            if (!cleaned.isEmpty() && !roots.contains(cleaned))
                roots << cleaned;
        }
    }
    return roots;
}

QStringList ModLoader::defaultRoots()
{
    // Legacy helper: tip roots only (selection logic is in loadAll).
    return tipModsRoots();
}

bool ModLoader::loadFromDirectory(const QString& packDir, const ModContext* earlyCtx)
{
    const QString pack = QDir::cleanPath(packDir);
    PackMeta meta;
    if (!readMeta(pack, &meta)) {
        qCWarning(lcModLoader) << "invalid mod.json in" << pack;
        return false;
    }
    if (meta.api < kMinApi || meta.api > kSupportedApi) {
        qCWarning(lcModLoader) << "mod" << meta.id << "API" << meta.api
                               << "outside host range" << kMinApi << ".." << kSupportedApi;
        return false;
    }
    // Prefer cleaned pack dir for native/QML paths (see tipModsRoots).
    meta.dir = pack;

    if (!meta.hostBuildId.isEmpty()) {
        const qtclient::ModApiV1* api = qtclient_mod_host_api_v1();
        const char* hostId = api && api->hostBuildId ? api->hostBuildId : "";
        if (std::strcmp(hostId, meta.hostBuildId.toUtf8().constData()) != 0) {
            const bool strict = qEnvironmentVariableIntValue("QTCLIENT_MOD_STRICT_HOST");
            qCWarning(lcModLoader) << "mod" << meta.id << "hostBuildId" << meta.hostBuildId
                                   << "!= host" << hostId
                                   << (strict ? "(reject)" : "(continuing; set QTCLIENT_MOD_STRICT_HOST=1 to reject)");
            if (strict)
                return false;
        }
    }

    // Atomic product load: when native is declared or present on disk, dlopen
    // must succeed before identity / urls / QML registration. Otherwise tip
    // packs show chrome without backends (partial product).
    const QString native = resolveNativePath(meta);
    const bool haveNative = nativeExists(native);
    const bool expectsNative = !meta.urlsOnly;
    if (expectsNative || haveNative) {
        if (!haveNative) {
            qCCritical(lcModLoader) << "mod" << meta.id << "declares native but missing" << native;
            return false;
        }
        if (!loadNativeEntry(native, earlyCtx)) {
            qCCritical(lcModLoader) << "mod" << meta.id << "native load failed" << native
                                    << "(refusing identity/urls/QML side effects)";
            return false;
        }
    }

    if (!meta.identity.displayName.empty())
        registerMod({ meta.identity, nullptr });
    if (!meta.urls.empty())
        registerModUrls(meta.urls);
    registerPackQmlPaths(meta);

    if (expectsNative || haveNative) {
        qCInfo(lcModLoader) << "loaded mod" << meta.id << "from" << pack;
        return true;
    }

    if (!meta.urls.empty() || !meta.identity.displayName.empty()) {
        qCInfo(lcModLoader) << "loaded urls/identity-only mod" << meta.id << "from" << pack;
        return true;
    }

    qCWarning(lcModLoader) << "mod" << meta.id << "has no native, urls, or identity";
    return false;
}

int ModLoader::loadAll(const QStringList& roots, const ModContext* earlyCtx)
{
    if (skipLoad())
        return 0;

    QString packDir;
    const QStringList tipRoots = roots.isEmpty() ? tipModsRoots() : roots;

    if (forceDefaultMod()) {
        packDir = findNamedPack(tipRoots, QStringLiteral("default"));
        if (packDir.isEmpty()) {
            qCCritical(lcModLoader) << "--default-mod: tip mods/default/mod.json not found under"
                                    << tipRoots;
            return 0;
        }
        qCInfo(lcModLoader) << "--default-mod: loading" << packDir;
    } else {
        const QString env = qEnvironmentVariable("QTCLIENT_MODS_DIR");
        if (!env.isEmpty()) {
            packDir = resolveEnvPack(env);
            if (packDir.isEmpty())
                return 0;
            qCInfo(lcModLoader) << "QTCLIENT_MODS_DIR exclusive:" << packDir;
        } else {
            packDir = pickTipPack(tipRoots);
        }
    }

    if (packDir.isEmpty())
        return 0;
    return loadFromDirectory(packDir, earlyCtx) ? 1 : 0;
}

} // namespace tibia::mods
