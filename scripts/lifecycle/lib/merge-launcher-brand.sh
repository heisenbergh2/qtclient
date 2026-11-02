#!/usr/bin/env bash
# Stage a launcher brand pack next to qtclient launcher tip (mods/<id>/).
# Soft binary stays CipSoft; product identity + art load at runtime unless --no-mods.
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

id="$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); i=d.get("id") or "";
assert i, "brand.json missing id"; print(i)' "$PACK/brand.json")"
dest="$TIP/mods/$id"
# Merge into existing tip pack (do not wipe native/qml from merge-pack).
mkdir -p "$dest"
cp -a "$PACK/brand.json" "$dest/"
[[ -f "$PACK/mod.json" ]] && cp -a "$PACK/mod.json" "$dest/"
if [[ -d "$PACK/images" ]]; then
  mkdir -p "$dest/images"
  cp -a "$PACK/images/." "$dest/images/"
fi
echo "[merge-launcher-brand] ${PACK} → ${dest}"
