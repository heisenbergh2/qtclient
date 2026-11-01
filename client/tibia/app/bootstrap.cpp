#include "bootstrap.h"

#include "clientoptions.h"
#include "gamewindow/gamewindowciptypes.h"
#include "gamewindow/mousecursorcontroller.h"
#include "mods/modloader.h"
#include "mods/registry.h"
#include "overrideinterceptor.h"
#include "packqmlinterceptor.h"
#include "qmldisk.h"
#include "imageproviders/imagestripimageprovider.h"

#include <cip_registration.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QLibraryInfo>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QResource>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTranslator>

#include <algorithm>

namespace tibia::app {

namespace {

struct Renderer
{
    int index;
    const char* name;
    QSGRendererInterface::GraphicsApi api;
};

constexpr Renderer Renderers[] = {
    { 0, "auto", QSGRendererInterface::Unknown },
    { 3, "software", QSGRendererInterface::Software },
    { 6, "d3d11", QSGRendererInterface::Direct3D11 },
    { 7, "vulkan", QSGRendererInterface::Vulkan },
    { 8, "opengl", QSGRendererInterface::OpenGL },
    { 9, "metal", QSGRendererInterface::Metal },
    { 10, "d3d12", QSGRendererInterface::Direct3D12 },
};

void setEnvDefault(const char* name, const QByteArray& value)
{
    if (!qEnvironmentVariableIsSet(name))
        qputenv(name, value);
}

/// CIP's strings name the client and the game "Tibia"; show the application name instead.
/// Tibia Coins and Embrace of Tibia are game content the server and store use by name.
class AppNameTranslator final : public QTranslator
{
public:
    using QTranslator::QTranslator;

    QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        QString text = QTranslator::translate(context, sourceText, disambiguation, n);
        if (text.contains(QLatin1String("Tibia"))) {
            static const QRegularExpression name(QStringLiteral("(?<!Embrace of )\\bTibia\\b(?! Coin)"));
            text.replace(name, QCoreApplication::applicationName());
        }
        return text;
    }
};

}

QSGRendererInterface::GraphicsApi graphicsApiForRenderer(const int rendererIndex)
{
    for (const auto& r : Renderers)
        if (r.index == rendererIndex)
            return r.api;
    return QSGRendererInterface::Unknown;
}

int parseRendererIndex(const QString& value, const int fallback)
{
    bool ok = false;
    const int index = value.toInt(&ok);
    if (ok)
        return index;
    for (const auto& r : Renderers)
        if (value.compare(QLatin1String(r.name), Qt::CaseInsensitive) == 0)
            return r.index;
    return fallback;
}

bool isRendererSupported(const int rendererIndex)
{
    switch (graphicsApiForRenderer(rendererIndex)) {
        case QSGRendererInterface::Unknown:
        case QSGRendererInterface::Software:
            return true;
#if defined(Q_OS_WASM)
        case QSGRendererInterface::OpenGL:
            return true;
#elif defined(Q_OS_MACOS) || defined(Q_OS_IOS)
        case QSGRendererInterface::OpenGL:
        case QSGRendererInterface::Metal:
            return true;
#elif defined(Q_OS_WIN)
        case QSGRendererInterface::OpenGL:
        case QSGRendererInterface::Direct3D11:
        case QSGRendererInterface::Direct3D12:
        case QSGRendererInterface::Vulkan:
            return true;
#else
        case QSGRendererInterface::OpenGL:
        case QSGRendererInterface::Vulkan:
            return true;
#endif
        default:
            return false;
    }
}

