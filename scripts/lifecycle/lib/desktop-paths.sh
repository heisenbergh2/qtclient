#!/usr/bin/env bash
# qtclient desktop layout (TOGGLE_BIN_FOLDER=ON):
#   build/<preset>/bin/client[.app]/…]
#   build/<preset>/bin/launcher[.app|/…]
#
# Usage (from another script):
#   # shellcheck source=desktop-paths.sh
#   source "$HERE/desktop-paths.sh"
#   qtclient_desktop_paths   # sets QTCLIENT_* vars; exit 2 if host unsupported
#
# Optional env (same meaning as make / build-tibia.sh):
#   DEV=1|0   DEBUG=1|0   TIBIA_PRESET=macos-dev
#
# Library — do not `set -e` here; callers own shell options.

qtclient_desktop_preset() {
  if [[ -n "${TIBIA_PRESET:-}" ]]; then
    printf '%s\n' "$TIBIA_PRESET"
    return 0
  fi
  local os base=release
  case "$(uname -s)" in
    Darwin) os=macos ;;
    Linux) os=linux ;;
    MINGW*|MSYS*|CYGWIN*) os=windows ;;
    *)
      echo "[desktop-paths] unsupported host: $(uname -s)" >&2
      return 2
      ;;
  esac
  # Match build-tibia.sh: release default, DEV→*-dev, DEBUG→*-debug (DEBUG wins).
  # Local qtclient runs default DEV=1 so they hit the same tree as umbrella builds.
  # Windows has no *-dev preset — stay on release (or debug).
  if [[ "$os" != windows ]]; then
    case "${DEV:-1}" in 0|false|FALSE|no|NO|off|OFF) ;;
      *) base=dev ;;
    esac
  fi
  case "${DEBUG:-}" in 1|true|TRUE|yes|YES|on|ON) base=debug ;; esac
  printf '%s\n' "${os}-${base}"
}

# Sets:
#   QTCLIENT_PRESET QTCLIENT_BUILD_DIR
#   QTCLIENT_CLIENT_BIN QTCLIENT_LAUNCHER_BIN
#   QTCLIENT_CLIENT_APP QTCLIENT_LAUNCHER_APP  (macOS .app bundle dirs; empty elsewhere)
qtclient_desktop_paths() {
  local otc="${1:-${TIBIA_CLIENT_DIR:-}}"
  if [[ -z "$otc" ]]; then
    echo "[desktop-paths] missing qtclient root" >&2
    return 2
  fi
  QTCLIENT_PRESET="$(qtclient_desktop_preset)" || return 2
  QTCLIENT_BUILD_DIR="$otc/build/$QTCLIENT_PRESET"
  local bin="$QTCLIENT_BUILD_DIR/bin"
  QTCLIENT_CLIENT_APP=""
  QTCLIENT_LAUNCHER_APP=""
  case "$(uname -s)" in
    Darwin)
      QTCLIENT_CLIENT_APP="$bin/client.app"
      QTCLIENT_LAUNCHER_APP="$bin/launcher.app"
      QTCLIENT_CLIENT_BIN="$QTCLIENT_CLIENT_APP/Contents/MacOS/client"
      QTCLIENT_LAUNCHER_BIN="$QTCLIENT_LAUNCHER_APP/Contents/MacOS/launcher"
      ;;
    Linux)
      QTCLIENT_CLIENT_BIN="$bin/client"
      QTCLIENT_LAUNCHER_BIN="$bin/launcher"
      ;;
    MINGW*|MSYS*|CYGWIN*)
      QTCLIENT_CLIENT_BIN="$bin/client.exe"
      QTCLIENT_LAUNCHER_BIN="$bin/launcher.exe"
      ;;
    *)
      echo "[desktop-paths] unsupported host: $(uname -s)" >&2
      return 2
      ;;
  esac
  export QTCLIENT_PRESET QTCLIENT_BUILD_DIR
  export QTCLIENT_CLIENT_BIN QTCLIENT_LAUNCHER_BIN
  export QTCLIENT_CLIENT_APP QTCLIENT_LAUNCHER_APP
}
