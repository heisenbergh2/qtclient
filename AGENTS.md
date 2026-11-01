# AGENTS.md — qtclient

qtclient is a **CipSoft client port** (Qt/QML). CipSoft / CIP is the only client
upstream. Do not treat other open clients as upstream or as a behavioral source.

## CIP as upstream (mandatory)

- Name the company **CipSoft** (or **CIP**). Never abbreviate it as **Soft**.
- `client/tibia/` — C++/QML rewrite of the official client (desktop + WASM).
- **`client/upstream/` is vendored, never hand-edited.** From `make tibia-sync` / `make sync`.
- Product UI belongs in **runtime packs** (`client/mods/<id>/`), not stock `client/tibia/`.
- **No Lua gameplay host** — QML + C++ only.

## Do not

- Commit `upstream/` or CipSoft asset blobs.
- Hand-edit generated CIP QML under `upstream/`.
- Add a second client upstream or copy behavior from other open clients.
