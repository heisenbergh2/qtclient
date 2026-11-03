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
if [[ ! -e "$QTCLIENT_CLIENT_BIN" ]]; then
  echo "[find-client-bin] missing $QTCLIENT_CLIENT_BIN (preset=$QTCLIENT_PRESET)" >&2
  exit 1
fi
printf '%s\n' "$QTCLIENT_CLIENT_BIN"
