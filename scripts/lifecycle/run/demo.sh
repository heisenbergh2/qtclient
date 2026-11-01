#!/usr/bin/env bash
# Stock demo launcher: CipSoft assets via tip; login via tip client mods/default.
# Unsets IdleOT DEV_JSON so Play uses real tip install (not local bypass).
#
# Usage (from apps/qtclient or via make demo-run):
#   bash scripts/lifecycle/lib/start-demo-launcher.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OTC="${TIBIA_CLIENT_DIR:-$(cd "$HERE/../../.." && pwd)}"
ROOT="$(cd "$OTC/../.." && pwd)"
TIP_DIR="${QTCLIENT_DEMO_TIP:-$ROOT/dist/demo/tip}"

# shellcheck source=desktop-paths.sh
source "$HERE/../lib/desktop-paths.sh"
qtclient_desktop_paths "$OTC"

if [[ ! -x "$QTCLIENT_LAUNCHER_BIN" ]]; then
  echo "[demo] missing $QTCLIENT_LAUNCHER_BIN — make qtclient launcher build" >&2
  exit 1
fi

# Demo rehearsal: never use DEV Play bypass (that skips CipSoft asset install).
unset QTCLIENT_DEV_JSON || true
unset QTCLIENT_AUTOPLAY || true
# Login comes from tip mods/default — do not force --no-mods on Play.
unset QTCLIENT_CLIENT_ARGS || true
unset QTCLIENT_MODS_DIR || true
# Clear any leftover IdleOT portal login env escape hatch.
unset QTCLIENT_PORTAL_URL || true
unset QTCLIENT_LOGIN_SCHEME QTCLIENT_LOGIN_HOST QTCLIENT_LOGIN_PORT || true
unset QTCLIENT_LOGIN_PATH QTCLIENT_CLIENTSERVICES_PATH QTCLIENT_CREATE_ACCOUNT_PATH || true
unset QTCLIENT_ASSET_BASE || true

if [[ -f "$TIP_DIR/tibiametadata.json" ]]; then
  export QTCLIENT_TIP_URL="${QTCLIENT_TIP_URL:-file://$TIP_DIR/tibiametadata.json}"
  echo "[demo] tip $QTCLIENT_TIP_URL"
elif [[ -n "${QTCLIENT_TIP_URL:-}" ]]; then
  echo "[demo] tip $QTCLIENT_TIP_URL"
else
  echo "[demo] no tip yet — generate with:" >&2
  echo "  make -C \"$OTC\" demo-pack   # or set QTCLIENT_TIP_URL" >&2
  echo "[demo] launcher will still start (CDN/empty tip may apply)" >&2
fi

echo "[demo] starting $QTCLIENT_LAUNCHER_BIN (Play → tip client + mods/default)"
cd "$OTC"
# Launcher chrome stays stock CIP; tip client loads mods/default (myAAC).
exec "$QTCLIENT_LAUNCHER_BIN" --no-mods "$@"
