#!/usr/bin/env bash
# Fail if tibia.wasm is missing IdleOT SIDE_MODULE keepalive exports.
# Catches Qt WasmMacros overwriting -sEXPORTED_FUNCTIONS (Soft #9 ship miss).
set -euo pipefail
WASM="${1:-}"
if [[ -z "$WASM" || ! -f "$WASM" ]]; then
  echo "usage: $0 path/to/tibia.wasm" >&2
  exit 2
fi
python3 - "$WASM" <<'PY'
import sys
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

def export_names(path: Path) -> set[str]:
    data = path.read_bytes()
    if data[:4] != b"\0asm":
        raise SystemExit(f"not a wasm module: {path}")
    off = 8
    while off < len(data):
        sid = data[off]
        off += 1
        size, off = read_leb(data, off)
        end = off + size
        if sid == 7:
            n, i = read_leb(data, off)
            names = set()
            for _ in range(n):
                nl, i = read_leb(data, i)
                names.add(data[i : i + nl].decode("utf-8"))
                i += nl
                i += 1  # kind
                _, i = read_leb(data, i)
            return names
        off = end
    return set()

# Canaries from IdleOT pack env imports — must survive MAIN_MODULE=2 DCE.
canaries = [
    "_ZN5tibia3app21ClientOptionsNotifier8instanceEv",
    "_ZN4Tile9hasGroundEv",
    "_ZN5tibia13qmlcomponents18buildWorldGeometryERKNS0_18WorldGeometryInputE",
    "g_things",
    "g_map",
]
names = export_names(Path(sys.argv[1]))
missing = [c for c in canaries if c not in names]
print(f"[host-exports-check] {sys.argv[1]}: {len(names)} exports")
if missing:
    print("[host-exports-check] MISSING canaries (Qt EXPORTED_FUNCTIONS wipe or DCE):", file=sys.stderr)
    for m in missing:
        print(f"  {m}", file=sys.stderr)
    print(
        "  Soft must apply host_exports.json as -Wl,--undefined=SYM,"
        "--export-if-defined=SYM after Qt finalize (not nested @rsp; "
        "not EXPORTED_FUNCTIONS=@host_exports).",
        file=sys.stderr,
    )
    raise SystemExit(1)
print("[host-exports-check] ok:", ", ".join(canaries))
PY
