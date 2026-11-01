# Changelog

All notable changes to the public qtclient tree are documented here.
Format inspired by [Keep a Changelog](https://keepachangelog.com/).
Versioning follows semver for public tags (`vX.Y.Z`).

## [Unreleased]

### Added

- Mod SDK: unified `mods/<id>/` pack layout, `qtclient_add_mod`,
  `ModLoader::loadFromDirectory`, and WorldView customization hooks
  (lighting driver, overlays, frame hooks).
- Public Mod API v1 (`include/qtclient/mod_api_v1.h` + `qtclient_mod_host_api_v1()`).
- Filesystem QML modules (`qml/<uri>/qmldir`) + `PackQmlInterceptor` for
  `qrc:/qt/qml/<uri>/…` → pack files (no STATIC qml plugin in the MODULE).
- Sample pack under `client/mods/_sample` demonstrating the SDK.
- SECURITY.md and clarified NOTICE (non-affiliation, BYO CipSoft materials).

### Changed

- Stock core is CIP classic lighting only; product raycast/weather live in
  packs via the WorldView API — no export/scrub pipeline.
- Product identity and launcher brand resolve from `mod.json` at runtime.
- Process env / WASM hooks: `QTCLIENT_*` in stock, `IDLEOT_*` in the product pack;
  `TIBIA_*` kept for CipSoft-shaped CMake/target names only.
- Source roots match include prefixes (no symlinks): `client/tibia/`,
  `client/mods/pack/pack/`.
- ModLoader: API min/max check, optional `hostBuildId`, import path is pack `qml/` only.

## [0.1.0] — 2026-03-28

### Added

- CipSoft-compatible Qt/QML client spun out as qtclient (otqt): CIP QML +
  `client/tibia/`, mods discovery, BYO assets.
- Session replay: `--record` / `--replay`; fixtures under `testdata/sessiondumps/`.
- First-run: `make setup` and release helper `qtclient-fetch-assets`.
