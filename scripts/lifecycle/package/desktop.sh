#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OTC="$(cd "$HERE/../../.." && pwd)"
HOST_ID="${QTCLIENT_HOST_BUILD_ID:-${SOURCE_COMMIT:-${GITHUB_SHA:-}}}"
if [[ -z "$HOST_ID" ]]; then
  HOST_ID="$(git -C "$OTC" rev-parse HEAD 2>/dev/null || echo unknown)"
fi
export QTCLIENT_HOST_BUILD_ID="$HOST_ID" SOURCE_COMMIT="$HOST_ID"

# shellcheck source=_pack.sh
source "$HERE/_pack.sh"
# Prefer QTCLIENT_PLATFORM: on Windows, MSVC's Platform (case-insensitive)
# collides with env PLATFORM and Actions drops the step value.
PLATFORM="${QTCLIENT_PLATFORM:-${PLATFORM:-}}"
PLATFORM="${PLATFORM:?QTCLIENT_PLATFORM (or PLATFORM) required for desktop package}"
OUT="${PACKAGE_OUT:-${RUNNER_TEMP:-$OTC/build/package}/client-$PLATFORM}"
bin_dir="${BIN_DIR:-}"
if [[ -z "$bin_dir" ]]; then
  bin="$("$OTC/scripts/lifecycle/lib/find-client-bin.sh" "$OTC")"
  # package_desktop.py --bin wants the TOGGLE_BIN_FOLDER dir that holds
  # client.app / client / client.exe (build/<preset>/bin), not macOS
  # applicationDirPath (…/client.app/Contents/MacOS).
  case "$bin" in
    *.app/Contents/MacOS/*)
      bin_dir="$(cd "$(dirname "$bin")/../../.." && pwd)"
      ;;
    *)
      bin_dir="$(dirname "$bin")"
      ;;
  esac
fi
[[ -d "$bin_dir" ]] || { echo "[desktop] bin dir missing: $bin_dir" >&2; exit 1; }
pack_desktop "$PLATFORM" "$bin_dir" "$OUT"
echo "PACKAGE_OUT=$OUT"
