#pragma once

#include "mods/registry.h"

#include <QString>
#include <QStringList>

namespace tibia::mods {

/// Unified pack loader (desktop + WASM): directory layout
///   mods/<id>/mod.json + qml/ + overrides/ + resources/
/// Native C++ is STATIC-linked into the tip (QTCLIENT_HAS_LINKED_PACK).
/// Single tip pack at a time. Load only at startup / pre-play.
class ModLoader
{
public:
    static constexpr int kMinApi = 1;
    static constexpr int kSupportedApi = 1;

    /// `--no-mods`: do not load packs for this process. Host → real CipSoft URLs.
    static void setSkipLoad(bool skip);
    static bool skipLoad();

    /// `--default-mod`: load only tip mods/default/ (ignore QTCLIENT_MODS_DIR + idleot).
    static void setForceDefaultMod(bool force);
    static bool forceDefaultMod();

    /// Resolve and load the single selected pack. Returns 0 when skipLoad().
    /// earlyCtx may be null: qtclient_mod_entry is register-only until bootstrapAll.
    static int loadAll(const QStringList& roots = {}, const ModContext* earlyCtx = nullptr);

    /// Load one pack directory containing mod.json.
    static bool loadFromDirectory(const QString& packDir, const ModContext* earlyCtx = nullptr);

    /// Tip mods roots (applicationDirPath()/mods + macOS Resources siblings).
    static QStringList tipModsRoots();

    /// @deprecated Prefer loadAll() with empty roots.
    static QStringList defaultRoots();
};

} // namespace tibia::mods
