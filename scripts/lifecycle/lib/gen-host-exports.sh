#!/usr/bin/env bash
# Regenerate cmake/host_exports.json from an IdleOT (or other) SIDE_MODULE.
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
python3 - "$MOD" "$OUT" <<'PY'
import json, sys
from pathlib import Path

def read_leb(buf, i):
    result = 0
    shift = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7

def read_name(buf, i):
    n, i = read_leb(buf, i)
    return buf[i : i + n].decode("utf-8"), i + n

def wasm_func_imports(path: Path) -> list[str]:
    data = path.read_bytes()
    if data[:4] != b"\0asm":
        raise SystemExit(f"not a wasm module: {path}")
    off = 8
    imports: list[str] = []
    while off < len(data):
        sid = data[off]
        off += 1
        size, off = read_leb(data, off)
        end = off + size
        if sid == 2:
            count, i = read_leb(data, off)
            for _ in range(count):
                _mod, i = read_name(data, i)
                name, i = read_name(data, i)
                kind = data[i]
                i += 1
                if kind == 0:
                    _, i = read_leb(data, i)
                    imports.append(name)
                elif kind == 1:
                    i += 1
                    flags = data[i]
                    i += 1
                    _, i = read_leb(data, i)
                    if flags & 1:
                        _, i = read_leb(data, i)
                elif kind == 2:
                    flags = data[i]
                    i += 1
                    _, i = read_leb(data, i)
                    if flags & 1:
                        _, i = read_leb(data, i)
                elif kind == 3:
                    i += 2
                else:
                    raise SystemExit(f"unknown import kind {kind}")
            break
        off = end
    return imports

shell = [
    "_main",
    "_paste_return",
    "_otc_request_shell_layout",
    "_otc_sync_shell_layout",
    "_otc_signal_dom_input",
]
mod_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])
merged = sorted(set(shell + wasm_func_imports(mod_path)))
out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(json.dumps(merged, indent=2) + "\n")
print(f"[host-exports] {len(merged)} symbols → {out_path} (from {mod_path})")
PY
