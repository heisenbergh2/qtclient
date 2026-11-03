#!/usr/bin/env bash
# Fail if a tip is missing the product pack tree, or (with --cdn) if CIP
# package.json packedhashes are absent from the content-addressed store.
#
# Usage:
#   check-tip-pack-presence.sh --tip <dir> [--id idleot] [--cdn <dist/cdn>]
#   check-tip-pack-presence.sh --tip <dir> --require-native   # legacy SIDE_MODULE
# Linked tip: FS chrome only (mod.json + qml/overrides) — default.
set -euo pipefail

TIP=""
CDN=""
PACK_ID="idleot"
REQUIRE_NATIVE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tip) TIP="${2:?}"; shift 2 ;;
    --cdn) CDN="${2:?}"; shift 2 ;;
    --id) PACK_ID="${2:?}"; shift 2 ;;
    --require-native) REQUIRE_NATIVE=1; shift ;;
    --allow-urls-only) REQUIRE_NATIVE=0; shift ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
done

[[ -n "$TIP" ]] || {
  echo "usage: $0 --tip <dir> [--id idleot] [--cdn <dist/cdn>]" >&2
  exit 1
}

pack="$TIP/mods/$PACK_ID"
if [[ ! -f "$pack/mod.json" ]]; then
  echo "::error::tip pack missing: $pack/mod.json" >&2
  echo "  Merge with merge-pack-into-tip.sh, then re-run make cdn / tip assemble." >&2
  exit 1
fi

if [[ "$REQUIRE_NATIVE" == "1" ]]; then
  native=""
  for cand in "$pack/native/mod.wasm" "$pack/native/mod.so" \
              "$pack/native/mod.dylib" "$pack/native/mod.dll"; do
    if [[ -f "$cand" ]]; then
      native="$cand"
      break
    fi
  done
  if [[ -z "$native" ]]; then
    echo "::error::tip pack has no native module under $pack/native/" >&2
    exit 1
  fi
  echo "[tip-presence] ok native=$native"
else
  echo "[tip-presence] ok urls-only mod.json=$pack/mod.json"
fi

if [[ -n "$CDN" ]]; then
  store="$CDN/content"
  [[ -d "$store" ]] || {
    echo "::error::CDN content store missing: $store" >&2
    exit 1
  }
  # Check package.json next to tip (CIP web package) when present.
  pkg=""
  for cand in "$TIP/package.json" \
              "$CDN/launcher/tibiaclient-web-current/package.json"; do
    if [[ -f "$cand" ]]; then
      pkg="$cand"
      break
    fi
  done
  if [[ -n "$pkg" ]]; then
    python3 - "$pkg" "$store" <<'PY'
import json, sys
from pathlib import Path

pkg = Path(sys.argv[1])
store = Path(sys.argv[2])
doc = json.loads(pkg.read_text())
missing = []
for ent in doc.get("files") or []:
    h = (ent.get("packedhash") or "").strip()
    if not h or len(h) < 4:
        continue
    blob = store / h[:2] / h
    if not blob.is_file():
        missing.append(f"{ent.get('localfile', '?')} → {blob}")
if missing:
    print(f"::error::{pkg} packedhashes missing from content store:", file=sys.stderr)
    for m in missing[:20]:
        print(f"  {m}", file=sys.stderr)
    if len(missing) > 20:
        print(f"  … +{len(missing) - 20} more", file=sys.stderr)
    print(
        "  Fix: write packed lzma into content/<aa>/<hash> (dev_cdn._cip_file_record).",
        file=sys.stderr,
    )
    sys.exit(1)
print(f"[tip-presence] ok content-store blobs for {pkg.name} ({len(doc.get('files') or [])} files)")
PY
  else
    echo "[tip-presence] skip content-store (no package.json near tip/CDN)"
  fi
fi

echo "[tip-presence] ok tip=$TIP pack=$PACK_ID"
