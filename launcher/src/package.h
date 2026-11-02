#pragma once

#include "app/hints.h"

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QUrl>
#include <QVector>

#include <functional>

/**
 * Tip packages[].directoryname for variant `regular` (CipSoft-shaped packages/<name>/).
 * Stock default is CipSoft's `TIBIA_CLIENT_PACKAGE_DIR` (`Tibia.app`).
 * Product tip packs override at runtime via mods/<id>/brand.json
 * (`LauncherBrand::clientPackageDir`). Must match portal PACKAGE_CLIENT.
 */
#ifndef TIBIA_CLIENT_PACKAGE_DIR
#define TIBIA_CLIENT_PACKAGE_DIR "Tibia.app"
#endif
inline constexpr const char* kClientPackageDir = TIBIA_CLIENT_PACKAGE_DIR;

/** CipSoft tip packages[].variant values. */
inline constexpr const char* kVariantRegular = "regular";
inline constexpr const char* kVariantPreview = "preview";
inline constexpr const char* kVariantTest = "test";
inline constexpr const char* kVariantBeta = "beta";

/** One CIP package.json / assets.json file record. */
struct CipFile
{
    QString url;
    QString localfile;
    QString packedhash;
    qint64 packedsize = 0;
    QString unpackedhash;
    qint64 unpackedsize = 0;
};

struct CipBuild
{
    QString os;
    QString architecture;
    QUrl packageUrl;
    QUrl versionUrl;
    QUrl assetsUrl;
    QUrl assetsVersionUrl;
    /// Optional tip pack channel (mods/<id>/ under the client package).
    /// Same shape as assets: mods.json + mods.json.sha256. Empty = stock CIP.
    QUrl modsUrl;
    QUrl modsVersionUrl;
};

struct CipPackageInfo
{
    QString name;
    QString variant;
    QString directoryname;
    QString logolinktarget;
    bool installautomatically = false;
    QVector<CipBuild> builds;
};

struct CipMetadata
{
    QVector<CipBuild> launcherBuilds;
    QVector<CipPackageInfo> packages;
    QUrl hintsUrl;
};

/**
 * Pick packages[] by QTCLIENT_VARIANT (regular|preview|test|beta),
 * then regular, then first. Returns nullptr when the tip has no packages.
 */
inline const CipPackageInfo* selectPackage(const CipMetadata& metadata)
{
    if (metadata.packages.isEmpty())
        return nullptr;
    QString want = qEnvironmentVariable("QTCLIENT_VARIANT", QString::fromLatin1(kVariantRegular));
    want = want.trimmed().toLower();
    if (want.isEmpty())
        want = QString::fromLatin1(kVariantRegular);
    for (const CipPackageInfo& info : metadata.packages) {
        if (info.variant.toLower() == want)
            return &info;
    }
    for (const CipPackageInfo& info : metadata.packages) {
        if (info.variant.toLower() == QLatin1String(kVariantRegular))
            return &info;
    }
    return &metadata.packages.first();
}

struct CipPackageDoc
{
    QString version;
    QString executable;
    QString generation;
    QVector<CipFile> files;
};

class QNetworkAccessManager;

/** CIP launcher protocol: tibiametadata.json, package.json, lzma files. */
class PackageClient : public QObject
{
    Q_OBJECT

public:
    explicit PackageClient(QObject* parent = nullptr);

    /** CDN bundle root, no trailing slash (hints / content store). */
    void setCdnBase(const QString& base);
    QString cdnBase() const { return m_cdn; }

    /**
     * Full URL to tibiametadata.json. Empty → cdnBase + "/launcher/tibiametadata.json".
     * Public / CIP-faithful builds set this to https://static.tibia.com/launcher/tibiametadata.json
     * (or a demo tip) via QTCLIENT_TIP_URL.
     */
    void setTipUrl(const QUrl& url);
    QUrl tipUrl() const;

    /** App support directory that holds launchermetadata.json and packages/. */
    void setDataDir(const QString& dir);
    QString dataDir() const { return m_dataDir; }

    QString hostOs() const;
    QString hostArch() const;

    CipMetadata fetchMetadata(QString* error);
    QString fetchVersion(const QUrl& versionUrl, QString* error);
    CipPackageDoc fetchPackage(const QUrl& packageUrl, QString* error);

    /// Non-blocking GET for the browser main thread (no nested QEventLoop).
    using GetCallback = std::function<void(const QByteArray& body, const QString& error)>;
    using InstallCallback = std::function<void(bool ok, const QString& error)>;
    void getAsync(const QUrl& url, GetCallback done);
    CipMetadata parseMetadata(const QByteArray& body, QString* error) const;
    CipPackageDoc parsePackage(const QByteArray& body, QString* error) const;
    /// Parse CIP assets.json or tip mods.json (`files` array; same shape).
    QVector<CipFile> fetchAssetFiles(const QUrl& assetsUrl, QString* error);
    QVector<CipFile> parseAssetFiles(const QByteArray& body, QString* error) const;
    QString fetchHints(const QUrl& hintsUrl, QString* error);
    QVector<tibia::app::Hint> fetchHintList(const QUrl& hintsUrl, QString* error);