void configureProcess(const ClientOptions& options, const int rendererOverride)
{
    qputenv("QT_QUICK_CONTROLS_STYLE", "Universal");

    int renderer = rendererOverride >= 0 ? rendererOverride : options.rendererIndex();
    if (!isRendererSupported(renderer))
        renderer = 0;
#ifdef Q_OS_WASM
    // WebGL is the only accelerated API in the browser.
    if (renderer == 0)
        renderer = 8;
#endif
    const auto api = graphicsApiForRenderer(renderer);
    if (api == QSGRendererInterface::Vulkan || api == QSGRendererInterface::Metal)
        qputenv("QT_MTL_NO_TRANSACTION", "1");
    QQuickWindow::setGraphicsApi(api);
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    // Render loop and atlas live in the app settings, not clientoptions.json.
    QSettings settings;
    const QString loop = settings.value(QStringLiteral("graphics/renderLoop"), QStringLiteral("threaded")).toString();
    setEnvDefault("QSG_RENDER_LOOP", api == QSGRendererInterface::OpenGL ? QByteArray("basic") : loop.toLatin1());

    // Idle sleep caps uncapped present rate (often ~150 FPS on this machine with the
    // default 4 ms). With vsync off and no software frame limit, stay awake; otherwise
    // keep a short idle sleep for battery when paced by vsync or a limit.
    const bool vsync = options.vSyncEnabled();
    const bool frameLimit = options.options().value(QLatin1String("frameRateLimitEnabled")).toBool(true);
    if (!vsync && !frameLimit)
        qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
    else
        qputenv("QT_QPA_UPDATE_IDLE_TIME", "4");

    // CIP: SIZE_LIMIT = min(max(w,h)/8, 256)+1. HD xBRZ 4× plates need a higher cap so
    // they stay atlas citizens when the atlas path is live (basic/OpenGL).
    const bool hdMode = options.options().value(QLatin1String("modHdMode")).toBool(false);
    const int atlasWidth = std::max(512, settings.value(QStringLiteral("graphics/atlasWidth"), hdMode ? 8192 : 4096).toInt());
    const int atlasHeight = std::max(512, settings.value(QStringLiteral("graphics/atlasHeight"), hdMode ? 8192 : 4096).toInt());
    const int sizeLimitCap = hdMode ? 1024 : 256;
    setEnvDefault("QSG_ATLAS_WIDTH", QByteArray::number(atlasWidth));
    setEnvDefault("QSG_ATLAS_HEIGHT", QByteArray::number(atlasHeight));
    setEnvDefault("QSG_ATLAS_SIZE_LIMIT",
                  QByteArray::number(std::min(std::max(atlasWidth, atlasHeight) >> 3, sizeLimitCap) + 1));

    qputenv("QML_XHR_ALLOW_FILE_READ", "1");

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapInterval(vsync ? 1 : 0);
    QSurfaceFormat::setDefaultFormat(format);
}

QString mountGraphicsResources(const QStringList& dirs)
{
    for (const QString& dir : dirs) {
        if (dir.isEmpty())
            continue;
        QStringList candidates{
            QDir(dir).filePath(QStringLiteral("graphics_resources.rcc")),
            // CIP engine package path (installed client).
            QDir(dir).filePath(QStringLiteral("Contents/MacOS/graphics_resources.rcc")),
            // Tip package / CDN things → Contents/Resources/assets (portal OPFS).
            QDir(dir).filePath(QStringLiteral("Contents/Resources/assets/graphics_resources.rcc")),
            QDir(dir).filePath(QStringLiteral("Resources/assets/graphics_resources.rcc")),
            // Dev extract: flat CIP assets dump.
            QDir(dir).filePath(QStringLiteral("assets/graphics_resources.rcc")),
        };
        // Legacy: data/things/<version>/graphics_resources.rcc, newest first.
        const QDir things(QDir(dir).filePath(QStringLiteral("things")));
        const auto versions = things.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QString& v : versions)
            candidates << things.filePath(v + QStringLiteral("/graphics_resources.rcc"));
        for (const QString& file : candidates) {
            if (QFileInfo::exists(file) && QResource::registerResource(file))
                return file;
        }
    }
    return {};
}

void installUiFonts(const QStringList& dirs)
{
    if (QFontDatabase::families().contains(QStringLiteral("Verdana"), Qt::CaseInsensitive))
        return;

    const QStringList faces{
        QStringLiteral("Verdana.ttf"),
        QStringLiteral("Verdana-Bold.ttf"),
        QStringLiteral("Verdana-Italic.ttf"),
        QStringLiteral("Verdana-Bold-Italic.ttf"),
    };
    QStringList loaded;
    for (const QString& dir : dirs) {
        if (dir.isEmpty())
            continue;
        const QStringList fontDirs{
            QDir(dir).filePath(QStringLiteral("fonts")),
            // Tip package / CDN things → Contents/Resources/assets/fonts.
            QDir(dir).filePath(QStringLiteral("Contents/Resources/assets/fonts")),
            QDir(dir).filePath(QStringLiteral("Resources/assets/fonts")),
            QDir(dir).filePath(QStringLiteral("assets/fonts")),
        };
        for (const QString& fonts : fontDirs) {
            for (const QString& face : faces) {
                if (loaded.contains(face))
                    continue;
                QFile file(QDir(fonts).filePath(face));
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                if (QFontDatabase::addApplicationFontFromData(file.readAll()) >= 0)
                    loaded << face;
            }
        }
    }
    if (loaded.isEmpty())
        qWarning("Verdana is not installed and data/fonts has no Verdana faces; UI text will not match the desktop client");
    else
        qInfo("loaded %d Verdana face(s) for the UI", static_cast<int>(loaded.size()));
}

