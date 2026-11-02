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

def wasm_env_imports(path: Path) -> list[str]:
    """Names imported from env (funcs + globals) that MAIN_MODULE must export.

    GOT.mem / GOT.func are relocatable stubs resolved by the dynamic loader
    against the side module and/or host — do not put side-local GOT.func
    names into host EXPORTED_FUNCTIONS (would stub-shadow the pack).
    Host globals the pack needs (g_things, Qt vtables, …) often appear as
    GOT.mem imports; include those that look like Soft/Qt host ABI.
    """
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
                mod, i = read_name(data, i)
                name, i = read_name(data, i)
                kind = data[i]
                i += 1
                if kind == 0:  # func
                    _, i = read_leb(data, i)
                    if mod == "env":
                        imports.append(name)
                elif kind == 1:  # table
                    i += 1
                    flags, i = read_leb(data, i)
                    _, i = read_leb(data, i)
                    if flags & 1:
                        _, i = read_leb(data, i)
                elif kind == 2:  # memory
                    flags, i = read_leb(data, i)
                    _, i = read_leb(data, i)
                    if flags & 1:
                        _, i = read_leb(data, i)
                elif kind == 3:  # global
                    i += 2
                    if mod == "env":
                        imports.append(name)
                    elif mod == "GOT.mem" and (
                        name.startswith("g_")
                        or name.startswith("_ZTV")  # host Qt/tibia vtables
                        or name.startswith("_ZTI")
                        or name.startswith("_ZTS")
                        or name.endswith("staticMetaObjectE")
                        or name == "_ZN7QString6_emptyE"
                    ):
                        # Skip pack-local typeinfo/vtables (IdleOT raycast, …).
                        if "qmlcomponents16TRaycastLighting" in name:
                            continue
                        if "mods19WorldLightingDriver" in name:
                            continue
                        if "St3__210__function" in name:
                            continue
                        imports.append(name)
                else:
                    raise SystemExit(f"unknown import kind {kind}")
            break
        off = end
    return imports

# Shell entrypoints Soft/Qt expect on the wasm Module (not always in mod.wasm).
shell = [
    "_main",
    "__embind_initialize_bindings",
    "_paste_return",
    "_otc_request_shell_layout",
    "_otc_sync_shell_layout",
    "_otc_signal_dom_input",
]
skip = {
    "memory",
    "__indirect_function_table",
    "__memory_base",
    "__table_base",
    "__stack_pointer",
    "__heap_base",
    "__heap_end",
    "table",
}

mod_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])
from_mod = [n for n in wasm_env_imports(mod_path) if n not in skip]
merged = sorted(set(shell + from_mod))
out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(json.dumps(merged, indent=2) + "\n")
print(f"[host-exports] {len(merged)} symbols → {out_path} (from {mod_path})")
PY
