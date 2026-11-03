# Mods and WorldView customization

## Stock = CipSoft

- `client/upstream/` — vendored CIP QML / shaders / strings (`make sync`)
- `launcher/upstream/` — CIP launcher chrome
- `client/tibia/<area>/` — `tibia::<area>` filesystem
- `launcher/src/` — tip-protocol launcher
- `shared/src/` — `shared::` types

Do not invent an OTC-style `modules/` tree that re-hosts stock CIP.
Keep the name **mods** / **pack** (not “plugins” — that collides with Qt QML plugins).

## Linked tip (stock Soft + product pack)

Product C++ **STATIC-links into Soft** at tip build time. Runtime FS chrome stays a
drop-in tree next to the binary (F5 QML). There is **no** `native/mod.*`,
`MAIN_MODULE`, `SIDE_MODULE`, or `host_exports.json`.

```text
<linked Soft tip>/                 # desktop binary dir, or tip with tibia.wasm
  tibia | tibia.js + tibia.wasm    # Soft + pack C++ in one binary
  mods/<id>/
    mod.json                       # id, QTCLIENT_MOD_API, urls?, identity?, qmlModule?
    qml/…  overrides/…  resources/…
```

| Soft build | Result |
|------------|--------|
| Stock (`STOCK_HOST=1` / no `QTCLIENT_LINKED_PACK`) | CIP tip; FS urls/identity packs only |
| Product (`-DQTCLIENT_LINKED_PACK=<pack>/client`) | Soft⊃pack C++ + staged `mods/<id>/` |

Local product path: `make qtclient client|wasm install` (Soft tip with
`QTCLIENT_LINKED_PACK`, then FS sync onto `dist/cdn`). Portal tip is
`IDLEOT_ASSET_BASE` (local nginx `:9080`).

## Pack format (desktop + WASM — same tree)

```text
mods/<id>/
  mod.json              # id, QTCLIENT_MOD_API, autoload, priority, identity?, urls?, qmlModule?
  qml/<qmlModule>/      # filesystem QML module (qmldir + *.qml)
  overrides/…           # optional CIP stock-path forks (runtime remap)
  resources/…           # optional assets
```

### Single tip pack + `urls`

Tip loads **at most one** pack under `mods/<id>/`:

| Mode | Pack |
|------|------|
| `--no-mods` / empty tip | none → real CipSoft (`https://www.tibia.com`, `https://static.tibia.com`) |
| `--default-mod` | force tip `mods/default/` |
| tip default | highest-priority autoload `mods/<id>/` (one product slot) |

**Product local path (single):** set `QTCLIENT_PACK` in the umbrella `.env`, then
`make qtclient client|wasm install` builds Soft with `QTCLIENT_LINKED_PACK` and
stages FS under tip `mods/<id>/`. `make qtclient client run` loads that tip pack.
`QTCLIENT_MODS_DIR` remains a Soft escape hatch for out-of-tree experiments only.

Optional `urls` on `mod.json` merge into `LauncherEnv` (env > mod > `release_env.lua` > CipSoft).

```json
"urls": {
  "loginBase": "http://127.0.0.1",
  "loginPath": "/login.php",
  "clientServicesPath": "/login.php",
  "createAccountPath": "/login.php",
  "pages": { "QTCLIENT_URL_CREATE_ACCOUNT": "/?subtopic=createaccount" }
}
```

| Pack | Role |
|------|------|
| [`mods/default`](../client/mods/default/mod.json) | Demo tip myAAC (`/login.php`); `make demo-pack` |
| Product `mods/<id>/` | Author pack (linked C++ + QML + optional `brand.json`); tip **replaces** `default` |

Urls-only packs (stock Soft) omit linked C++. Product packs rebuild Soft with the pack.

## Tip mods channel (CDN / launcher)

Soft is a **generic CipSoft host**. Product chrome is a tip install channel next to
engine (`url`/`versionurl`) and assets (`assetsurl`):

```text
packages/<directoryname>/
  Contents/…                 # CIP engine (unchanged)
  mods/<id>/                 # ONE product pack (from mods.json)
    brand.json               # optional launcher chrome
    mod.json
    qml/…  overrides/…  resources/…  images/launcher/…
  mods.json.sha256           # stamp written by PackageClient::installMods
```

`tibiametadata.json` `packages[].builds[]` may include:

```json
"modsurl": "…/mods-current/mods.json",
"modsversionurl": "…/mods-current/mods.json.sha256"
```

Empty / omitted = stock CIP (no tip pack). `mods.json` mirrors `assets.json`
(`files[]` with `localfile` under `mods/<id>/…`). Launcher Play requires
engine + assets + **mods** stamps to match when the channel is present.

