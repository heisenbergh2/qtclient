#include "window.h"

#include "brand.h"
#include "textbrowserwithnetwork.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QUrl>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QTextEdit>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcLauncher, "tibia.launcher")

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <cstdlib>
#endif

namespace {

/// CipSoft mock: `ETA hh:mm:ss XXXX kB/s XXX/XXX mB` — fixed columns so the
/// package chrome does not reflow as digits change (and does not wrap).
QString formatEta(qint64 bytes, qint64 total, qint64 elapsedMs)
{
    const double seconds = qMax(elapsedMs, qint64(1)) / 1000.0;
    const double rate = bytes / seconds;
    const qint64 left = rate > 1.0 && total > bytes ? qint64((total - bytes) / rate) : 0;
    const qint64 hh = left / 3600;
    const qint64 mm = (left % 3600) / 60;
    const qint64 ss = left % 60;
    auto mb = [](qint64 n) {
        const double v = n / 1000000.0;
        return QString::number(v, 'f', v >= 100.0 ? 0 : 1);
    };
    const qint64 rateKb = qMax(qint64(0), qint64(rate / 1000.0));
    return QStringLiteral("ETA %1:%2:%3 %4 kB/s %5/%6 mB")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(rateKb, 4, 10, QLatin1Char(' '))
        .arg(mb(bytes), 5, QLatin1Char(' '))
        .arg(mb(total > 0 ? total : bytes), 5, QLatin1Char(' '));
}

/// CIP ProgressBar_BG.png is 106×42.
constexpr int kProgressBarWidth = 106;
constexpr int kProgressBarHeight = 42;
/// Classic logo is 240×96; CIP Updating/Launching are 196×152 — pin one box.
constexpr int kLogoWidth = 240;
constexpr int kLogoHeight = 152;
/// Wide enough for the padded ETA line at Verdana 13 bold (no wrap).
constexpr int kPackageBoxWidth = 280;
/// CipSoft stage (TLauncherMainWindow). Browser fills the host and centers this.
constexpr int kSoftStageWidth = 800;
constexpr int kSoftStageHeight = 600;

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

QString vendoredStyle(const char* name)
{
    QFile file(QStringLiteral(":/styles/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString css = QString::fromUtf8(file.readAll());
    // qtclient qrc chrome; tip pack brand.json can replace BackgroundImage / logos.
    const LauncherBrand& brand = LauncherBrand::instance();
    if (!brand.packRoot.isEmpty()) {
        for (const char* img : {"BackgroundImage.jpg", "TibiaLogo_Classic.png", "CipSoftLogo.png"}) {
            const QString disk = brand.chromePath(QLatin1String(img));
            if (disk.isEmpty())
                continue;
            css.replace(QStringLiteral(":/images/launcher/") + QLatin1String(img),
                        QUrl::fromLocalFile(disk).toString());
        }
    }
    return css;
}

/// CIP footer left label: trailing hex of `15.33.c9e45e` → `c9e45e`.
QString shortVersionHash(const QString& version)
{
    if (version.isEmpty())
        return {};
    const int dot = version.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < version.size())
        return version.mid(dot + 1);
    const int dash = version.lastIndexOf(QLatin1Char('-'));
    if (dash >= 0 && dash + 1 < version.size())
        return version.mid(dash + 1);
    return version;
}

bool readDevDesktop(QString* exeOut, QString* cwdOut, QString* revisionOut)
{
#ifndef __EMSCRIPTEN__
    const QString path = qEnvironmentVariable("QTCLIENT_DEV_JSON");
    if (path.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject desktop = QJsonDocument::fromJson(file.readAll()).object()
                                    .value(QStringLiteral("desktop"))
                                    .toObject();
    const QString exe = desktop.value(QStringLiteral("exe")).toString();
    if (exe.isEmpty() || !QFileInfo::exists(exe))
        return false;
    if (exeOut)
        *exeOut = exe;
    if (cwdOut)
        *cwdOut = desktop.value(QStringLiteral("cwd")).toString();
    if (revisionOut)
        *revisionOut = desktop.value(QStringLiteral("revision")).toString();
    return true;
#else
    Q_UNUSED(exeOut);
    Q_UNUSED(cwdOut);
    Q_UNUSED(revisionOut);
    return false;
#endif
}

/// Monorepo `make qtclient launcher run` — Play uses QTCLIENT_DEV_JSON, not tip AppData.
bool devPlayAvailable()
{
    return readDevDesktop(nullptr, nullptr, nullptr);
}

bool cdnIsLoopback(const QString& cdn)
{
    const QUrl url(cdn);
    if (!url.isValid())
        return false;
    const QString host = url.host();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
        || host == QLatin1String("::1");
}


} // namespace

LauncherWindow::LauncherWindow(PackageClient* client, QWidget* parent)
    : QMainWindow(parent)
    , m_client(client)
{
    // TLauncherMainWindow: width 800, maximum height 600, background stylesheet
    // on the window. The package column, hint browser and footer are the same
    // widgets CIP builds; the browser build keeps this window.
    setWindowTitle(LauncherBrand::instance().productName);
#ifdef __EMSCRIPTEN__
    // Desktop stays CIP 800×600. Browser: fill the portal host (no fixed size —
    // Qt WASM maps QScreen to #tibia-screen). soft-stage scales CIP chrome to
    // fit; do not force 640×480 — that dwarfs phone CSS widths and leaves a
    // postage-stamp launcher after the canvas is letterboxed.
    setMinimumSize(0, 0);
    if (QScreen* s = screen())
        resize(s->size());
#else
    // CipSoft TLauncherMainWindow: fixed 800×600 chrome (browser soft-stage
    // scales that same layout for mobile — see layoutSoftStage).
    setFixedSize(800, 600);
#endif
    auto* central = new QWidget(this);
    setCentralWidget(central);

    m_logo = new QLabel;
    m_logo->setAlignment(Qt::AlignCenter);
    m_logo->setFixedSize(kLogoWidth, kLogoHeight);
    setLogoState(LogoState::Classic);

    const QPixmap buttonPixmap(QStringLiteral(":/images/launcher/Button_Blue_idle.png"));
    m_action = new QPushButton;
    m_action->setFixedSize(buttonPixmap.isNull() ? QSize(150, 37) : buttonPixmap.size());
    m_action->setCursor(Qt::PointingHandCursor);
    m_action->setFocusPolicy(Qt::NoFocus);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFixedSize(kProgressBarWidth, kProgressBarHeight);
    m_progress->setStyleSheet(vendoredStyle("progress.qss"));
    m_progress->hide();
    // Host keeps CIP's 106×42 slot even while the bar is hidden so Update → Play
    // does not reflow the action button.
    auto* progressHost = new QWidget;
    progressHost->setFixedSize(kProgressBarWidth, kProgressBarHeight);
    auto* progressLayout = new QVBoxLayout(progressHost);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(0);
    progressLayout->addWidget(m_progress, 0, Qt::AlignCenter);

    const QString labelStyle = vendoredStyle("label-13.qss");
    m_version = new QLabel;
    m_version->setStyleSheet(labelStyle);
    m_version->setAlignment(Qt::AlignCenter);
    // CIP shows a single short line (15.33.c9e45e). Wrapping a long stamp was
    // painting the first line into the Play button on WASM.
    m_version->setWordWrap(false);
    m_version->setMinimumHeight(16);
    m_status = new QLabel;
    m_status->setStyleSheet(labelStyle);
    m_status->setAlignment(Qt::AlignCenter);
    // CIP ETA is one line; wrapping stretched the package box and moved Play.
    m_status->setWordWrap(false);
    m_status->setFixedHeight(18);
    m_status->setMinimumWidth(kPackageBoxWidth - 24);

    m_defaultClient = new QCheckBox(QStringLiteral("Default Client"));
    m_defaultClient->setChecked(QSettings().value(QStringLiteral("defaultClient"), true).toBool());
    m_defaultClient->setStyleSheet(vendoredStyle("checkbox.qss"));
    connect(m_defaultClient, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("defaultClient"), on);
    });

    m_packageBox = new QGroupBox;
    m_packageBox->setAlignment(Qt::AlignHCenter);
    m_packageBox->setStyleSheet(vendoredStyle("package-group.qss"));
    m_packageBox->setFixedWidth(kPackageBoxWidth);
    auto* pack = new QVBoxLayout(m_packageBox);
    pack->setSpacing(2);
    pack->setContentsMargins(10, 10, 10, 10);
    pack->addWidget(m_action, 0, Qt::AlignHCenter);
    // CIP keeps a clear gap under Play before `15.33.c9e45e`.
    pack->addSpacing(12);
    pack->addWidget(m_version, 0, Qt::AlignHCenter);
    pack->addSpacing(6);
    pack->addWidget(m_defaultClient, 0, Qt::AlignHCenter);
    pack->addWidget(progressHost, 0, Qt::AlignHCenter);
    pack->addWidget(m_status, 0, Qt::AlignHCenter);