void alignTextHinting(QQmlEngine* engine)
{
#if !defined(Q_OS_WASM)
    Q_UNUSED(engine);
#else
    // CIP sets PreferDefaultHinting. On the desktop that is CoreText, which
    // keeps Verdana's advances, so the 1px outline sits in the gap. On wasm
    // the same setting is FreeType full hinting, which rewrites those advances
    // and the outline of one letter lands on the next. Vertical-only hinting
    // is the FreeType mode that leaves horizontal advances alone.
    if (!engine)
        return;
    QObject* style = engine->singletonInstance<QObject*>(QStringLiteral("qmlcomponents"), QStringLiteral("TibiaStyle"));
    if (!style) {
        qWarning("TibiaStyle is not loaded; browser text hinting stays at the platform default");
        return;
    }
    QFont font = style->property("defaultTextFont").value<QFont>();
    font.setHintingPreference(QFont::PreferVerticalHinting);
    style->setProperty("defaultTextFont", font);
#endif
}

void installTranslations()
{
    auto* translator = new AppNameTranslator(QCoreApplication::instance());
    if (translator->load(QStringLiteral(":/strings/client.en.qm")))
        QCoreApplication::installTranslator(translator);
}

std::unique_ptr<QQmlEngine> createEngine()
{
    cip::registerQmlTypes();

    auto engine = std::make_unique<QQmlEngine>();
    // Always qrc baseUrl so absolute /images/… and the cmake directory qmldir
    // stay on resources. Disk mode remaps .qml/.js via the interceptor only —
    // never addImportPath(cip), which surfaces CIP's plugin qmldir on file://.
    engine->setBaseUrl(QUrl(QStringLiteral("qrc:/qt/qml/qmlcomponents/qml/")));
    // Runtime packs: ModLoader recorded mods/<id>/qml import roots at startup.
    for (const std::string& path : tibia::mods::packQmlImportPaths()) {
        const QString qpath = QString::fromStdString(path);
        if (QDir(qpath).exists())
            engine->addImportPath(qpath);
    }
#if defined(TIBIA_QML_DISK)
    // DEV: also import from source pack qml/ so `import idleot` + disk remap stay in sync.
    // --no-mods drops pack qml/ and pack overrides baked into the binary; engine
    // client/overrides stay. Pack trees live under .../mods/.
    QString diskModDirs = QStringLiteral(TIBIA_QML_MOD_DIR);
    QString diskOverrideDirs = QStringLiteral(TIBIA_QML_OVERRIDES_DIR);
    if (tibia::mods::ModLoader::skipLoad()) {
        diskModDirs.clear();
        QStringList keptOverrides;
        for (const QString& root : diskOverrideDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
            if (!root.contains(QLatin1String("/mods/")))
                keptOverrides.push_back(root);
        }
        diskOverrideDirs = keptOverrides.join(QLatin1Char(':'));
    } else {
        for (const QString& root : diskModDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
            if (QDir(root).exists())
                engine->addImportPath(root);
        }
    }
    engine->addUrlInterceptor(new DiskQmlInterceptor(QStringLiteral(TIBIA_QML_CIP_DIR),
                                                     diskModDirs,
                                                     diskOverrideDirs));
    installQmlDiskHotReload(engine.get(), engine.get());
#endif
    // Production / fallback: qrc:/qt/qml/<module>/… → staged mods/<id>/qml/<module>/…
    engine->addUrlInterceptor(new PackQmlInterceptor());
#ifdef Q_OS_MACOS
    // macdeployqt symlinks QML plugins from Resources/qml to PlugIns/quick; the launcher
    // installs no symlinks, so load them from PlugIns/quick directly.
    const QString quickPlugins = QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/quick");
    if (QDir(quickPlugins).exists())
        engine->addPluginPath(quickPlugins);
#endif
    imageproviders::registerImageProviders(engine.get());

    auto* cursor = new gamewindow::TMouseCursorController(ClientOptions::load().options(), engine.get());
    engine->rootContext()->setContextProperty(QStringLiteral("tibiaMouseCursorController"), cursor);
    auto* textCopy = new gamewindow::TTextCopyController(engine.get());
    engine->rootContext()->setContextProperty(QStringLiteral("tibiaTextCopyController"), textCopy);
    // Root context objects CIP's components test for null before use.
    engine->rootContext()->setContextProperty(QStringLiteral("tibiaContextMenuController"),
                                              QVariant::fromValue<QObject*>(nullptr));
    // CIP 0x1009095e0: panels never cache into layers.
    engine->rootContext()->setContextProperty(QStringLiteral("UICachingEnabled"), false);

#if !defined(TIBIA_QML_DISK)
    auto interceptor = std::make_unique<OverrideInterceptor>();
    if (!interceptor->isEmpty())
        engine->addUrlInterceptor(interceptor.release());
#endif
    return engine;
}

}
