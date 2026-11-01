# Demo tip (qtclient)

CIP launcher protocol with **our** client package and **CipSoft** assets.
Login comes from the single tip pack [`mods/default`](../../client/mods/default/mod.json)
([myAAC](https://github.com/slawkens/myaac) at `http://127.0.0.1/login.php`).
`--no-mods` (no pack) talks to real CipSoft. product tip **replaces** `mods/default`
with `mods/pack` (never stacked).

| Tip field | Points at |
|-----------|-----------|
| `url` / `versionurl` | qtclient GitHub Release `package.json` (+ `.version`) |
| `assetsurl` / `assetsversionurl` | `https://static.tibia.com/launcher/assets-current/…` |
| `hints` | optional CIP hints |

Example tip: [`tibiametadata.demo.json`](tibiametadata.demo.json).

## Tester recipe (myAAC + Crystal)

1. Run [myAAC](https://github.com/slawkens/myaac) so `http://127.0.0.1/login.php` answers CIP JSON (`type: login`).
2. Run Crystal (or another OT server) on the game port myAAC advertises (typically `7172`).
3. Download the qtclient demo Release for your OS (launcher + tip). Do **not** need a local Tibia install or CIP assets in git.
4. Start the launcher (sidecar `tibiametadata.json` or `QTCLIENT_TIP_URL=…`).
5. **Update** installs our client package + CipSoft `assets-current` (package includes `mods/default`).
6. **Play** logs in at `http://127.0.0.1/login.php` with no env tweaks.

Force the default pack from a tip that also has product (or with `QTCLIENT_MODS_DIR` set):

```bash
./client --default-mod
# or: make qtclient client run ARGS='--default-mod'
```

Override AAC host/path without rebuilding (env escape hatch, wins over mod):

```bash
export QTCLIENT_LOGIN_HOST=aac.example.com
export QTCLIENT_LOGIN_PATH=/login.php
```

Or edit/reinstall `mods/default/mod.json` `urls` — no host rebuild.

## product portal

Product pack in `apps/pack-client` (`QTCLIENT_PACK`). Tip install:
`make qtclient client install` merges `mods/pack` (with MODULE) and removes
`mods/default`. Run loads that tip pack only.

## Running a tip

```bash
make demo-pack && make demo-run
```

**ToS:** Fetching CipSoft `assetsurl` content is subject to CipSoft Terms of
Service and your licensed Tibia install. Do not redistribute CipSoft blobs.

## Packaging a Release

```bash
make client build && make launcher build
make demo-pack RELEASE_BASE=https://github.com/OWNER/REPO/releases/latest/download
# artifacts under ../../dist/demo/
```

`emit_demo_tip.py` stages `mods/default` next to the Play binary and writes a
revision-only `release_env.lua`.
