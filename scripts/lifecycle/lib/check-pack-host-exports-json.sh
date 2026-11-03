#!/usr/bin/env bash
# Fail if pack SIDE_MODULE imports are absent from Soft cmake/host_exports.json.
# Early Soft bump signal (no tibia.wasm required) — release still runs binary coverage.
# Usage: check-pack-host-exports-json.sh path/to/mod.wasm path/to/host_exports.json
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PACK="${1:?mod.wasm}"
JSON="${2:?host_exports.json}"
python3 - "$HERE" "$PACK" "$JSON" <<'PY'
import json, sys
from pathlib import Path

lib = Path(sys.argv[1])
sys.path.insert(0, str(lib))
from wasm_abi import host_needed_from_pack  # noqa: E402

pack = Path(sys.argv[2])
js = Path(sys.argv[3])
skip = {
    "_main",
    "__embind_initialize_bindings",
    "_paste_return",
    "_otc_request_shell_layout",
    "_otc_sync_shell_layout",
    "_otc_signal_dom_input",
}
needed = [s for s in host_needed_from_pack(pack) if s not in skip]
have = set(json.loads(js.read_text()))
missing = [s for s in needed if s not in have]
print(
    f"[pack-host-exports-json] json={len(have)} pack_needs={len(needed)} "
    f"missing={len(missing)}"
)
if missing:
    print(
        f"::error::{pack.name} needs {len(missing)} Soft keepalives absent from {js.name}:",
        file=sys.stderr,
    )
    for m in missing[:40]:
        print(f"  {m}", file=sys.stderr)
    if len(missing) > 40:
        print(f"  … +{len(missing) - 40} more", file=sys.stderr)
    print(
        "  Soft bump: make host-exports IDLEOT_MOD_WASM=… && rebuild Soft wasm / client-sdk.",
        file=sys.stderr,
    )
    sys.exit(1)
print(f"[pack-host-exports-json] ok {pack.name} ⊆ {js.name}")
PY
