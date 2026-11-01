#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class QQmlEngine;
class QQuickItem;
class QObject;
class QImage;

namespace tibia::client {
class TGameClient;
}

namespace tibia::mods {

class MissionBackend;
class AttentionBackend;
class InstanceBackend;

struct ModIdentity {
    std::string id;
    std::string displayName;
    std::string organization;
    std::string organizationDomain;
    std::string bundleId;
    std::string cdnBaseUrl;
    int priority = 100;
};

/// CIP `[URLS]`-shaped login/AAC config from mod.json `urls` (no host rebuild to change).
struct ModUrls {
    std::string loginBase; ///< Full origin, e.g. http://127.0.0.1 or http://127.0.0.1:5173
    std::string loginPath;
    std::string clientServicesPath;
    std::string createAccountPath;
    std::string assetBase;
    std::string paymentsBase;
    /// QTCLIENT_URL_* page overrides (path or absolute URL).
    std::unordered_map<std::string, std::string> pages;
    int priority = 100;
    bool empty() const
    {
        return loginBase.empty() && loginPath.empty() && clientServicesPath.empty()
            && createAccountPath.empty() && assetBase.empty() && paymentsBase.empty()
            && pages.empty();
    }
};

/// CIP objects product mods may need. Parent long-lived product objects to `host`.
struct ModContext {
    QQmlEngine* engine = nullptr;
    QQuickItem* rootItem = nullptr;
    QQuickItem* mapWindowPane = nullptr;
    QQuickItem* gameWindowItem = nullptr;
    client::TGameClient* gameClient = nullptr;
    QObject* dialogStack = nullptr;
    QObject* host = nullptr;
    /// Optional `app::LauncherEnv*` (typed void to keep registry free of app headers).
    const void* launcherEnv = nullptr;
    std::unordered_map<std::string, QObject*> services;
};

using ModBootstrapFn = void (*)(const ModContext& ctx);

struct ModEntry {
    ModIdentity identity;
    ModBootstrapFn bootstrap = nullptr;
};

void registerMod(ModEntry entry);
const std::vector<ModEntry>& registeredMods();
void bootstrapAll(const ModContext& ctx);
/// Bootstraps that must run before gameClient.start (login chrome, etc.).
void registerEarlyBootstrap(ModBootstrapFn fn);
void earlyBootstrapAll(const ModContext& ctx);
const ModIdentity* primaryIdentity();
/// Register pack urls (sorted by priority ascending; lower wins like identity).
void registerModUrls(ModUrls urls);
/// First non-empty urls after priority sort (IdleOT priority 10 beats myaac 50).
const ModUrls* primaryUrls();
/// Call from mod static initializers so identity is ready before QApplication.
void ensureModsRegistered();
using EnsureModsFn = void (*)();
void setEnsureModsHook(EnsureModsFn fn);

/// Pack `qml/` dirs discovered by ModLoader (startup). Applied when the QML engine is created.
void addPackQmlImportPath(const std::string& absolutePath);
const std::vector<std::string>& packQmlImportPaths();

/// Pack `overrides/` roots (mods/<id>/overrides). Remap qrc:/qt/qml/qmlcomponents/…
/// onto FS files at runtime (same layout as configure-time CIP forks; live-reload friendly).
void addPackOverrideRoot(const std::string& absolutePath);
const std::vector<std::string>& packOverrideRoots();

using EffectTintFn = uint8_t (*)(uint16_t effectId);
using EffectVariantTintFn = uint8_t (*)(uint32_t variantId);
using ApplyImageTintFn = void (*)(QImage& image, uint8_t tint);

void setEffectTintHook(EffectTintFn fn);
void setEffectVariantTintHook(EffectVariantTintFn fn);
void setApplyImageTintHook(ApplyImageTintFn fn);
EffectTintFn effectTintHook();
EffectVariantTintFn effectVariantTintHook();
ApplyImageTintFn applyImageTintHook();

void setMissionBackend(MissionBackend* backend);
MissionBackend* missionBackend();
void setAttentionBackend(AttentionBackend* backend);
AttentionBackend* attentionBackend();
void setInstanceBackend(InstanceBackend* backend);
InstanceBackend* instanceBackend();

using LoginSyncEnqueueFn = void (*)(std::function<void()> fn);
void setLoginSyncEnqueueHook(LoginSyncEnqueueFn fn);
void enqueueLoginSync(std::function<void()> fn);

} // namespace tibia::mods
