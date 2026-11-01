<p align="center">
  <strong>qtclient</strong> · <code>otqt</code><br/>
  CipSoft-compatible <strong>Qt/QML</strong> Tibia client — desktop &amp; browser WASM
</p>

<p align="center">
  <a href="https://github.com/qtclient/qtclient/actions"><img alt="CI" src="https://github.com/qtclient/qtclient/actions/workflows/ci.yml/badge.svg" /></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg" /></a>
</p>

**qtclient** is a ground-up **CipSoft client port**: official CIP QML UI (vendored,
never hand-edited), CIP tip launcher protocol, and a C++ engine checked against
CipSoft behavior. Not affiliated with CipSoft GmbH.

Public repository: **[qtclient/qtclient](https://github.com/qtclient/qtclient)**.

## Why qtclient (vs OTClient)

qtclient is **not** an OTClient fork. It is a CipSoft port: official QML, tip
launcher, and protocol behavior. Use it when you want CIP parity; use OTClient
when you want a Lua/OTUI community stack.

| | **qtclient** | **OTClient** |
|--|--|--|
| UI | CipSoft **QML** (official chrome) | Lua / OTUI |
| Upstream | Official CIP client (`make tibia-sync`) | Community protocol tree |
| Launcher | CIP tip protocol | Custom / third-party updaters |
| Browser | First-class **WASM** | Limited or separate ports |
| Extensibility | Runtime **mods/packs** on a stock CIP host | Usually fork the tree |
| Assets | BYO licensed Tibia / demo tip `assetsurl` | Mixed redistributions |

Built to **match CipSoft** — not to copy or extend OTClient.

### Highlights

- **CIP QML host** — `client/upstream/` from sync; controllers under `client/tibia/`
- **CIP launcher** — tip metadata Update / Play
- **Desktop + WASM**
- **Stock vs packs** — optional `mods/<id>/` at runtime
- **Mod SDK** — `mod_api_v1`, filesystem QML, F5 hot reload in dev
- **Session tools** — `--record` / `--replay`
- **Toolchain** — CMake + Ninja + vcpkg; CipSoft-patched Qt via `make qt`

## Quick start

```bash
git clone https://github.com/qtclient/qtclient.git
cd qtclient
git submodule update --init third_party/Qt5LegacyControls
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
make setup
make client build && make client run
```

BYO licensed `Tibia.app` for sync/extract — CipSoft blobs are never redistributed.

## Docs

| Doc | Topic |
|-----|--------|
| [docs/public/README.md](docs/public/README.md) | Layout |
| [docs/mods.md](docs/mods.md) | Packs |
| [docs/open-source.md](docs/open-source.md) | Public vs packs |
| [SECURITY.md](SECURITY.md) | Security |
| [AGENTS.md](AGENTS.md) | CIP-as-upstream rules |