    /**
     * CIP gap check: true when any package.json file is missing or its unpacked
     * sha256/size does not match the installed copy (skip-if-hash-match).
     */
    bool hasFileGaps(const CipPackageDoc& doc, const QString& destDir) const;

    /**
     * Download and unpack every file into destDir. Writes partial.package.json.version
     * while running, then package.json and package.json.version.
     */
    bool installPackage(const QUrl& packageUrl, const CipPackageDoc& doc, const QString& destDir, QString* error,
                        qint64 progressBase = 0, qint64 progressTotal = -1);
    /** Same as installPackage, but chains getAsync (required on the WASM main thread). */
    void installPackageAsync(const QUrl& packageUrl, const CipPackageDoc& doc, const QString& destDir,
                             InstallCallback done, qint64 progressBase = 0, qint64 progressTotal = -1);
    bool installAssets(const QUrl& assetsUrl, const QVector<CipFile>& files, const QString& assetsVersion,
                       const QString& destDir, QString* error, qint64 progressBase = 0, qint64 progressTotal = -1);
    /** Same as installAssets, but chains getAsync (required on the WASM main thread). */
    void installAssetsAsync(const QUrl& assetsUrl, const QVector<CipFile>& files, const QString& assetsVersion,
                            const QString& destDir, InstallCallback done, qint64 progressBase = 0,
                            qint64 progressTotal = -1);
    /**
     * Tip product pack into destDir (localfile paths under mods/<id>/…).
     * Writes mods.json.sha256. No-op channel when modsUrl is empty.
     */
    bool installMods(const QUrl& modsUrl, const QVector<CipFile>& files, const QString& modsVersion,
                     const QString& destDir, QString* error, qint64 progressBase = 0, qint64 progressTotal = -1);
    void installModsAsync(const QUrl& modsUrl, const QVector<CipFile>& files, const QString& modsVersion,
                          const QString& destDir, InstallCallback done, qint64 progressBase = 0,
                          qint64 progressTotal = -1);
    /** Packed bytes still needed for files not already installed (progress totals). */
    qint64 pendingPackedBytes(const QVector<CipFile>& files, const QString& destDir) const;

    static const CipBuild* matchBuild(const QVector<CipBuild>& builds, const QString& os, const QString& arch);
    static QString sha256Hex(const QByteArray& data);
    static QString sha256File(QFile& file);

Q_SIGNALS:
    void progress(qint64 bytes, qint64 totalBytes, int fileIndex, int fileCount);

private:
    QByteArray get(const QUrl& url, QString* error);
    QUrl resolve(const QUrl& base, const QString& url) const;
    /**
     * Product CDN only: rewrite /launcher/ onto m_cdn and fetch
     * content/<aa>/<packedhash>. Foreign tips (e.g. static.tibia.com) stay
     * tip-relative — CIP launcher protocol.
     */
    bool useCdnContentStore() const;
    QUrl fileDownloadUrl(const QUrl& packageUrl, const CipFile& file) const;
    /**
     * Download pool. progressBase/progressTotal span a multi-phase install
     * (client package then assets) so the bar does not reset to 0 between phases.
     * progressTotal <= 0 → use this batch's packed byte sum only.
     */
    bool installFiles(const QUrl& packageUrl, const QVector<CipFile>& files, const QString& destDir, QString* error,
                      qint64 progressBase = 0, qint64 progressTotal = -1);
    void installFilesAsync(const QUrl& packageUrl, const QVector<CipFile>& files, const QString& destDir, int index,
                           qint64 donePacked, qint64 totalPacked, InstallCallback done);
    bool writePackageMarkers(const CipPackageDoc& doc, const QString& destDir, QString* error);
    /** True when localfile already matches unpacked hash/size (CIP skip-if-hash-match). */
    bool fileAlreadyInstalled(const CipFile& file, const QString& destDir) const;
    QByteArray getForInstall(const QUrl& url, qint64 donePacked, qint64 totalPacked, int fileIndex, int fileCount,
                             QString* error);

    QNetworkAccessManager* m_nam = nullptr;
    QString m_cdn;
    QUrl m_tipUrl;
    QString m_dataDir;
    /** When true, get/getAsync emit progress (install downloads only). */
    bool m_emitProgress = false;
    qint64 m_progressDone = 0;
    qint64 m_progressTotal = 0;
    int m_progressFileIndex = 0;
    int m_progressFileCount = 0;
};
