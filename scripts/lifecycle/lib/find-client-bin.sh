#!/usr/bin/env bash
# qtclient Play binary under the active preset: build/<preset>/bin/…
# Usage: find-client-bin.sh [qtclient-root]
# Env: DEV DEBUG TIBIA_PRESET (see desktop-paths.sh)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=desktop-paths.sh
source "$HERE/desktop-paths.sh"

OTC="${1:-${TIBIA_CLIENT_DIR:-$(cd "$HERE/../../.." && pwd)}}"
qtclient_desktop_paths "$OTC"
# Windows .exe may not report -x under Git Bash — accept -e.
if [[ -e "$QTCLIENT_CLIENT_BIN" ]]; then
  printf '%s\n' "$QTCLIENT_CLIENT_BIN"
  exit 0
fi

# Fallback: desktop.sh may have built a different preset than DEV implies
# (e.g. CI release tip while a local shell still exports DEV=1).
_os="$(uname -s)"
case "$_os" in
  Darwin) _prefix=macos ;;
  Linux) _prefix=linux ;;
  MINGW*|MSYS*|CYGWIN*) _prefix=windows ;;
  *)
    echo "[find-client-bin] missing $QTCLIENT_CLIENT_BIN (preset=$QTCLIENT_PRESET)" >&2
    exit 1
    ;;
esac
for _base in release dev debug; do
  [[ "$_prefix" == windows && "$_base" == dev ]] && continue
  _preset="${_prefix}-${_base}"
  [[ "$_preset" == "$QTCLIENT_PRESET" ]] && continue
  TIBIA_PRESET="$_preset" qtclient_desktop_paths "$OTC"
  if [[ -e "$QTCLIENT_CLIENT_BIN" ]]; then
    echo "[find-client-bin] using preset=$_preset ($QTCLIENT_CLIENT_BIN)" >&2
    printf '%s\n' "$QTCLIENT_CLIENT_BIN"
    exit 0
  fi
done
echo "[find-client-bin] missing client binary under build/${_prefix}-{release,dev,debug}/bin" >&2
exit 1