    auto* packageRow = new QHBoxLayout;
    packageRow->addStretch(1);
    auto* packageColumn = new QVBoxLayout;
    packageColumn->addWidget(m_logo, 0, Qt::AlignHCenter);
    packageColumn->addSpacing(6);
    packageColumn->addWidget(m_packageBox, 0, Qt::AlignHCenter | Qt::AlignBottom);
    packageRow->addLayout(packageColumn, 2);
    packageRow->addStretch(1);

    // CIP shared::QTextBrowserWithNetwork — remote hint <img> via Network.
    m_news = new shared::QTextBrowserWithNetwork;
    m_news->setOpenExternalLinks(true);
    m_news->setContextMenuPolicy(Qt::NoContextMenu);
    m_news->setFixedWidth(542);
    m_news->setFixedHeight(110);
    m_news->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_news->viewport()->setAutoFillBackground(false);
    m_news->document()->setDefaultStyleSheet(vendoredStyle("textbrowser-links.qss"));
    m_news->setStyleSheet(vendoredStyle("textbrowser.qss"));
    m_news->setHtml(QStringLiteral("<p>Loading launcher package configuration…</p>"));

    m_hintBox = new QGroupBox(QStringLiteral("Hint"));
    m_hintBox->setAlignment(Qt::AlignHCenter);
    m_hintBox->setStyleSheet(vendoredStyle("hint-group.qss"));
    m_hintBox->setFixedWidth(560);
    auto* hintLayout = new QVBoxLayout(m_hintBox);
    // CIP hint-group.qss already insets the title via border-image (17px top).
    hintLayout->setContentsMargins(6, 2, 6, 6);
    hintLayout->addWidget(m_news);

    // CIP footer row: short hash | mark | About — one baseline.
    m_buildLabel = new QLabel;
    m_buildLabel->setStyleSheet(vendoredStyle("label-11.qss"));
    m_buildLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    const LauncherBrand& brand = LauncherBrand::instance();
    const QString markUrl = brand.chromeUrl(QStringLiteral("CipSoftLogo.png"));
    const QPixmap markPixmap(brand.chromePath(QStringLiteral("CipSoftLogo.png")).isEmpty()
                                 ? QStringLiteral(":/images/launcher/CipSoftLogo.png")
                                 : brand.chromePath(QStringLiteral("CipSoftLogo.png")));
    m_mark = new QLabel;
    m_mark->setTextFormat(Qt::RichText);
    m_mark->setOpenExternalLinks(true);
    m_mark->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    if (!markPixmap.isNull()) {
        m_mark->setFixedSize(markPixmap.size());
        m_mark->setText(QStringLiteral("<a href=\"%1\"><img src=\"%2\"></a>")
                            .arg(brand.productUrl, markUrl));
    } else {
        m_mark->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                            .arg(brand.productUrl, brand.productName));
    }

    auto* about = new QLabel(QStringLiteral(
        "<a href=\"about\" style=\"color: #909090; text-decoration: none;\">About</a>"));
    about->setStyleSheet(vendoredStyle("label-11.qss"));
    about->setTextFormat(Qt::RichText);
    about->setTextInteractionFlags(Qt::TextBrowserInteraction);
    about->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    connect(about, &QLabel::linkActivated, this, [this] {
        const LauncherBrand& b = LauncherBrand::instance();
        QMessageBox::about(this, QStringLiteral("About"),
                           b.productName + QStringLiteral(" launcher\n\nSource code, including modifications, at the ")
                               + b.productName + QStringLiteral(" repository."));
    });

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(12, 0, 12, 0);
    footer->setSpacing(0);
    footer->addWidget(m_buildLabel, 0, Qt::AlignVCenter);
    footer->addStretch(1);
    footer->addWidget(m_mark, 0, Qt::AlignVCenter);
    footer->addStretch(1);
    footer->addWidget(about, 0, Qt::AlignVCenter);

    auto* mainPage = new QWidget;
    auto* mainLayout = new QVBoxLayout(mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addSpacing(11);
    mainLayout->addLayout(packageRow);
    mainLayout->addSpacing(6);
    mainLayout->addWidget(m_hintBox, 0, Qt::AlignHCenter);
    mainLayout->addStretch(1);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(footer);
    mainLayout->addSpacing(8);

    auto* eula = new QTextEdit;
    eula->setReadOnly(true);
    eula->setWordWrapMode(QTextOption::WordWrap);
    eula->setFixedHeight(330);
    eula->setStyleSheet(vendoredStyle("textedit.qss"));
    QFile eulaFile(QStringLiteral(":/documents/tibia-eula.md"));
    if (eulaFile.open(QIODevice::ReadOnly))
        eula->setMarkdown(QString::fromUtf8(eulaFile.readAll()));
    auto* accept = new QPushButton(QStringLiteral("Accept"));
    connect(accept, &QPushButton::clicked, this, &LauncherWindow::acceptEula);
    auto* eulaPage = new QWidget;
    auto* eulaLayout = new QVBoxLayout(eulaPage);
    eulaLayout->addSpacing(11);
    eulaLayout->addWidget(eula);
    eulaLayout->addWidget(accept, 0, Qt::AlignHCenter);

    m_stack = new QStackedLayout;
    m_stack->addWidget(eulaPage);
    m_stack->addWidget(mainPage);
#ifdef __EMSCRIPTEN__
    // /play is behind the portal; skip the CIP desktop EULA dialog so the
    // package chrome (logo, Download/Play, hints) is what players see first.
    const bool accepted = true;
#else
    const bool accepted = QSettings().value(QStringLiteral("eulaAccepted")).toBool();
#endif
    m_stack->setCurrentIndex(accepted || eula->toPlainText().isEmpty() ? 1 : 0);

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
#ifdef __EMSCRIPTEN__
    // CipSoft chrome is authored at 800×600. Embed it in a QGraphicsView so
    // narrow/mobile hosts can uniform-scale + center without reflowing CIP metrics.
    m_softStage = new QWidget;
    m_softStage->setFixedSize(kSoftStageWidth, kSoftStageHeight);
    m_softStage->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* stageLayout = new QVBoxLayout(m_softStage);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(0);
    stageLayout->addLayout(m_stack);

    m_stageView = new QGraphicsView(central);
    m_stageView->setFrameShape(QFrame::NoFrame);
    m_stageView->setStyleSheet(QStringLiteral(
        "QGraphicsView, QGraphicsView > QWidget { background: transparent; border: none; }"));
    m_stageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_stageView->setAlignment(Qt::AlignCenter);
    m_stageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stageView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_stageView->setBackgroundBrush(Qt::NoBrush);
    m_stageView->viewport()->setAutoFillBackground(false);
    m_stageView->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* scene = new QGraphicsScene(m_stageView);
    scene->setBackgroundBrush(Qt::NoBrush);
    m_stageView->setScene(scene);
    m_stageProxy = scene->addWidget(m_softStage);
    m_stageProxy->setTransformOriginPoint(0, 0);
    root->addWidget(m_stageView);
    layoutSoftStage();
    // First layout often runs before Qt WASM assigns the real CSS host size.
    QTimer::singleShot(0, this, &LauncherWindow::layoutSoftStage);
    QTimer::singleShot(50, this, &LauncherWindow::layoutSoftStage);
    if (QScreen* s = screen())
        connect(s, &QScreen::geometryChanged, this, &LauncherWindow::layoutSoftStage);
#else
    root->addLayout(m_stack);
#endif

    connect(m_action, &QPushButton::clicked, this, &LauncherWindow::onAction);
    connect(m_client, &PackageClient::progress, this, &LauncherWindow::onProgress);
    applyStyle();
    // CipSoft does not flash Download before metadata. DEV Play is known from
    // QTCLIENT_DEV_JSON before tip stamps load — show Green Play immediately.
    // Otherwise leave the action disabled until refresh() knows NotInstalled vs Play.
    if (devPlayAvailable()) {
        m_status->setText(LauncherBrand::instance().noMods ? QStringLiteral("Dev (qtclient)")
                                                           : QStringLiteral("Dev (pack)"));
        setActionSkin(QStringLiteral("Green"), QStringLiteral("Play"));
        m_action->setEnabled(true);
    } else {
        m_action->setEnabled(false);
        m_action->setText({});
    }
}

void LauncherWindow::acceptEula()
{
    QSettings().setValue(QStringLiteral("eulaAccepted"), true);
    if (m_stack)
        m_stack->setCurrentIndex(1);
}

