#!/usr/bin/env bash
# Fail if tibia.wasm is missing any cmake/host_exports.json keepalive.
# Catches Qt WasmMacros wipe and incomplete MAIN_MODULE=2 keepalives.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SOFT="$(cd "$HERE/../../.." && pwd)"
WASM="${1:-}"
JSON="${2:-$SOFT/cmake/host_exports.json}"
if [[ -z "$WASM" || ! -f "$WASM" ]]; then
  echo "usage: $0 path/to/tibia.wasm [host_exports.json]" >&2
  exit 2
fi
if [[ ! -f "$JSON" ]]; then
  echo "missing host_exports.json: $JSON" >&2
  exit 2
fi
python3 - "$HERE" "$WASM" "$JSON" <<'PY'
import json, sys
from pathlib import Path

lib = Path(sys.argv[1])
sys.path.insert(0, str(lib))
from wasm_abi import export_names  # noqa: E402

wasm = Path(sys.argv[2])
needed = json.loads(Path(sys.argv[3]).read_text())
# JS/shell stubs — not object symbols; Qt EXPORTED_FUNCTIONS covers these.
skip = {
    "_main",
    "__embind_initialize_bindings",
    "_paste_return",
    "_otc_request_shell_layout",
    "_otc_sync_shell_layout",
    "_otc_signal_dom_input",
}
required = [s for s in needed if s and s not in skip]
names = export_names(wasm)
missing = [s for s in required if s not in names]
print(f"[host-exports-check] {wasm}: {len(names)} exports; require {len(required)}")
if missing:
    print(
        f"[host-exports-check] MISSING {len(missing)} keepalives "
        "(incomplete host_exports apply or DCE):",
        file=sys.stderr,
    )
    for m in missing[:40]:
        print(f"  {m}", file=sys.stderr)
    if len(missing) > 40:
        print(f"  … +{len(missing) - 40} more", file=sys.stderr)
    print(
        "  Soft must apply host_exports.json as SHELL:@host_exports.rsp "
        "(-Wl,--undefined=SYM,--export-if-defined=SYM) after Qt finalize.",
        file=sys.stderr,
    )
    raise SystemExit(1)
print(f"[host-exports-check] ok: all {len(required)} host_exports present")
PY
