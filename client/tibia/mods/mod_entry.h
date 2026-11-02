#pragma once

#include "mods/registry.h"

#include <QtGlobal>

#if defined(__EMSCRIPTEN__)
#  include <emscripten.h>
#  define QTCLIENT_MOD_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#  define QTCLIENT_MOD_KEEPALIVE
#endif

#if defined(_WIN32)
#  define QTCLIENT_MOD_EXPORT extern "C" Q_DECL_EXPORT
#else
#  define QTCLIENT_MOD_EXPORT extern "C" QTCLIENT_MOD_KEEPALIVE __attribute__((visibility("default")))
#endif

/// Pack native entry. Same symbol on desktop shared libs and wasm SIDE_MODULE.
/// Called once after QLibrary::load / dlopen — register mods, drivers, overlays here.
using QtClientModEntryFn = void (*)(const tibia::mods::ModContext* earlyCtx);

#define QTCLIENT_MOD_ENTRY_NAME "qtclient_mod_entry"
