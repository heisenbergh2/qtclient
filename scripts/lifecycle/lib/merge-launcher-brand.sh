#!/usr/bin/env bash
# Stage a launcher brand pack next to qtclient launcher tip (mods/<id>/).
# Soft binary stays CipSoft; product identity + art load at runtime unless --no-mods.
#
# --tip is applicationDirPath (desktop binary dir / Contents/MacOS, or wasm dir
# with tibia.wasm). When TIP is an engine root that contains *.app/Contents/MacOS,
# resolve to that MacOS dir so ModLoader finds mods next to the Play binary.
#
# Usage:
#   merge-launcher-brand.sh --pack <launcher/mods tree> --tip <applicationDirPath>
set -euo pipefail

PACK=""
TIP=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pack) PACK="${2:?}"; shift 2 ;;
    --tip) TIP="${2:?}"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done
[[ -n "$PACK" && -n "$TIP" ]] || {
  echo "usage: $0 --pack <dir> --tip <applicationDirPath>" >&2
  exit 1
}
test -f "$PACK/brand.json" || { echo "missing $PACK/brand.json" >&2; exit 1; }
test -d "$TIP" || { echo "missing tip dir $TIP" >&2; exit 1; }

# Engine root → Contents/MacOS (macOS Soft tip).
macos="$(find "$TIP" -maxdepth 3 -path '*/Contents/MacOS' -type d 2>/dev/null | head -1 || true)"
if [[ -n "$macos" && "$TIP" != "$macos" ]]; then
  echo "[merge-launcher-brand] tip → $macos (Contents/MacOS)"
  TIP="$macos"
fi

id="$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); i=d.get("id") or "";
assert i, "brand.json missing id"; print(i)' "$PACK/brand.json")"
dest="$TIP/mods/$id"
# Merge into existing tip pack (do not wipe native/qml from merge-pack).
mkdir -p "$dest"
cp -a "$PACK/brand.json" "$dest/"
# Never clobber a pack mod.json that ModLoader can load (needs non-empty id).
# Launcher chrome is brand.json + images; product pack owns mod.json.
if [[ -f "$PACK/mod.json" ]]; then
  if [[ -f "$dest/mod.json" ]] && python3 -c '
import json,sys
d=json.load(open(sys.argv[1]))
sys.exit(0 if (d.get("id") or "").strip() else 1)
' "$dest/mod.json"; then
    echo "[merge-launcher-brand] keep existing pack mod.json (has id)"
  else
    cp -a "$PACK/mod.json" "$dest/"
  fi
fi
if [[ -d "$PACK/images" ]]; then
  mkdir -p "$dest/images"
  cp -a "$PACK/images/." "$dest/images/"
fi
echo "[merge-launcher-brand] ${PACK} → ${dest}"
