#pragma once

#include "app/hints.h"
#include "package.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include <functional>

class QStackedLayout;

class QCheckBox;
class QGraphicsProxyWidget;
class QGraphicsView;
class QGroupBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QWidget;
#ifndef __EMSCRIPTEN__
class QProcess;
#endif

namespace shared {
class QTextBrowserWithNetwork;
}

class LauncherWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LauncherWindow(PackageClient* client, QWidget* parent = nullptr);

    void start();
    /**
     * Optional Play hook (tests). Production wasm Play remounts into a game-only
     * Module (sessionStorage qtclient.play + location.reload). Desktop spawn is unchanged.
     */
    void setInProcessPlayHandler(std::function<void()> handler) { m_inProcessPlay = std::move(handler); }
#ifdef __EMSCRIPTEN__
    /** Called from JS after `__qtclientInstallThings` finishes (EM_ASM keepalive). */
    void onThingsInstallDone(bool ok, bool thenPlay, const QString& version);
#endif

private:
    enum class LogoState { Classic, Updating, Launching };

    void applyStyle();
    void paintBackground();
    void acceptEula();
    void setActionSkin(const QString& color, const QString& icon);
    void setLogoState(LogoState state);
#ifdef __EMSCRIPTEN__
    void resizeEvent(QResizeEvent* event) override;
    void applyThingsProgressFromJs();
    void layoutSoftStage();
#endif
    void applyHint(const QVector<tibia::app::Hint>& hints);
    void loadFeed();
    void refresh();
    void setBusy(bool busy, const QString& label);
    void onProgress(qint64 bytes, qint64 total, int fileIndex, int fileCount);
    void onAction();
    void installIfNeeded(bool thenPlay);
#ifdef __EMSCRIPTEN__
    void startAsync();
    void installIfNeededAsync(bool thenPlay);
    /** After engine package: CIP assets stamp + product things packs into OPFS tip. */
    void installAssetsAndThingsAsync(bool thenPlay);
    /** After assets/things: optional tip mods channel, then finish. */
    void installModsThenFinishAsync(bool thenPlay, const QString& version);
    void finishInstallAsync(bool thenPlay, const QString& version);
    void installThingsPacksViaJs(bool thenPlay, const QString& version);
#endif
    void play();
    /// Local DEV: `QTCLIENT_DEV_JSON` desktop.exe (stock --no-mods / tip pack).
    bool tryPlayDevClient();
    void spawn(const QString& executable, const QString& workDir, const QString& revision,
               const QStringList& clientArgs = {});
    void considerSelfUpdate(const CipMetadata& metadata);
    QString packageDir() const;
    QString localVersion() const;
    QString localAssetsStamp() const;
    QString localModsStamp() const;
    bool thingsPacksStampPresent() const;
    /// True when mods channel omitted or local mods.json.sha256 matches remote.
    bool modsStampOk() const;
    /// CipSoft install vocabulary: Unknown → PartiallyInstalled → InstalledAndUsable.
    QString installStatusLabel() const;
    void fetchRemoteStampsParallel();

    PackageClient* m_client = nullptr;
    std::function<void()> m_inProcessPlay;
    CipPackageInfo m_package;
    CipBuild m_build;
    CipPackageDoc m_packageDoc;
    QUrl m_hintsUrl;
    QString m_remoteVersion;
    QString m_remoteAssetsVersion;
    QString m_remoteModsVersion;
    bool m_hasGaps = false;
    bool m_busy = false;
    QElapsedTimer m_timer;

    QStackedLayout* m_stack = nullptr;
    shared::QTextBrowserWithNetwork* m_news = nullptr;
    QLabel* m_logo = nullptr;
    QLabel* m_version = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_buildLabel = nullptr;
    QLabel* m_mark = nullptr;
    QCheckBox* m_defaultClient = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_action = nullptr;
    QGroupBox* m_packageBox = nullptr;
    QGroupBox* m_hintBox = nullptr;
#ifdef __EMSCRIPTEN__
    QWidget* m_softStage = nullptr;
    QGraphicsView* m_stageView = nullptr;
    QGraphicsProxyWidget* m_stageProxy = nullptr;
#endif
#ifndef __EMSCRIPTEN__
    QPointer<QProcess> m_game;
#endif
};
