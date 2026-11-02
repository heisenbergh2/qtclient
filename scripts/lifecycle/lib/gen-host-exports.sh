#!/usr/bin/env bash
# Regenerate cmake/host_exports.json from a pack SIDE_MODULE.
# Usage:
#   scripts/lifecycle/lib/gen-host-exports.sh path/to/mod.wasm
#   IDLEOT_MOD_WASM=… scripts/lifecycle/lib/gen-host-exports.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SOFT="$(cd "$HERE/../../.." && pwd)"
OUT="${HOST_EXPORTS_OUT:-$SOFT/cmake/host_exports.json}"
MOD="${1:-${IDLEOT_MOD_WASM:-}}"
if [[ -z "$MOD" || ! -f "$MOD" ]]; then
  echo "usage: $0 path/to/mod.wasm" >&2
  echo "  (or set IDLEOT_MOD_WASM)" >&2
  exit 1
fi
python3 - "$HERE" "$MOD" "$OUT" <<'PY'
import json, sys
from pathlib import Path

lib = Path(sys.argv[1])
sys.path.insert(0, str(lib))
from wasm_abi import host_needed_from_pack  # noqa: E402

mod_path = Path(sys.argv[2])
out_path = Path(sys.argv[3])
merged = host_needed_from_pack(mod_path)
out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(json.dumps(merged, indent=2) + "\n")
print(f"[host-exports] {len(merged)} symbols → {out_path} (from {mod_path})")
PY