### Atomic ModLoader

When the tip was built with `QTCLIENT_HAS_LINKED_PACK`:

1. Call linked `qtclient_mod_entry` **first**
2. Then register identity / urls / QML from FS `mods/<id>/`

Stock Soft (no linked pack): FS urls/identity only; a leftover `native/` tree fails closed.

### Author path (any product)

1. Pin Soft SHA
2. `qtclient_add_mod(my_pack …)` under the pack `client/` (STATIC into Soft)
3. Build Soft with `-DQTCLIENT_LINKED_PACK=<pack>/client`
4. Publish tip + `mods.json` lzma blobs on the CDN
5. Point `tibiametadata` `modsurl` / `modsversionurl` at that tip

Build with one CMake API:

```cmake
include(cmake/QtClientMod.cmake)  # Soft tip already has target tibia
qtclient_add_mod(my_pack
  SOURCES src/entry.cpp …
  QML_DIR qml
  QML_MODULE my_pack   # stages qml/<module>/ + generates qmldir
  OVERRIDES_DIR overrides
  MOD_JSON mod.json
)
```

Authors do **not** choose “plugin vs SIDE_MODULE”. Soft owns the tip link;
`ModLoader::loadFromDirectory` invokes the linked entry then FS chrome.

### WASM linked tip

1. Soft tip is a normal Emscripten executable (no `MAIN_MODULE` / pack `SIDE_MODULE`).
2. Product packs STATIC-link via `QTCLIENT_LINKED_PACK` (keep `qtclient_mod_entry` with
   `LINKER:--undefined=` on WASM).
3. Gates:
   - Soft CI: `_sample` STATIC linked canary + no `native/mod.wasm` under tip
   - Tip presence: `check-tip-pack-presence.sh` (`mods/<id>/mod.json` + content-store)
4. Optional `hostBuildId` on `mod.json` (strict on WASM by default).

```bash
# next to client binary, or tip OPFS mods/
mods/my_pack/mod.json
mods/my_pack/qml/my_pack/qmldir
```

Stock Soft CDN ships host only. Product tips rebuild Soft with the pack linked, then
stage FS chrome. Release refuses a tip whose `mod.json` still declares `native`.

## Pack tiers

| Tier | What | When |
|------|------|------|
| Linked tip | Soft⊃pack C++ + FS `qml/` + `resources/` + `mod.json` | Tip build; CDN / `make qtclient install` |
| Override pack | CIP forks under `overrides/` | **Runtime** `PackQmlInterceptor` remap |

### Filesystem QML modules (F5 hot reload)

Qt resolves `import <qmlModule>` as `$importPath/<qmlModule>/qmldir`.
ModLoader adds **`mods/<id>/qml`** as the import path (pack QML only — not
`overrides/`). Product C++ may keep `qrc:/qt/qml/<module>/…` URLs —
`PackQmlInterceptor` remaps them onto the FS module. Prefer
`makeModuleComponent(engine, "TypeName")` for `QQmlComponent` loads so sibling
types resolve after cache clears.

Do **not** bake STATIC `qt_add_qml_module` into the tip — that fights desktop F5.

| Build | QML source | Hot reload |
|-------|------------|------------|
| Umbrella `make qtclient client build` (`DEV=1` → `TIBIA_QML_DISK`) | Live pack `qml/` + override source trees via `DiskQmlInterceptor`; file watch + **F5** clears component cache | Yes (desktop) |
| Umbrella `make stock client build` | Soft stock host (no pack) | — |
| Production `make -C apps/qtclient client build` | Staged `mods/<id>/qml/<uri>/` via `PackQmlInterceptor` | Edit → restage / rebuild tip |

## Mod API v1

Public header: [`include/qtclient/mod_api_v1.h`](../include/qtclient/mod_api_v1.h).

Resolve with `qtclient_mod_host_api_v1()` (exported from the host). Surface:

| Hook | Role |
|------|------|
| `registerMod` / `registerEarlyBootstrap` | Identity + bootstrap callbacks |
| `setEffectTintHook` / variant / apply | Sprite effect tints |
| `setMissionBackend` / `setAttentionBackend` / `setInstanceBackend` | Product backends |
| `setLoginSyncEnqueueHook` | Login-thread work queue |
| `setWorldLightingDriver` / `addWorldOverlayFactory` / `setWorldFrameHooks` | WorldView |
| `setWeatherDriver` | Weather overlay + sound |
| `setHdScaler` | HD sprite scaler |

Brand identity comes from `mod.json` (`identity` + `priority`) via ModLoader.
Packs register feature bootstraps only (do not re-register a second brand id).

