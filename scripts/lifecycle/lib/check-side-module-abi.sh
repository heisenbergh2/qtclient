#!/usr/bin/env bash
# Fail if a pack SIDE_MODULE still imports pack-local types from Soft env,
# or is missing qtclient_mod_entry (ModLoader dlsym).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
WASM="${1:?mod.wasm}"
python3 - "$HERE" "$WASM" <<'PY'
import sys
from pathlib import Path

lib = Path(sys.argv[1])
sys.path.insert(0, str(lib))
from wasm_abi import export_names, is_pack_local, load_denylist, side_module_imports  # noqa: E402

path = Path(sys.argv[2])
denylist = load_denylist()
exports = export_names(path)
# SIDE_MODULE relocatable links often list pack-local symbols as env/GOT
# imports that resolve to the module's own exports at dlopen. Fail only when
# Soft would have to provide them (imported and not exported here).
unresolved: list[str] = []
for mod, name, kind in side_module_imports(path):
    if mod in ("env", "GOT.mem") and is_pack_local(name, denylist) and name not in exports:
        unresolved.append(f"{mod}:{name}")
errors: list[str] = []
if unresolved:
    errors.append(
        "pack-local imports Soft cannot provide (not exported by SIDE_MODULE):\n  "
        + "\n  ".join(unresolved[:30])
        + (f"\n  … +{len(unresolved) - 30} more" if len(unresolved) > 30 else "")
    )
if "qtclient_mod_entry" not in exports:
    errors.append("missing export qtclient_mod_entry (ModLoader dlsym)")
if errors:
    print(f"::error::{path}: " + "; ".join(errors), file=sys.stderr)
    sys.exit(1)
print(f"[check-side-module-abi] ok {path.name} exports={len(exports)}")
PY
