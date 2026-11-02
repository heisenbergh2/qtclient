#!/usr/bin/env bash
# Merge a built IdleOT pack into the qtclient tip so ModLoader finds mods/idleot
# next to the Play binary (CIP tip sync — no separate mods protocol).
#
# --tip is the applicationDirPath (desktop) or the directory that contains
# tibia.wasm (wasm). Exactly one destination: <tip>/mods/idleot
# Removes mods/default (and any other sibling packs) so tip has a single product slot.
#
# Usage:
#   merge-pack-into-tip.sh --pack <mods/idleot tree> --tip <applicationDirPath>
set -euo pipefail

PACK=""
TIP=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pack) PACK="${2:?}"; shift 2 ;;
    --tip) TIP="${2:?}"; shift 2 ;;
    --platform) shift 2 ;; # accepted for callers; dest layout is tip/mods/idleot
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done
[[ -n "$PACK" && -n "$TIP" ]] || {
  echo "usage: $0 --pack <dir> --tip <applicationDirPath>" >&2
  exit 1
}
test -f "$PACK/mod.json" || { echo "missing $PACK/mod.json" >&2; exit 1; }
test -d "$TIP" || { echo "missing tip dir $TIP" >&2; exit 1; }

# Engine root → Contents/MacOS so ModLoader sees mods next to the Play binary.
# Callers often pass the Soft client tip root (…/engine) that contains *.app.
macos="$(find "$TIP" -maxdepth 3 -path '*/Contents/MacOS' -type d 2>/dev/null | head -1 || true)"
if [[ -n "$macos" && "$TIP" != "$macos" ]]; then
  echo "[merge-pack] tip → $macos (Contents/MacOS)"
  TIP="$macos"
fi

mods_root="$TIP/mods"
# Single tip pack: drop demo default + any leftover siblings before installing idleot.
if [[ -d "$mods_root" ]]; then
  for sibling in "$mods_root"/*; do
    [[ -e "$sibling" ]] || continue
    base="$(basename "$sibling")"
    [[ "$base" == idleot || "$base" == idleot.files ]] && continue
    rm -rf "$sibling"
  done
fi

dest="$mods_root/idleot"
rm -rf "$dest"
mkdir -p "$mods_root"
cp -a "$PACK" "$dest"
test -f "$dest/mod.json"
(
  cd "$dest"
  find . -type f | sed 's|^\./||' | sort
) >"${dest}.files"
# Manifest lives beside the tree as mods/idleot.files (${dest}.files).
echo "[merge-pack] ${PACK} → ${dest} ($(wc -l < "${dest}.files" | tr -d ' ') files)"