void LauncherWindow::paintBackground()
{
#ifdef __EMSCRIPTEN__
    // Window border-image is unreliable on Qt WASM OpenGL; paint the CIP
    // background on the soft stage (800×600) and keep the host letterbox clear.
    const LauncherBrand& brand = LauncherBrand::instance();
    const QString bgPath = brand.chromePath(QStringLiteral("BackgroundImage.jpg"));
    const QPixmap bg(bgPath.isEmpty() ? QStringLiteral(":/images/launcher/BackgroundImage.jpg")
                                      : bgPath);
    if (bg.isNull())
        return;
    if (m_softStage) {
        QPalette stagePal = m_softStage->palette();
        stagePal.setBrush(QPalette::Window,
                          QBrush(bg.scaled(kSoftStageWidth, kSoftStageHeight, Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation)));
        m_softStage->setAutoFillBackground(true);
        m_softStage->setPalette(stagePal);
    }
    // Letterbox behind the transparent QGraphicsView — same art, host-sized.
    const QSize target = size().isEmpty() ? QSize(kSoftStageWidth, kSoftStageHeight) : size();
    QPalette pal = palette();
    pal.setBrush(QPalette::Window,
                 QBrush(bg.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    setAutoFillBackground(true);
    setPalette(pal);
#endif
}

void LauncherWindow::applyStyle()
{
#ifdef __EMSCRIPTEN__
    paintBackground();
    // CIP package-group.qss uses negative padding that collapses the version
    // onto Play when border-image metrics differ on WASM — same chrome image,
    // positive padding only.
    m_packageBox->setStyleSheet(QStringLiteral(
        "QGroupBox {"
        "  border-image: url(:/images/launcher/clientpackage-background.png) 4 4 4 4 repeat;"
        "  border-top: 4px transparent;"
        "  border-left: 4px transparent;"
        "  border-bottom: 4px transparent;"
        "  border-right: 4px transparent;"
        "  background-attachment: fixed;"
        "  padding-top: 10px;"
        "  padding-left: 8px;"
        "  padding-bottom: 10px;"
        "  padding-right: 8px;"
        "}"));
    m_hintBox->setStyleSheet(vendoredStyle("hint-group.qss"));
    m_version->setMinimumHeight(18);
#else
    setStyleSheet(vendoredStyle("window.qss"));
#endif
}

#ifdef __EMSCRIPTEN__
void LauncherWindow::layoutSoftStage()
{
    if (!m_stageView || !m_stageProxy || !m_softStage)
        return;
    QSize host = m_stageView->viewport()->size();
    // Qt WASM viewport can lag (or report device px) vs the CSS host on mobile
    // Safari. Prefer the portal screen element's client box when available.
    const int cssW = EM_ASM_INT({
        var el = document.getElementById('tibia-screen')
            || document.getElementById('play-stage');
        return el ? (el.clientWidth | 0) : 0;
    });
    const int cssH = EM_ASM_INT({
        var el = document.getElementById('tibia-screen')
            || document.getElementById('play-stage');
        return el ? (el.clientHeight | 0) : 0;
    });
    if (cssW >= 8 && cssH >= 8)
        host = QSize(cssW, cssH);
    if (host.width() < 8 || host.height() < 8)
        return;
    // Uniform scale to fill the host. Cap upscale at 2× so large tablets are
    // readable without blurring CIP art into mush on desktop-sized browsers.
    const qreal sx = qreal(host.width()) / qreal(kSoftStageWidth);
    const qreal sy = qreal(host.height()) / qreal(kSoftStageHeight);
    const qreal scale = qMin(qreal(2), qMin(sx, sy));
    m_stageProxy->setScale(scale);
    const qreal w = kSoftStageWidth * scale;
    const qreal h = kSoftStageHeight * scale;
    m_stageProxy->setPos((host.width() - w) * 0.5, (host.height() - h) * 0.5);
    m_stageView->scene()->setSceneRect(0, 0, host.width(), host.height());
}

void LauncherWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    paintBackground();
    layoutSoftStage();
}

void LauncherWindow::applyThingsProgressFromJs()
{
    // Module.qtclientThingsProgress = { label, percent } from __qtclientInstallThings.
    char* labelPtr = static_cast<char*>(EM_ASM_PTR({
        var p = Module.qtclientThingsProgress;
        if (!p || typeof p.label !== 'string')
            return 0;
        var s = p.label;
        var len = lengthBytesUTF8(s) + 1;
        var buf = _malloc(len);
        stringToUTF8(s, buf, len);
        return buf;
    }));
    const int percent = EM_ASM_INT({
        var p = Module.qtclientThingsProgress;
        if (!p || typeof p.percent !== 'number')
            return -1;
        return Math.max(0, Math.min(100, p.percent | 0));
    });
    if (labelPtr) {
        m_status->setText(QString::fromUtf8(labelPtr));
        free(labelPtr);
    }
    if (percent >= 0) {
        m_progress->show();
        m_progress->setValue(percent);
    }
}
#endif

void LauncherWindow::setActionSkin(const QString& color, const QString& icon)
{
    const QString image = icon.isEmpty()
        ? QString()
        : QStringLiteral("image: url(:/images/launcher/Icon_%1.png);").arg(icon);
    m_action->setStyleSheet(QStringLiteral(
        "QPushButton { background-image: url(:/images/launcher/Button_%1_idle.png); %2 border: none; }"
        "QPushButton:hover { background-image: url(:/images/launcher/Button_%1_hover.png); }"
        "QPushButton:pressed { background-image: url(:/images/launcher/Button_%1_pressed.png);"
        " padding-top: 1px; padding-left: 1px; padding-bottom: -1px; padding-right: -1px; }"
        "QPushButton:disabled { background-image: url(:/images/launcher/Button_%1_idle.png); }")
        .arg(color, image));
}

void LauncherWindow::setLogoState(LogoState state)
{
    if (!m_logo)
        return;
    // Fixed logo box (see kLogoWidth/Height) — CIP Updating/Launching are taller
    // than Classic; resizing the pixmap box was jumping Play.
    m_logo->setFixedSize(kLogoWidth, kLogoHeight);
    const LauncherBrand& brand = LauncherBrand::instance();
    QString file = QStringLiteral("TibiaLogo_Classic.png");
    if (state == LogoState::Updating)
        file = QStringLiteral("TibiaLogo_UpdatingLauncher.png");
    else if (state == LogoState::Launching)
        file = QStringLiteral("TibiaLogo_LaunchingClient.png");
    const QString disk = brand.chromePath(file);
    const QString path = disk.isEmpty() ? (QStringLiteral(":/images/launcher/") + file) : disk;
    const QPixmap pixmap(path);
    if (!pixmap.isNull()) {
        m_logo->setPixmap(pixmap.scaled(kLogoWidth, kLogoHeight, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
        m_logo->setText({});
        return;
    }
    if (state == LogoState::Classic)
        m_logo->setText(brand.productName);
}

QString LauncherWindow::packageDir() const
{
    // CipSoft: packages/<directoryname>/ from the selected tip package
    // (regular → tip directoryname / TIBIA_CLIENT_PACKAGE_DIR).
    QString dirName = m_package.directoryname;
    if (dirName.isEmpty())
        dirName = LauncherBrand::instance().clientPackageDir;
    return QDir(m_client->dataDir()).filePath(QStringLiteral("packages/") + dirName);
}

QString LauncherWindow::localVersion() const
{
    const QString ver = readText(QDir(packageDir()).filePath(QStringLiteral("package.json.version")));
    if (ver.isEmpty())
        return {};
#ifdef __EMSCRIPTEN__
    // Stamp-only installs wrote the version without unpacking files — treat as missing
    // so Download runs a real install with progress instead of jumping to Play.
    const QDir dir(packageDir());
    if (!QFileInfo::exists(dir.filePath(QStringLiteral("tibia.js")))
        && !QFileInfo::exists(dir.filePath(QStringLiteral("tibia.wasm"))))
        return {};
#endif
    return ver;
}

QString LauncherWindow::localAssetsStamp() const
{
    return readText(QDir(packageDir()).filePath(QStringLiteral("assets.json.sha256")));
}

QString LauncherWindow::localModsStamp() const
{
    return readText(QDir(packageDir()).filePath(QStringLiteral("mods.json.sha256")));
}

bool LauncherWindow::thingsPacksStampPresent() const
{
    const QDir tip(packageDir());
    // Canonical stamp (portal + CIP-style); accept legacy product name once.
    return QFileInfo::exists(tip.filePath(QStringLiteral(".tibia-sprite-packs")))
        || QFileInfo::exists(tip.filePath(QStringLiteral(".idleot-sprite-packs")));
}

bool LauncherWindow::modsStampOk() const
{
    // Empty remote = channel omitted (stock CIP) or stamp not fetched yet.
    return m_remoteModsVersion.isEmpty() || localModsStamp() == m_remoteModsVersion;
}

QString LauncherWindow::installStatusLabel() const
{
    if (localVersion().isEmpty())
        return QStringLiteral("NotInstalled");
    const bool assetsOk = m_remoteAssetsVersion.isEmpty()
        || localAssetsStamp() == m_remoteAssetsVersion;
    const bool versionOk = m_remoteVersion.isEmpty() || localVersion() == m_remoteVersion;
#ifdef __EMSCRIPTEN__
    const bool thingsOk = thingsPacksStampPresent();
#else
    const bool thingsOk = true;
#endif
    // CIP: PackageBeingInstalled / InstallationInProgress while busy; otherwise
    // PartiallyInstalled until stamps + files match → InstalledAndUsable.
    if (m_busy)
        return QStringLiteral("InstallationInProgress");
    if (!versionOk || m_hasGaps || !assetsOk || !thingsOk || !modsStampOk())
        return QStringLiteral("PartiallyInstalled");
    return QStringLiteral("InstalledAndUsable");
}

void LauncherWindow::fetchRemoteStampsParallel()
{
    // CipSoft GETs launcher/client/assets stamps concurrently before decide.
    // Soft adds an optional mods stamp (third tip channel) when modsversionurl is set.
    int pending = 0;
    QEventLoop loop;
    auto oneDone = [&] {
        if (--pending <= 0)
            loop.quit();
    };
    ++pending;
    m_client->getAsync(m_build.versionUrl, [this, oneDone](const QByteArray& body, const QString& error) {
        if (!error.isEmpty() && body.isEmpty())
            qCWarning(lcLauncher) << "client version stamp failed" << error;
        else
            m_remoteVersion = QString::fromUtf8(body).trimmed();
        oneDone();
    });
    if (!m_build.assetsVersionUrl.isEmpty()) {
        ++pending;
        m_client->getAsync(m_build.assetsVersionUrl, [this, oneDone](const QByteArray& body, const QString& error) {
            if (!error.isEmpty() && body.isEmpty())
                qCWarning(lcLauncher) << "assets stamp failed" << error;
            else
                m_remoteAssetsVersion = QString::fromUtf8(body).trimmed();
            oneDone();
        });
    }
    if (!m_build.modsVersionUrl.isEmpty()) {
        ++pending;
        m_client->getAsync(m_build.modsVersionUrl, [this, oneDone](const QByteArray& body, const QString& error) {
            if (!error.isEmpty() && body.isEmpty())
                qCWarning(lcLauncher) << "mods stamp failed" << error;
            else
                m_remoteModsVersion = QString::fromUtf8(body).trimmed();
            oneDone();
        });
    }
    if (pending > 0)
        loop.exec();
}

void LauncherWindow::setBusy(bool busy, const QString& label)
{
    m_busy = busy;
    m_action->setEnabled(!busy);
    if (busy) {
        m_progress->show();
        m_progress->setValue(0);
        m_action->setText({});
        setActionSkin(QStringLiteral("Orange"), QStringLiteral("Update"));
        setLogoState(LogoState::Updating);
        if (!label.isEmpty())
            m_status->setText(label);
        else
            m_status->setText(QStringLiteral("PartiallyInstalled"));
    } else {
        m_progress->hide();
        m_progress->setValue(0);
        setLogoState(LogoState::Classic);
    }
}

void LauncherWindow::onProgress(qint64 bytes, qint64 total, int fileIndex, int fileCount)
{
    if (!m_busy)
        return;
    if (!m_timer.isValid())
        m_timer.start();
    const qint64 overallTotal = total > 0 ? total : bytes;
    if (overallTotal > 0)
        m_progress->setValue(int(qMin(qint64(100), bytes * 100 / overallTotal)));
    // At 100% CIP still unpacks / verifies; avoid a lying ETA 00:00:00.
    if (overallTotal > 0 && bytes >= overallTotal)
        m_status->setText(QStringLiteral("Finishing…"));
    else
        m_status->setText(formatEta(bytes, overallTotal, m_timer.elapsed()));
    Q_UNUSED(fileIndex);
    Q_UNUSED(fileCount);
}



void LauncherWindow::refresh()
{
    const QString local = localVersion();
    const QString shown = m_remoteVersion.isEmpty() ? local : m_remoteVersion;
    m_version->setText(shown);
    // CIP: bottom-left is the short hash from the package version, not "dev".
    if (m_buildLabel) {
        const QString hash = shortVersionHash(shown);
        m_buildLabel->setText(hash.isEmpty() ? QStringLiteral(TIBIA_LAUNCHER_VERSION) : hash);
    }
    if (m_busy)
        return;
    m_progress->hide();
    m_action->setText({});
    // CIP: version stamp plus per-file unpackedhash gaps (package.json files[]).
    if (!m_packageDoc.files.isEmpty())
        m_hasGaps = m_client->hasFileGaps(m_packageDoc, packageDir());
    const bool assetsStale = !m_remoteAssetsVersion.isEmpty()
        && localAssetsStamp() != m_remoteAssetsVersion;
    const bool modsStale = !modsStampOk();
#ifdef __EMSCRIPTEN__
    const bool thingsStale = !thingsPacksStampPresent();
#else
    const bool thingsStale = false;
#endif
    const bool devPlay = readDevDesktop(nullptr, nullptr, nullptr);
    const QString status = installStatusLabel();
    qCInfo(lcLauncher) << "install status" << status
                       << "local" << local
                       << "remote" << m_remoteVersion
                       << "gaps" << m_hasGaps
                       << "assetsStale" << assetsStale
                       << "modsStale" << modsStale
                       << "thingsStale" << thingsStale
                       << "busy" << m_busy
                       << "devPlay" << devPlay
                       << "assetsStamp" << localAssetsStamp()
                       << "remoteAssets" << m_remoteAssetsVersion
                       << "modsStamp" << localModsStamp()
                       << "remoteMods" << m_remoteModsVersion
                       << "thingsStamp" << thingsPacksStampPresent();
    if (devPlay) {
        // Local monorepo: Play launches build/<preset>/bin/client (stock --no-mods
        // or tip pack). Tip Download/Update is skipped — use a tip-only
        // launcher without QTCLIENT_DEV_JSON for CIP install rehearsal.
        m_status->setText(LauncherBrand::instance().noMods
                              ? QStringLiteral("Dev (qtclient)")
                              : QStringLiteral("Dev (pack)"));
        setActionSkin(QStringLiteral("Green"), QStringLiteral("Play"));
        m_action->setEnabled(true);
        return;
    }
    m_status->setText(status);
    if (local.isEmpty())
        setActionSkin(QStringLiteral("Blue"), QStringLiteral("Download"));
    else if ((!m_remoteVersion.isEmpty() && local != m_remoteVersion) || m_hasGaps || assetsStale
             || modsStale || thingsStale)
        setActionSkin(QStringLiteral("Orange"), QStringLiteral("Update"));
    else
        setActionSkin(QStringLiteral("Green"), QStringLiteral("Play"));
    m_action->setEnabled(true);
}

void LauncherWindow::applyHint(const QVector<tibia::app::Hint>& hints)
{
    QVector<tibia::app::Hint> feed = hints;
    if (!tibia::app::hasRichHints(feed))
        feed = tibia::app::loadBundledHints();
    const tibia::app::Hint hint = tibia::app::randomHint(feed);
    if (hint.richText.isEmpty())
        return;
    m_hintBox->setTitle(hint.title.isEmpty() ? QStringLiteral("Hint") : hint.title);
    m_news->setHtml(tibia::app::hintHtml(hint, m_client->cdnBase()));
}

void LauncherWindow::loadFeed()
{
    QString error;
    QUrl url = m_hintsUrl;
    if (!url.isValid() && !m_client->cdnBase().isEmpty())
        url = QUrl(m_client->cdnBase() + QStringLiteral("/hints.json"));
    applyHint(m_client->fetchHintList(url, &error));
}

void LauncherWindow::considerSelfUpdate(const CipMetadata& metadata)
{
#ifdef __EMSCRIPTEN__
    Q_UNUSED(metadata);
    return;
#else
    // Local monorepo / CDN rehearsal must never replace this cmake-built binary
    // with the product tip launcher. TIBIA_LAUNCHER_VERSION is a git short hash
    // here, so it always differs from tip package.json.version and would
    // otherwise download tip launcher files and startDetached that tip.
    if (devPlayAvailable()) {
        qInfo("launcher self-update skipped (QTCLIENT_DEV_JSON / Dev Play)");
        return;
    }
    if (cdnIsLoopback(m_client->cdnBase())) {
        qInfo("launcher self-update skipped (loopback CDN)");
        return;
    }
    if (qEnvironmentVariableIntValue("QTCLIENT_DISABLE_SELF_UPDATE") == 1) {
        qInfo("launcher self-update skipped (QTCLIENT_DISABLE_SELF_UPDATE=1)");
        return;
    }
    const QString local = QStringLiteral(TIBIA_LAUNCHER_VERSION);
    if (local == QStringLiteral("dev") || local == QStringLiteral("embedded"))
        return;
    const CipBuild* build = PackageClient::matchBuild(metadata.launcherBuilds, m_client->hostOs(), m_client->hostArch());
    if (!build)
        return;
    QString error;
    const QString remote = m_client->fetchVersion(build->versionUrl, &error);
    if (remote.isEmpty() || remote == local)
        return;
    const CipPackageDoc doc = m_client->fetchPackage(build->packageUrl, &error);
    if (doc.version.isEmpty())
        return;
    const QString staging = QDir(m_client->dataDir()).filePath(QStringLiteral("self-update"));
    QDir(staging).removeRecursively();
    if (!m_client->installPackage(build->packageUrl, doc, staging, &error)) {
        m_status->setText(error);
        return;
    }
#if defined(Q_OS_MACOS)
    const QString root = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../..")).canonicalPath();
#else
    const QString root = QCoreApplication::applicationDirPath();
#endif
    QDirIterator it(staging, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(staging).relativeFilePath(path);
        if (rel.contains(QStringLiteral("package.json")) || rel == QStringLiteral("assets.json")
            || rel == QStringLiteral("assets.json.sha256"))
            continue;
        const QString dest = QDir(root).filePath(rel);
        QDir().mkpath(QFileInfo(dest).path());
        QFile::remove(dest);
        QFile::copy(path, dest);
    }
    const QString exe = QDir(root).filePath(doc.executable);
    QFile(exe).setPermissions(QFile::permissions(exe) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    if (QProcess::startDetached(exe, {}, root))
        QCoreApplication::quit();
#endif
}

void LauncherWindow::start()
{
#ifdef __EMSCRIPTEN__
    startAsync();
#else
    QString error;
    const CipMetadata metadata = m_client->fetchMetadata(&error);
    if (metadata.packages.isEmpty()) {
        m_news->setPlainText(error.isEmpty() ? QStringLiteral("Download of main configuration failed:") : error);
        refresh();
        return;
    }
    if (const CipPackageInfo* selected = selectPackage(metadata))
        m_package = *selected;
    else
        m_package = metadata.packages.first();
    m_hintsUrl = metadata.hintsUrl;
    if (m_mark && !m_package.logolinktarget.isEmpty()) {
        const QUrl logoUrl(m_package.logolinktarget);
        if (logoUrl.isValid() && (logoUrl.scheme() == QLatin1String("http") || logoUrl.scheme() == QLatin1String("https"))) {
            m_mark->setText(QStringLiteral("<a href=\"%1\"><img src=\":/images/launcher/CipSoftLogo.png\"></a>")
                                .arg(QString::fromUtf8(logoUrl.toEncoded())));
        }
    }
    const CipBuild* build = PackageClient::matchBuild(m_package.builds, m_client->hostOs(), m_client->hostArch());
    if (!build) {
        m_news->setPlainText(QStringLiteral("Could not determine patch information. Your client might no longer be supported."));
        refresh();
        return;
    }
    m_build = *build;
    fetchRemoteStampsParallel();
    {
        QString packageError;
        m_packageDoc = m_client->fetchPackage(m_build.packageUrl, &packageError);
        if (!m_packageDoc.files.isEmpty())
            m_hasGaps = m_client->hasFileGaps(m_packageDoc, packageDir());
    }
    loadFeed();
    refresh();
    considerSelfUpdate(metadata);
    const bool patch = QCoreApplication::arguments().contains(QStringLiteral("--patch"));
    const bool forceSoft = QCoreApplication::arguments().contains(QStringLiteral("--show-launcher"))
        || qEnvironmentVariableIntValue("QTCLIENT_FORCE_LAUNCHER") == 1;
    const QString local = localVersion();
    const bool assetsOk = m_remoteAssetsVersion.isEmpty()
        || localAssetsStamp() == m_remoteAssetsVersion;
    // Warm tip → auto-Play (CIP --patch when current). Soft chrome still used for
    // Update / --show-launcher / QTCLIENT_FORCE_LAUNCHER=1.
    if (!forceSoft && !m_remoteVersion.isEmpty() && local == m_remoteVersion && !m_hasGaps
        && assetsOk && modsStampOk()) {
        qInfo("launcher warm tip InstalledAndUsable — auto Play");
        play();
        return;
    }
    // CIP: auto-download only for NotInstalled when installautomatically is set, or --patch.
    // Version mismatch / file gaps show Orange Update and wait for a click.
    if ((m_package.installautomatically && local.isEmpty())
        || (patch && local != m_remoteVersion))
        installIfNeeded(patch);
#endif
}

#ifdef __EMSCRIPTEN__
void LauncherWindow::startAsync()
{
    const QUrl metaUrl(m_client->tipUrl());
    m_client->getAsync(metaUrl, [this](const QByteArray& body, const QString& error) {
        QString parseError = error;
        const CipMetadata metadata = m_client->parseMetadata(body, &parseError);
        if (metadata.packages.isEmpty()) {
            m_news->setPlainText(parseError.isEmpty() ? QStringLiteral("Download of main configuration failed:") : parseError);
            refresh();
            return;
        }
        if (const CipPackageInfo* selected = selectPackage(metadata))
            m_package = *selected;
        else
            m_package = metadata.packages.first();
        m_hintsUrl = metadata.hintsUrl;
        if (m_mark && !m_package.logolinktarget.isEmpty()) {
            const QUrl logoUrl(m_package.logolinktarget);
            if (logoUrl.isValid() && (logoUrl.scheme() == QLatin1String("http") || logoUrl.scheme() == QLatin1String("https"))) {
                m_mark->setText(QStringLiteral("<a href=\"%1\"><img src=\":/images/launcher/CipSoftLogo.png\"></a>")
                                    .arg(QString::fromUtf8(logoUrl.toEncoded())));
            }
        }
        const CipBuild* build = PackageClient::matchBuild(m_package.builds, m_client->hostOs(), m_client->hostArch());
        if (!build) {
            m_news->setPlainText(QStringLiteral("Could not determine patch information. Your client might no longer be supported."));
            refresh();
            return;
        }
        m_build = *build;
        // CipSoft: parallel client + assets (+ optional mods) stamp GETs, then package + hints.
        auto continueWithPackage = [this] {
            m_client->getAsync(m_build.packageUrl, [this](const QByteArray& packageBody, const QString&) {
                QString parseError;
                m_packageDoc = m_client->parsePackage(packageBody, &parseError);
                if (!m_packageDoc.files.isEmpty())
                    m_hasGaps = m_client->hasFileGaps(m_packageDoc, packageDir());
                QUrl hintsUrl = m_hintsUrl;
                if (!hintsUrl.isValid() && !m_client->cdnBase().isEmpty())
                    hintsUrl = QUrl(m_client->cdnBase() + QStringLiteral("/hints.json"));
                m_client->getAsync(hintsUrl, [this](const QByteArray& hintsBody, const QString&) {
                    applyHint(tibia::app::parseHints(hintsBody));
                    refresh();
                    const QString local = localVersion();
                    const bool forceSoft =
                        QCoreApplication::arguments().contains(QStringLiteral("--show-launcher"))
                        || qEnvironmentVariableIntValue("QTCLIENT_FORCE_LAUNCHER") == 1;
                    const bool assetsOk = m_remoteAssetsVersion.isEmpty()
                        || localAssetsStamp() == m_remoteAssetsVersion;
                    const bool thingsOk = thingsPacksStampPresent();
                    // Warm tip → remount game-only (same as Soft Play / portal auto-skip).
                    if (!forceSoft && !m_remoteVersion.isEmpty() && local == m_remoteVersion
                        && !m_hasGaps && assetsOk && thingsOk && modsStampOk()) {
                        qInfo("launcher wasm warm tip InstalledAndUsable — auto Play remount");
                        play();
                        return;
                    }
                    // CIP: auto-download only for NotInstalled when installautomatically is set.
                    if (m_package.installautomatically && local.isEmpty())
                        installIfNeededAsync(false);
                });
            });
        };
        struct StampState {
            int pending = 0;
            bool versionFailed = false;
            QString versionError;
        };
        auto* stamps = new StampState;
        stamps->pending = 1
            + (m_build.assetsVersionUrl.isEmpty() ? 0 : 1)
            + (m_build.modsVersionUrl.isEmpty() ? 0 : 1);
        auto onStampDone = [this, stamps, continueWithPackage] {
            if (--stamps->pending > 0)
                return;
            const bool failed = stamps->versionFailed;
            const QString err = stamps->versionError;
            delete stamps;
            if (failed) {
                m_news->setPlainText(err);
                refresh();
                return;
            }
            continueWithPackage();
        };
        m_client->getAsync(m_build.versionUrl, [this, stamps, onStampDone](const QByteArray& versionBody, const QString& versionError) {
            if (!versionError.isEmpty() && versionBody.isEmpty()) {
                stamps->versionFailed = true;
                stamps->versionError = versionError;
            } else {
                m_remoteVersion = QString::fromUtf8(versionBody).trimmed();
            }
            onStampDone();
        });
        if (!m_build.assetsVersionUrl.isEmpty()) {
            m_client->getAsync(m_build.assetsVersionUrl, [this, onStampDone](const QByteArray& assetsBody, const QString&) {
                m_remoteAssetsVersion = QString::fromUtf8(assetsBody).trimmed();
                onStampDone();
            });
        }
        if (!m_build.modsVersionUrl.isEmpty()) {
            m_client->getAsync(m_build.modsVersionUrl, [this, onStampDone](const QByteArray& modsBody, const QString&) {
                m_remoteModsVersion = QString::fromUtf8(modsBody).trimmed();
                onStampDone();
            });
        }
    });
}

void LauncherWindow::finishInstallAsync(bool thenPlay, const QString& version)
{
    m_remoteVersion = version;
    m_hasGaps = false;
    setBusy(false, {});
    m_status->clear();
    m_progress->hide();
    refresh();
    if (thenPlay)
        play();
}

void LauncherWindow::installThingsPacksViaJs(bool thenPlay, const QString& version)
{
    // Product sprite zip packs + boot essentials into OPFS tip (Module.FS). No zip in C++.
    // JS sets Module.qtclientThingsResult; we poll so we need no extra WASM exports.
    // Pass packageDir so the stamp lands where thingsPacksStampPresent() looks
    // (must match selected tip directoryname).
    // After the engine package hits 100%, keep the bar honest during assets/sprites.
    m_progress->show();
    m_progress->setValue(0);
    m_status->setText(QStringLiteral("Loading assets…"));
    const QByteArray cdn = m_client->cdnBase().toUtf8();
    const QByteArray pkg = packageDir().toUtf8();
    // EM_ASM is a C preprocessor macro: commas outside () split arguments. Prefer
    // sequential assigns and function(a){return function(b){...}} over {a:1, b:2}.
    EM_ASM({
        var cdn = UTF8ToString($0);
        var packageRoot = UTF8ToString($1);
        Module.qtclientThingsResult = null;
        Module.qtclientThingsProgress = {};
        Module.qtclientThingsProgress.label = 'Loading assets...';
        Module.qtclientThingsProgress.percent = 0;
        Promise.resolve()
            .then(function() {
                // MODULARIZE keeps FS off window; portal installThings looks at
                // window.FS / window.Module.FS — publish them before calling.
                var fs = (typeof FS !== 'undefined') ? FS : (Module && Module.FS);
                if (fs) {
                    window.FS = fs;
                    window.Module = Module;
                }
                if (typeof window.__qtclientInstallThings !== 'function')
                    throw new Error('__qtclientInstallThings is not bound');
                // Portal callback is (label, percent). Use arguments[] so EM_ASM
                // never sees a comma-separated parameter list.
                var progress = function() {
                    var args = arguments;
                    // EM_ASM: empty '' is an invalid C character constant — use String().
                    var lab = args.length > 0 ? args[0] : String();
                    var pct = args.length > 1 ? args[1] : 0;
                    Module.qtclientThingsProgress = {};
                    Module.qtclientThingsProgress.label = String(lab || String());
                    Module.qtclientThingsProgress.percent = pct | 0;
                };
                return window.__qtclientInstallThings(cdn, fs, packageRoot, progress);
            })
            .then(function() { Module.qtclientThingsResult = 1; })
            .catch(function(err) {
                console.error('[qtclient] things install failed', err);
                Module.qtclientThingsResult = 0;
            });
    }, cdn.constData(), pkg.constData());
    auto* timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer, thenPlay, version]() {
        applyThingsProgressFromJs();
        const int result = EM_ASM_INT({
            if (Module.qtclientThingsResult === null || Module.qtclientThingsResult === undefined)
                return -1;
            var r = Module.qtclientThingsResult | 0;
            Module.qtclientThingsResult = undefined;
            Module.qtclientThingsProgress = undefined;
            return r;
        });
        if (result < 0)
            return;
        timer->stop();
        timer->deleteLater();
        onThingsInstallDone(result != 0, thenPlay, version);
    });
    timer->start();
}

void LauncherWindow::installAssetsAndThingsAsync(bool thenPlay)
{
    // Prefer zip packs on web (thousands of per-file lzma assets are too slow).
    // assets.json.sha256 is stamped in onThingsInstallDone when the remote tip is known.
    const QString version = m_remoteVersion.isEmpty() ? localVersion() : m_remoteVersion;
    installThingsPacksViaJs(thenPlay, version);
}

void LauncherWindow::installModsThenFinishAsync(bool thenPlay, const QString& version)
{
    if (m_build.modsUrl.isEmpty() || modsStampOk()) {
        finishInstallAsync(thenPlay, version);
        return;
    }
    m_status->setText(QStringLiteral("Loading mods…"));
    m_client->getAsync(m_build.modsUrl, [this, thenPlay, version](const QByteArray& body, const QString& error) {
        QString parseError = error;
        const QVector<CipFile> files = m_client->parseAssetFiles(body, &parseError);
        if (files.isEmpty() && !parseError.isEmpty()) {
            setBusy(false, {});
            refresh();
            m_status->setText(parseError);
            return;
        }
        const QString tip = packageDir();
        const qint64 batch = m_client->pendingPackedBytes(files, tip);
        m_client->installModsAsync(m_build.modsUrl, files, m_remoteModsVersion, tip,
                                   [this, thenPlay, version](bool ok, const QString& installError) {
                                       if (!ok) {
                                           setBusy(false, {});
                                           refresh();
                                           m_status->setText(installError.isEmpty()
                                                                ? QStringLiteral("The installation of mods failed.")
                                                                : installError);
                                           return;
                                       }
                                       finishInstallAsync(thenPlay, version);
                                   },
                                   0, batch > 0 ? batch : 1);
    });
}

void LauncherWindow::onThingsInstallDone(bool ok, bool thenPlay, const QString& version)
{
    if (!ok) {
        setBusy(false, {});
        refresh();
        m_status->setText(QStringLiteral("The installation of assets failed."));
        return;
    }
    if (!m_remoteAssetsVersion.isEmpty()) {
        QFile stamp(QDir(packageDir()).filePath(QStringLiteral("assets.json.sha256")));
        if (stamp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            stamp.write(m_remoteAssetsVersion.toUtf8());
            stamp.close();
        }
    }
    installModsThenFinishAsync(thenPlay, version);
}

void LauncherWindow::installIfNeededAsync(bool thenPlay)
{
    const QString local = localVersion();
    const bool packageOk = !m_remoteVersion.isEmpty() && local == m_remoteVersion && !m_hasGaps;
    const bool assetsOk = m_remoteAssetsVersion.isEmpty()
        || localAssetsStamp() == m_remoteAssetsVersion;
    const bool thingsOk = thingsPacksStampPresent();
    const bool modsOk = modsStampOk();
    qCInfo(lcLauncher) << "installIfNeededAsync"
                       << "thenPlay" << thenPlay
                       << "packageOk" << packageOk
                       << "assetsOk" << assetsOk
                       << "thingsOk" << thingsOk
                       << "modsOk" << modsOk
                       << "local" << local
                       << "remote" << m_remoteVersion;
    if (packageOk && assetsOk && thingsOk && modsOk) {
        if (thenPlay)
            play();
        return;
    }
    setBusy(true, {});
    m_timer.start();
    if (packageOk && assetsOk && thingsOk && !modsOk) {
        qCInfo(lcLauncher) << "installIfNeededAsync → mods only";
        const QString version = m_remoteVersion.isEmpty() ? local : m_remoteVersion;
        installModsThenFinishAsync(thenPlay, version);
        return;
    }
    if (packageOk) {
        qCInfo(lcLauncher) << "installIfNeededAsync → assets/things (+ mods)";
        installAssetsAndThingsAsync(thenPlay);
        return;
    }
    auto runInstall = [this, thenPlay](const CipPackageDoc& doc, const QString& parseError) {
        if (doc.version.isEmpty()) {
            setBusy(false, {});
            refresh();
            m_status->setText(parseError.isEmpty() ? QStringLiteral("The installation of %1 failed.").arg(m_package.name)
                                                  : parseError);
            return;
        }
        m_packageDoc = doc;
        // Engine package into FS tip, then assets/things/mods before Play is ready.
        m_client->installPackageAsync(m_build.packageUrl, doc, packageDir(),
                                      [this, thenPlay, version = doc.version](bool ok, const QString& installError) {
                                          if (!ok) {
                                              setBusy(false, {});
                                              // refresh() rewrites status to Unknown/PartiallyInstalled —
                                              // set the failure text after so Download failures are visible.
                                              refresh();
                                              m_status->setText(installError.isEmpty()
                                                                   ? QStringLiteral("The installation of %1 failed.")
                                                                         .arg(m_package.name)
                                                                   : installError);
                                              return;
                                          }
                                          m_remoteVersion = version;
                                          m_hasGaps = false;
                                          installAssetsAndThingsAsync(thenPlay);
                                      });
    };
    if (!m_packageDoc.files.isEmpty() && m_packageDoc.version == m_remoteVersion) {
        runInstall(m_packageDoc, {});
        return;
    }
    m_client->getAsync(m_build.packageUrl, [this, thenPlay, runInstall](const QByteArray& body, const QString& error) {
        QString parseError = error;
        runInstall(m_client->parsePackage(body, &parseError), parseError);
    });
}

#endif

void LauncherWindow::installIfNeeded(bool thenPlay)
{
    const QString local = localVersion();
    const bool assetsOk = m_remoteAssetsVersion.isEmpty()
        || localAssetsStamp() == m_remoteAssetsVersion;
    if (!m_remoteVersion.isEmpty() && local == m_remoteVersion && !m_hasGaps && assetsOk
        && modsStampOk()) {
        if (thenPlay)
            play();
        return;
    }
    setBusy(true, {});
    m_timer.start();
    QString error;
    CipPackageDoc doc = m_packageDoc;
    if (doc.files.isEmpty() || doc.version != m_remoteVersion)
        doc = m_client->fetchPackage(m_build.packageUrl, &error);
    if (doc.version.isEmpty()) {
        setBusy(false, {});
        m_status->setText(error.isEmpty() ? QStringLiteral("The installation of %1 failed.").arg(m_package.name) : error);
        refresh();
        return;
    }

    // One progress span across client package + assets + mods so the bar does not
    // snap back to 0% between phases (felt like two competing updates).
    const QString tip = packageDir();
    qint64 pkgBytes = m_client->pendingPackedBytes(doc.files, tip);
    QVector<CipFile> assets;
    qint64 assetBytes = 0;
    QString assetsVersion;
    if (!m_build.assetsUrl.isEmpty()) {
        assetsVersion = m_client->fetchVersion(m_build.assetsVersionUrl, &error);
        const QString have = readText(QDir(tip).filePath(QStringLiteral("assets.json.sha256")));
        if (!assetsVersion.isEmpty() && assetsVersion != have) {
            assets = m_client->fetchAssetFiles(m_build.assetsUrl, &error);
            assetBytes = m_client->pendingPackedBytes(assets, tip);
        }
    }
    QVector<CipFile> mods;
    qint64 modsBytes = 0;
    QString modsVersion;
    if (!m_build.modsUrl.isEmpty()) {
        modsVersion = m_remoteModsVersion.isEmpty()
            ? m_client->fetchVersion(m_build.modsVersionUrl, &error)
            : m_remoteModsVersion;
        if (!modsVersion.isEmpty())
            m_remoteModsVersion = modsVersion;
        const QString haveMods = localModsStamp();
        if (!modsVersion.isEmpty() && modsVersion != haveMods) {
            mods = m_client->fetchAssetFiles(m_build.modsUrl, &error);
            modsBytes = m_client->pendingPackedBytes(mods, tip);
        }
    }
    const qint64 grandTotal = qMax(pkgBytes + assetBytes + modsBytes, qint64(1));

    if (!m_client->installPackage(m_build.packageUrl, doc, tip, &error, 0, grandTotal)) {
        setBusy(false, {});
        m_status->setText(error.isEmpty() ? QStringLiteral("The installation of %1 failed.").arg(m_package.name) : error);
        refresh();
        return;
    }
    m_packageDoc = doc;
    if (!assets.isEmpty()) {
        if (!m_client->installAssets(m_build.assetsUrl, assets, assetsVersion, tip, &error, pkgBytes, grandTotal)) {
            setBusy(false, {});
            m_status->setText(error);
            refresh();
            return;
        }
    }
    if (!mods.isEmpty()) {
        if (!m_client->installMods(m_build.modsUrl, mods, modsVersion, tip, &error,
                                   pkgBytes + assetBytes, grandTotal)) {
            setBusy(false, {});
            m_status->setText(error);
            refresh();
            return;
        }
    }
    m_remoteVersion = doc.version;
    m_hasGaps = false;
    setBusy(false, {});
    m_status->clear();
    m_progress->hide();
    refresh();
    if (thenPlay)
        play();
}

void LauncherWindow::onAction()
{
#ifndef __EMSCRIPTEN__
    // DEV Play must not fall through to tip Download/Update (constructor may
    // still be waiting on metadata, or tip AppData may look NotInstalled).
    if (devPlayAvailable()) {
        play();
        return;
    }
#endif
    const QString local = localVersion();
    const bool assetsOk = m_remoteAssetsVersion.isEmpty()
        || localAssetsStamp() == m_remoteAssetsVersion;
#ifdef __EMSCRIPTEN__
    const bool thingsOk = thingsPacksStampPresent();
#else
    const bool thingsOk = true;
#endif
    if (!m_remoteVersion.isEmpty() && local == m_remoteVersion && !m_hasGaps && assetsOk && thingsOk
        && modsStampOk())
        play();
#ifdef __EMSCRIPTEN__
    else
        // Tip already on disk → Update should continue into Play after packs sync.
        installIfNeededAsync(!local.isEmpty());
#else
    else
        installIfNeeded(false);
#endif
}

bool LauncherWindow::tryPlayDevClient()
{
#ifdef __EMSCRIPTEN__
    return false;
#else
    QString exe;
    QString cwd;
    QString revision;
    if (!readDevDesktop(&exe, &cwd, &revision))
        return false;

    QStringList clientArgs = QProcess::splitCommand(qEnvironmentVariable("QTCLIENT_CLIENT_ARGS"));
    const LauncherBrand& brand = LauncherBrand::instance();
    if (brand.noMods) {
        if (!clientArgs.contains(QStringLiteral("--no-mods")))
            clientArgs.prepend(QStringLiteral("--no-mods"));
        qInfo().noquote() << "Play → qtclient DEV client (--no-mods)" << exe;
    } else if (brand.defaultMod) {
        if (!clientArgs.contains(QStringLiteral("--default-mod")))
            clientArgs.prepend(QStringLiteral("--default-mod"));
        qInfo().noquote() << "Play → qtclient DEV client (--default-mod)" << exe;
    } else {
        const QString tipDir = QFileInfo(exe).absolutePath();
        const QDir tipMods(QDir(tipDir).filePath(QStringLiteral("mods")));
        bool tipPack = false;
        if (tipMods.exists()) {
            for (const QFileInfo& info : tipMods.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (info.fileName().startsWith(QLatin1Char('_')))
                    continue;
                if (QFile::exists(info.absoluteFilePath() + QStringLiteral("/mod.json"))) {
                    tipPack = true;
                    break;
                }
            }
        }
        const QString envMods = qEnvironmentVariable("QTCLIENT_MODS_DIR");
        const bool envPack = !envMods.isEmpty()
            && (QFile::exists(QDir(envMods).filePath(QStringLiteral("mod.json")))
                || QDir(envMods).exists());
        if (!tipPack && !envPack) {
            m_status->setText(QStringLiteral("Tip pack missing — make qtclient client install"));
            qWarning("Play: missing tip mods/<id>/mod.json under %s", qUtf8Printable(tipDir));
            setLogoState(LogoState::Classic);
            refresh();
            return true; // handled (do not fall through to tip package)
        }
        qInfo().noquote() << "Play → packed DEV client" << exe;
    }
    if (cwd.isEmpty())
        cwd = QFileInfo(exe).absolutePath();
    spawn(exe, cwd, revision.isEmpty() ? m_remoteVersion : revision, clientArgs);
    return true;
#endif
}

void LauncherWindow::play()
{
    // CIP swaps the header art to LaunchingClient before the game appears
    // (desktop spawn hide / browser remount overlay).
    setLogoState(LogoState::Launching);
    setActionSkin(QStringLiteral("Green"), QStringLiteral("Play"));
    m_action->setEnabled(false);
#ifdef __EMSCRIPTEN__
    // Optional in-process hook (tests). Production /play remounts into a
    // game-only Module so mobile Safari is not holding launcher + QML/WebGL.
    if (m_inProcessPlay) {
        const QByteArray pkg = packageDir().toUtf8();
        const QByteArray data = m_client->dataDir().toUtf8();
        EM_ASM({
            window.__qtclientPackageRoot = UTF8ToString($0);
            window.__qtclientDataDir = UTF8ToString($1);
            if (typeof FS !== "undefined" && FS.syncfs)
                FS.syncfs(false, function() {});
        }, pkg.constData(), data.constData());
        m_inProcessPlay();
        return;
    }
    // Browser stand-in for CIP QProcess::startDetached: remount into a game-only
    // Module (portal takePlayHandoff → QTCLIENT_WASM_SKIP_LAUNCHER).
    {
        const QByteArray pkg = packageDir().toUtf8();
        const QByteArray data = m_client->dataDir().toUtf8();
        const QString pkgName = m_package.name.isEmpty() ? QStringLiteral("Tibia") : m_package.name;
        qInfo().noquote() << "Launching executable of Package " << pkgName << " : " << pkg
                          << "(wasm remount)";
        EM_ASM({
            window.__qtclientPackageRoot = UTF8ToString($0);
            window.__qtclientDataDir = UTF8ToString($1);
            console.info("[qtclient] Play remount (game-only boot)", {
                packageRoot: window.__qtclientPackageRoot,
                dataDir: window.__qtclientDataDir
            });
            var go = function() {
                try { sessionStorage.setItem("tibia.play", "1"); } catch (e) {}
                try { sessionStorage.setItem("tibia.play.transition", "1"); } catch (e) {}
                // qtclient Widgets + ~70MB WASM still hold the heap. Drop canvas /
                // Module refs and yield so Safari can reclaim before the game
                // Module compiles again — otherwise remount jetsams
                // ("A problem repeatedly occurred").
                try {
                    var nodes = document.querySelectorAll("canvas");
                    for (var i = 0; i < nodes.length; i++) {
                        try {
                            nodes[i].width = 0;
                            nodes[i].height = 0;
                            nodes[i].remove();
                        } catch (e2) {}
                    }
                } catch (e3) {}
                try {
                    if (typeof Module !== "undefined" && Module) {
                        try { Module.canvas = null; } catch (e4) {}
                    }
                    window.Module = undefined;
                } catch (e5) {}
                setTimeout(function() { location.reload(); }, 350);
            };
            if (typeof FS !== "undefined" && FS.syncfs) {
                FS.syncfs(false, function() { go(); });
            } else {
                go();
            }
        }, pkg.constData(), data.constData());
    }
#else
    // Local monorepo: qtclient build from dist/cdn/latest/dev.json (not tip AppData).
    if (tryPlayDevClient())
        return;
    const QString dir = packageDir();
    QString exe = readText(QDir(dir).filePath(QStringLiteral("package.json")));
    Q_UNUSED(exe);
    const CipPackageDoc doc = [&] {
        CipPackageDoc parsed;
        QFile file(QDir(dir).filePath(QStringLiteral("package.json")));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
            parsed.executable = json.object().value(QStringLiteral("executable")).toString();
            parsed.version = json.object().value(QStringLiteral("version")).toString();
        }
        return parsed;
    }();
    const QString executable = QDir(dir).filePath(doc.executable);
    QFile exeFile(executable);
    exeFile.setPermissions(exeFile.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    if (!QFileInfo::exists(executable)) {
        setLogoState(LogoState::Classic);
        refresh();
        m_status->setText(QStringLiteral("Tibia cannot be started"));
        return;
    }
    QStringList tipArgs = QProcess::splitCommand(qEnvironmentVariable("QTCLIENT_CLIENT_ARGS"));
    const LauncherBrand& tipBrand = LauncherBrand::instance();
    if (tipBrand.noMods) {
        if (!tipArgs.contains(QStringLiteral("--no-mods")))
            tipArgs.prepend(QStringLiteral("--no-mods"));
    } else if (tipBrand.defaultMod) {
        if (!tipArgs.contains(QStringLiteral("--default-mod")))
            tipArgs.prepend(QStringLiteral("--default-mod"));
    }
    spawn(executable, dir, doc.version.isEmpty() ? m_remoteVersion : doc.version, tipArgs);
#endif
}

void LauncherWindow::spawn(const QString& executable, const QString& workDir, const QString& revision,
                           const QStringList& clientArgs)
{
#ifdef __EMSCRIPTEN__
    Q_UNUSED(executable);
    Q_UNUSED(workDir);
    Q_UNUSED(revision);
    Q_UNUSED(clientArgs);
#else
    if (m_game)
        return;
    const QString marker = QDir(m_client->dataDir()).filePath(QStringLiteral(".restart"));
    const QString showFile = QDir(m_client->dataDir()).filePath(QStringLiteral(".show-launcher"));
    QFile::remove(marker);
    QFile::remove(showFile);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Launcher → client contract uses TIBIA_* / QTCLIENT_* env vars.
    // Portal loginservice.php accepts latest/release.json.revision (errorCode 7 otherwise).
    // Prefer that over tip package.json.version or DEV mtime stamps.
    QString loginRevision = revision;
    {
        QEventLoop loop;
        QString live;
        m_client->getAsync(
            QUrl(m_client->cdnBase() + QStringLiteral("/latest/release.json")),
            [&](const QByteArray& body, const QString& err) {
                if (err.isEmpty() && !body.isEmpty()) {
                    live = QJsonDocument::fromJson(body)
                               .object()
                               .value(QStringLiteral("revision"))
                               .toString()
                               .trimmed();
                }
                loop.quit();
            });
        loop.exec();
        if (!live.isEmpty())
            loginRevision = live;
    }
    if (loginRevision != revision)
        qInfo().noquote() << "Play → login revision" << loginRevision << "(spawn was" << revision << ")";
    env.insert(QStringLiteral("QTCLIENT_LAUNCHER"), QStringLiteral("1"));
    env.insert(QStringLiteral("QTCLIENT_CLIENT_REVISION"), loginRevision);
    env.insert(QStringLiteral("QTCLIENT_RESTART_FILE"), marker);
    env.insert(QStringLiteral("QTCLIENT_SHOW_FILE"), showFile);
    env.insert(QStringLiteral("QTCLIENT_LAUNCHER_BIN"), QCoreApplication::applicationFilePath());
    // Tip CDN for assets; login URLs come from the client's mod.json `urls`
    // (tip mods/<id> urls) — do not inject QTCLIENT_LOGIN_* here.
    env.insert(QStringLiteral("QTCLIENT_ASSET_BASE"), m_client->cdnBase());

    auto* process = new QProcess(this);
    m_game = process;
    // CipSoft Play: QProcess on Contents/MacOS/<executable> (not `open -a`).
    // Launch Services ignores workDir (cwd → "/") and breaks local DEV builds
    // that resolve data/setup.otml relative to apps/qtclient.
    process->setProgram(executable);
    process->setArguments(clientArgs);
    process->setWorkingDirectory(workDir);
    process->setProcessEnvironment(env);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(process, &QProcess::started, this, [this] { hide(); });
    connect(process, &QProcess::finished, this, [this, marker](int code, QProcess::ExitStatus status) {
        const bool restart = QFile::remove(marker);
        m_game = nullptr;
        if (status == QProcess::NormalExit && code == 0 && !restart) {
            QCoreApplication::quit();
            return;
        }
        show();
        raise();
        activateWindow();
        setLogoState(LogoState::Classic);
        if (restart) {
            start();
            return;
        }
        refresh();
        if (status != QProcess::NormalExit || code != 0)
            m_status->setText(QStringLiteral("Tibia cannot be started"));
    });
    auto* timer = new QTimer(process);
    connect(timer, &QTimer::timeout, this, [this, showFile] {
        if (QFile::remove(showFile)) {
            show();
            raise();
            activateWindow();
            // Game still owns the session — keep Play disabled until finished.
            if (!m_game) {
                setLogoState(LogoState::Classic);
                refresh();
            }
        }
    });
    timer->start(500);
    process->start();
    if (!process->waitForStarted(5000)) {
        const QString detail = process->errorString();
        m_game = nullptr;
        process->deleteLater();
        setLogoState(LogoState::Classic);
        refresh();
        m_status->setText(detail.isEmpty() ? QStringLiteral("Tibia cannot be started")
                                           : QStringLiteral("Tibia cannot be started: %1").arg(detail));
    }
#endif
}
