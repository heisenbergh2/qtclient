#!/usr/bin/env python3
"""Shared WASM import/export helpers for Soft↔pack ABI scripts."""
from __future__ import annotations

from pathlib import Path


def read_leb(buf: bytes, i: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, i
        shift += 7


def read_name(buf: bytes, i: int) -> tuple[str, int]:
    n, i = read_leb(buf, i)
    return buf[i : i + n].decode("utf-8"), i + n


def load_denylist(path: Path | None = None) -> list[str]:
    here = Path(__file__).resolve().parent
    p = path or (here / "pack_local_denylist.txt")
    out: list[str] = []
    for line in p.read_text().splitlines():
        s = line.split("#", 1)[0].strip()
        if s:
            out.append(s)
    return out


def is_pack_local(name: str, denylist: list[str] | None = None) -> bool:
    needles = denylist if denylist is not None else load_denylist()
    return any(n in name for n in needles)


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
            count, i = read_leb(data, off)
            names: set[str] = set()
            for _ in range(count):
                name, i = read_name(data, i)
                i += 1  # kind
                _, i = read_leb(data, i)
                names.add(name)
            return names
        off = end
    return set()


def side_module_imports(path: Path) -> list[tuple[str, str, int]]:
    """Return (module, name, kind) for import section entries."""
    data = path.read_bytes()
    if data[:4] != b"\0asm":
        raise SystemExit(f"not a wasm module: {path}")
    off = 8
    out: list[tuple[str, str, int]] = []
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
                if kind == 0:
                    _, i = read_leb(data, i)
                elif kind == 1:
                    i += 1
                    flags, i = read_leb(data, i)
                    _, i = read_leb(data, i)
                    if flags & 1:
                        _, i = read_leb(data, i)
                elif kind == 2:
                    flags, i = read_leb(data, i)
                    _, i = read_leb(data, i)
                    if flags & 1:
                        _, i = read_leb(data, i)
                elif kind == 3:
                    i += 2
                else:
                    raise SystemExit(f"unknown import kind {kind}")
                out.append((mod, name, kind))
            break
        off = end
    return out


def host_needed_from_pack(mod_wasm: Path, denylist: list[str] | None = None) -> list[str]:
    """Soft MAIN_MODULE keepalives implied by a pack SIDE_MODULE.

    Skip pack-local denylist names and any import the SIDE_MODULE also exports
    (Emscripten relocatable link lists self-defined symbols as env/GOT imports).
    Soft must only keepalive symbols the pack does not define.
    """
    needles = denylist if denylist is not None else load_denylist()
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
    shell = [
        "_main",
        "__embind_initialize_bindings",
        "_paste_return",
        "_otc_request_shell_layout",
        "_otc_sync_shell_layout",
        "_otc_signal_dom_input",
    ]
    defined = export_names(mod_wasm)
    imports: list[str] = []
    for mod, name, kind in side_module_imports(mod_wasm):
        if is_pack_local(name, needles):
            continue
        if name in skip or name in defined:
            continue
        if mod == "env" and kind in (0, 3):
            imports.append(name)
        elif mod == "GOT.mem" and kind == 3 and (
            name.startswith("g_")
            or name.startswith("_ZTV")
            or name.startswith("_ZTI")
            or name.startswith("_ZTS")
            or name.endswith("staticMetaObjectE")
            or name == "_ZN7QString6_emptyE"
        ):
            if "St3__210__function" in name:
                continue
            imports.append(name)
    return sorted(set(shell + imports))
