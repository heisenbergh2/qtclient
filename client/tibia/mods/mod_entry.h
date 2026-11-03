#pragma once

#include "mods/registry.h"

#include <QtGlobal>

/// Pack native entry — linked into the tip (STATIC). Soft calls this when
/// QTCLIENT_HAS_LINKED_PACK is defined; FS mods/<id>/ still supplies QML/urls.
using QtClientModEntryFn = void (*)(const tibia::mods::ModContext* earlyCtx);

#define QTCLIENT_MOD_ENTRY_NAME "qtclient_mod_entry"

#if defined(_WIN32)
#  define QTCLIENT_MOD_EXPORT extern "C" Q_DECL_EXPORT
#else
#  define QTCLIENT_MOD_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#if defined(QTCLIENT_HAS_LINKED_PACK)
QTCLIENT_MOD_EXPORT void qtclient_mod_entry(const tibia::mods::ModContext* earlyCtx);
#endif
