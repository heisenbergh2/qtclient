#!/usr/bin/env bash
# Fail if Soft tibia.wasm does not export every host keepalive implied by pack mod.wasm.
# Usage: check-pack-host-coverage.sh path/to/tibia.wasm path/to/mod.wasm
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="${1:?tibia.wasm}"
PACK="${2:?mod.wasm}"
python3 - "$HERE" "$HOST" "$PACK" <<'PY'
import sys
from pathlib import Path

lib = Path(sys.argv[1])
sys.path.insert(0, str(lib))
from wasm_abi import export_names, host_needed_from_pack  # noqa: E402

host = Path(sys.argv[2])
pack = Path(sys.argv[3])
skip = {
    "_main",
    "__embind_initialize_bindings",
    "_paste_return",
    "_otc_request_shell_layout",
    "_otc_sync_shell_layout",
    "_otc_signal_dom_input",
}
needed = [s for s in host_needed_from_pack(pack) if s not in skip]
have = export_names(host)
missing = [s for s in needed if s not in have]
print(
    f"[pack-host-coverage] host_exports={len(have)} pack_needs={len(needed)} "
    f"missing={len(missing)}"
)
if missing:
    print(
        f"::error::{pack.name} needs {len(missing)} Soft keepalives absent from {host.name}:",
        file=sys.stderr,
    )
    for m in missing[:40]:
        print(f"  {m}", file=sys.stderr)
    if len(missing) > 40:
        print(f"  … +{len(missing) - 40} more", file=sys.stderr)
    print(
        "  Regen Soft cmake/host_exports.json (gen-host-exports.sh) and rebuild Soft wasm.",
        file=sys.stderr,
    )
    sys.exit(1)
print(f"[pack-host-coverage] ok {pack.name} ⊆ {host.name}")
PY
