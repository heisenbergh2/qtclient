#!/usr/bin/env bash
# Point a client binary's CipSoft Resources tree at apps/qtclient/data
# (siblings assets/ + sounds/). Usage: link-cip-resources.sh <client-bin> [data-dir]
set -euo pipefail

BIN=${1:?client binary}
QTCLIENT_DATA=${2:-}
if [[ -z "$QTCLIENT_DATA" ]]; then
  HERE="$(cd "$(dirname "$0")" && pwd)"
  QTCLIENT_DATA="$(cd "$HERE/../../../data" && pwd)"
fi
QTCLIENT_DATA="$(cd "$QTCLIENT_DATA" && pwd)"

if [[ ! -f "$QTCLIENT_DATA/assets/catalog-content.json" ]]; then
  echo "[tibia] skip Resources link — no catalog at $QTCLIENT_DATA/assets (run make extract)"
  exit 0
fi

# macOS app: …/Contents/MacOS/client → …/Contents/Resources
# Linux / bare: …/client → …/Resources (sibling of binary dir's parent when nested)
if [[ "$BIN" == */Contents/MacOS/* ]]; then
  RES="$(cd "$(dirname "$BIN")/.." && pwd)/Resources"
else
  RES="$(cd "$(dirname "$BIN")" && pwd)/Resources"
fi

if [[ -L "$RES" ]]; then
  current="$(readlink "$RES" || true)"
  if [[ "$current" == "$QTCLIENT_DATA" ]]; then
    exit 0
  fi
  rm -f "$RES"
elif [[ -d "$RES" ]]; then
  # Real directory (packaged install) — do not clobber.
  if [[ -f "$RES/assets/catalog-content.json" ]]; then
    exit 0
  fi
  echo "[tibia] warn: $RES exists and is not a symlink; leaving in place"
  exit 0
elif [[ -e "$RES" ]]; then
  echo "[tibia] warn: $RES exists and is not a directory; leaving in place"
  exit 0
fi

ln -s "$QTCLIENT_DATA" "$RES"
echo "[tibia] Resources → $QTCLIENT_DATA ($RES)"
