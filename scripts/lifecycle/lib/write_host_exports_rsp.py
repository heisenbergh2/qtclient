#!/usr/bin/env python3
"""Write emcc @rsp of -Wl,--undefined=SYM,--export-if-defined=SYM from host_exports.json."""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

SKIP = {
    "_main",
    "__embind_initialize_bindings",
    "_paste_return",
    "_otc_request_shell_layout",
    "_otc_sync_shell_layout",
    "_otc_signal_dom_input",
}

# Soft-owned stubs (side_module_abi_stubs.cpp) — force export so DCE cannot
# drop them; --export-if-defined was a no-op when libc never linked the symbol.
FORCE_EXPORT = {
    "memcmp",
    "__cxa_throw",
}


def main() -> int:
    src = Path(os.environ["HOST_EXPORTS_JSON"])
    dst = Path(os.environ["HOST_EXPORTS_RSP"])
    syms = json.loads(src.read_text())
    if not isinstance(syms, list) or not syms:
        print(f"host_exports.json empty or not a list: {src}", file=sys.stderr)
        return 1
    lines: list[str] = []
    for sym in syms:
        if not sym or sym in SKIP:
            continue
        # One linker flag group per line; emcc @file expands these as argv.
        if sym in FORCE_EXPORT:
            lines.append(f"-Wl,--export={sym}")
        else:
            lines.append(f"-Wl,--undefined={sym},--export-if-defined={sym}")
    if not lines:
        print(f"no keepalive symbols after skip: {src}", file=sys.stderr)
        return 1
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(lines) + "\n")
    print(f"[host-exports-rsp] {len(lines)} flags → {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
