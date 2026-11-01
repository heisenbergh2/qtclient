# Mods

Stock CipSoft is **not** under `mods/` (`upstream/` + `client/tibia/`).

| OTC | This client |
|-----|-------------|
| `modules/` (stock) | CIP stock |
| Customizing stock | `client/overrides/` |
| `mods/<name>/` | `mods/<name>/` + `mod.json` + `qml/<uri>/` |

## Tracked packs

| Pack | Role |
|------|------|
| `_sample/` | SDK example |
| `default/` | Demo tip myAAC urls (`/login.php`); staged by `make demo-pack` |

product lives in `apps/pack-client` (`QTCLIENT_PACK` in root `.env`).
`make qtclient client install` merges into tip `mods/pack` (replaces `default`).

## Inplace (gitignored)

Other packs under this directory are gitignored. Drop private packs here:

```bash
ln -s ~/my-pack mods/my_pack
# product (umbrella): make qtclient client install   # QTCLIENT_PACK in .env
```

Enable with default `QTCLIENT_ENABLE_MODS=ON` (auto-discovers non-`_*` packs).
Stock-only: `-DQTCLIENT_ENABLE_MODS=OFF`.

QML must be a filesystem module: `qml/<qmlModule>/qmldir` (see [`docs/mods.md`](../../docs/mods.md)).
Public C++ surface: [`include/qtclient/mod_api_v1.h`](../../include/qtclient/mod_api_v1.h).

See [`docs/mods.md`](../../docs/mods.md) and [`_sample/`](_sample/).