`QTCLIENT_MOD_INTERNAL=1` is a **temporary, non-public** migration bridge for
in-tree deep engine includes. New packs must use `mod_api_v1.h` only
(see [`client/mods/_sample`](../client/mods/_sample)). Do not rely on
`QTCLIENT_MOD_INTERNAL` in out-of-tree packs.

### Versioning

- `mod.json` → `QTCLIENT_MOD_API` must be in `[ModLoader::kMinApi, kSupportedApi]` (currently 1..1). Legacy key `modApi` is not read.
- Optional `hostBuildId` — warn (or reject if `QTCLIENT_MOD_STRICT_HOST=1`) when it differs from the host SHA baked into `mod_api_v1`.
- `qtclient_mod_entry(earlyCtx)` — **register-only** when `earlyCtx` is null (startup load); UI wiring runs in `earlyBootstrapAll` / `bootstrapAll`.

## Overrides

| Layer | Path | When applied |
|-------|------|----------------|
| Engine / Qt-only | `client/overrides/` (e.g. LegacyControls) | Configure-time merge into `qmlcomponents` qrc |
| Pack stock forks | `mods/<pack>/overrides/` (rel. to `client/upstream/qml/`) | **Runtime** remap via `PackQmlInterceptor` (same URI → pack FS) |
| Launcher | `launcher/overrides/` + `launcher/mods/<pack>/` | Launcher build |
| Dev F5 | pack `qml/` + `overrides/` on disk | `TIBIA_QML_DISK` / Pack interceptor; **F5** clears cache — no client rebuild |

Stock Soft publishes once. Product tips rebuild Soft with `QTCLIENT_LINKED_PACK`.
`make tibia-sync --check` still scans pack `overrides/` for `cip-base` drift.

## CMake discovery (dev)

- `QTCLIENT_ENABLE_MODS` (default ON)
- `QTCLIENT_MODS_DIR` (CMake + runtime) default `${TIBIA_ROOT}/client/mods`
- `QTCLIENT_LINKED_PACK` — out-of-tree pack `client/` STATIC into tip
- Empty `QTCLIENT_MODS` = all non-`_*` packs with `mod.json`
- Sample: [`client/mods/_sample`](../client/mods/_sample) — enable with `-DQTCLIENT_MODS=_sample`

## WorldView customization

Stock renderer = CIP sprites + classic light map. Product packs plug in via
cached hooks (null-check only on the hot path):

| Extension | Role |
|-----------|------|
| `WorldLightingDriver` | Optional lighting (pack raycast / custom) |
| `WorldOverlayFactory` | QSGNode siblings after sprites |
| `WorldFrameHooks` | Request shared outdoor/roof gather |
| Sprite effect / HD hooks | `mods/registry` + `sprites/hdscale` |
| `ModContext.services` | Named `QObject*` for QML |

### Performance

- Load mods only at startup / pre-play.
- Per-frame: cached pointers + null checks; no `mod.json` I/O, no directory scans.
- Stock with no packs: CIP path with no product lighting linked.
- Oracle: see [development/mod-perf-oracle.md](development/mod-perf-oracle.md).

## Entry symbol

```c
extern "C" void qtclient_mod_entry(const tibia::mods::ModContext* earlyCtx);
```

Register mods, backends, WorldView drivers/overlays inside this function
(prefer `qtclient_mod_host_api_v1()`). Linked tips keep this symbol past DCE.

## Client + pack

- **`Client: ci`** (public) publishes stock Soft (`client-<plat>:<sha>`, `client-sdk:<sha>`).
- Product release rebuilds Soft with `QTCLIENT_LINKED_PACK`, stages FS chrome, brands the tip, promotes CDN.
- Local: `make qtclient client|wasm install` builds Soft with the pack linked and stages `mods/<id>`. Run loads tip `mods/<id>` only. `make stock client run` / `make stock launcher run` pass `--no-mods`.

## Make CLI

```bash
make stock client build          # CIP host (or DEV disk QML)
make stock client run            # --no-mods (stock client)
make stock launcher run          # --no-mods (stock launcher + CIP chrome)
make qtclient client install     # Soft tip + QTCLIENT_LINKED_PACK + FS chrome
make qtclient launcher install   # Soft launcher + tip brand
make qtclient wasm install       # Soft wasm tip + QTCLIENT_LINKED_PACK + FS chrome
make qtclient client run         # start the linked tip; warns if tip mods/<id> missing
make stock demo-pack && make stock demo-run  # tip with mods/default + CipSoft assetsurl
make stock client run ARGS='--default-mod'   # force myAAC tip pack
make stock client run ARGS='--no-mods'       # real CipSoft URLs
make -C apps/qtclient client build           # app-local Soft stock
```
