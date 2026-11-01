#!/usr/bin/env python3
"""Generate opcode dispatch from upstream/protocol/messages.json.

Every non-Unknown message needs a handler in handler_map.json. A missing or
stale entry fails. tibia-sync and the cmake check both run this script.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ENUM_RE = re.compile(r"^\s+(GameServer\w+|Client\w+)\s*=\s*(\d+)", re.M)
FUNC_RE = re.compile(
    r"\n(?:void|bool|int|uint\d+_t|std::\w+)\s+ProtocolGame::(\w+)\s*\(",
    re.M,
)

# CIP names whose handler is not the legacy qtclient switch target.
SERVER_CALL_OVERRIDE = {
    "ReadyForSecondaryConnection": "parseReadyForSecondaryConnection(msg);",
    "OutfitMemorial": "parseOutfitMemorial(msg);",
}

CLIENT_FUNC_OVERRIDE = {
    "GuildMessage": "sendGetGuildMessage",
    "EditGuildMessage": "sendEditGuildMessage",
    "GetTextForReport": "sendGetTextForReport",
    "GetTransactionDetails": "sendGetTransactionDetails",
    "GetOfferDescription": "sendGetOfferDescription",
    "SetOutfit": "sendChangeOutfit",
    "OpenMonsterCyclopedia": "sendRequestBestiary",
    "RequestResourceBalance": "sendRequestResourceBalance",
    "WalkNorth": "sendWalkNorth",
    "WalkSouth": "sendWalkSouth",
    "UseObject": "sendUseItem",
    "Look": "sendLook",
    "BugReport": "sendBugReport",
    "RequestStoreOffers": "sendRequestStoreOffers",
    "CyclopediaMapAction": "sendCyclopediaMapDonate",
    "WeaponProficiencyCommand": "sendWeaponProficiencyAction",
    "EquipObject": "sendEquipItemWithTier",
    "UpdateExivaOptions": "sendExivaRestrictions",
    "InspectObject": "sendInspectionObject",
    "GemAtelier": "sendWheelGemAction",
    "StashAction": "sendStashWithdraw",
}


def load_messages(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise SystemExit(f"{path}: expected a JSON array")
    return data


def is_unknown(msg: dict) -> bool:
    names = msg.get("tibia") or []
    return names == ["Unknown"] or names == ["unknown"]


def cip_name(msg: dict) -> str:
    names = msg.get("tibia") or []
    return names[0] if names else "?"


def load_enums(path: Path) -> tuple[dict[int, list[str]], dict[int, list[str]]]:
    text = path.read_text(encoding="utf-8")
    server: dict[int, list[str]] = {}
    client: dict[int, list[str]] = {}
    for name, value in ENUM_RE.findall(text):
        bucket = server if name.startswith("GameServer") else client
        bucket.setdefault(int(value), []).append(name)
    return server, client


def split_params(param_src: str) -> list[str]:
    params: list[str] = []
    depth = 0
    start = 0
    for i, ch in enumerate(param_src):
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        elif ch == "," and depth == 0:
            params.append(param_src[start:i].strip())
            start = i + 1
    tail = param_src[start:].strip()
    if tail:
        params.append(tail)
    return params


def dummy_expr(typ: str) -> str:
    typ = re.sub(r"\s+", " ", typ).strip()
    typ = typ.removeprefix("const ").replace("&", "").strip()
    if typ in {"std::string_view", "std::string"}:
        return f"{typ}{{}}"
    if typ.startswith(("std::vector", "std::map", "std::tuple", "std::array")):
        return "{}"
    if typ in {"uint8_t", "uint16_t", "uint32_t", "uint64_t", "int", "int8_t", "int16_t", "int32_t", "size_t"}:
        return "0"
    if typ == "bool":
        return "false"
    if typ.startswith("Otc::"):
        return f"static_cast<{typ}>(0)"
    return f"{typ}{{}}"


def function_signatures(text: str) -> dict[str, str]:
    """Map ProtocolGame method name to its parameter list (first definition)."""
    out: dict[str, str] = {}
    for match in re.finditer(
        r"\nvoid ProtocolGame::(\w+)\s*\((.*?)\)\s*(?:const\s*)?\{",
        text,
        re.S,
    ):
        name, params = match.group(1), match.group(2).strip()
        out.setdefault(name, params)
    return out


def dummy_call(name: str, params: str) -> str:
    if not params:
        return f"self->{name}()"
    args = []
    for param in split_params(params):
        # drop the parameter name (last token) when the type is not a template-only token
        tokens = param.split()
        if len(tokens) >= 2 and not tokens[-1].startswith("*") and ">" not in tokens[-1]:
            typ = " ".join(tokens[:-1])
        else:
            typ = param
        args.append(dummy_expr(typ))
    return f"self->{name}({', '.join(args)})"


def case_bodies(parse_text: str) -> dict[str, str]:
    parts = re.split(r"\n\s*case Proto::(GameServer\w+)\s*:", parse_text)
    pairs = list(zip(parts[1::2], parts[2::2]))
    bodies: dict[str, str] = {}
    cleaned: list[tuple[str, str]] = []
    for name, body in pairs:
        body = body.split("default:")[0]
        body = re.split(r"\n\s*break;", body, maxsplit=1)[0].strip()
        cleaned.append((name, body))
    for i, (name, body) in enumerate(cleaned):
        if body:
            bodies[name] = body
            continue
        # Fallthrough (`case A: case B:`).
        for _next, nxt in cleaned[i + 1:]:
            if nxt:
                bodies[name] = nxt
                break
    return bodies


def statement_from_body(body: str) -> str:
    fns = re.findall(r"\b(parse\w+)\s*\(", body)
    unique: list[str] = []
    for fn in fns:
        if fn not in unique:
            unique.append(fn)
    if "getClientVersion" in body or "getFeature" in body or "callGlobalField" in body or len(unique) != 1:
        # Keep the existing case body, including version gates that are not
        # the CIP semantic conflicts (those are overridden by name).
        indented = "\n".join(("    " + line) if line else "" for line in body.splitlines())
        return indented.strip() + "\n"
    return f"{unique[0]}(msg);"


def bootstrap(messages: list[dict], codes: Path, parse_path: Path, send_path: Path) -> list[dict]:
    server_enums, client_enums = load_enums(codes)
    bodies = case_bodies(parse_path.read_text(encoding="utf-8"))
    send_text = send_path.read_text(encoding="utf-8")
    send_funcs = function_signatures(send_text)
    entries: list[dict] = []
    for msg in messages:
        if is_unknown(msg):
            continue
        direction = msg["dir"]
        opcode = int(msg["opcode"])
        name = cip_name(msg)
        if direction == "server":
            if name in SERVER_CALL_OVERRIDE:
                call = SERVER_CALL_OVERRIDE[name]
            else:
                enums = server_enums.get(opcode, [])
                body = ""
                for enum in enums:
                    if enum in bodies:
                        body = bodies[enum]
                        break
                if not body:
                    raise SystemExit(f"no parse case for S 0x{opcode:02X} {name} enums={enums}")
                call = statement_from_body(body)
            entries.append({"dir": "server", "opcode": opcode, "name": name, "call": call})
        elif direction == "client":
            if name in CLIENT_FUNC_OVERRIDE:
                func = CLIENT_FUNC_OVERRIDE[name]
            else:
                enums = client_enums.get(opcode, [])
                spans = list(re.finditer(r"\nvoid ProtocolGame::(\w+)\s*\(", send_text))
                found: list[str] = []
                for enum in enums:
                    pat = re.compile(rf"\bProto::{enum}\b")
                    for i, match in enumerate(spans):
                        end = spans[i + 1].start() if i + 1 < len(spans) else len(send_text)
                        window = send_text[match.end():end]
                        if pat.search(window):
                            fn = match.group(1)
                            if fn not in found:
                                found.append(fn)
                literal = re.compile(rf"addU8\(\s*(?:0x{opcode:02X}|{opcode})\s*\)", re.I)
                if not found:
                    for i, match in enumerate(spans):
                        end = spans[i + 1].start() if i + 1 < len(spans) else len(send_text)
                        window = send_text[match.end():end]
                        if literal.search(window):
                            fn = match.group(1)
                            if fn not in found:
                                found.append(fn)
                if not found:
                    raise SystemExit(f"no client writer for C 0x{opcode:02X} {name}")
                if len(found) != 1:
                    print(f"client C 0x{opcode:02X} {name}: using {found[0]} (also {found[1:]})", file=sys.stderr)
                func = found[0]
            entries.append({"dir": "client", "opcode": opcode, "name": name, "func": func})
        else:
            raise SystemExit(f"unknown dir {direction}")
    entries.sort(key=lambda e: (e["dir"], e["opcode"]))
    return entries


def index_map(entries: list[dict]) -> dict[tuple[str, int], dict]:
    out: dict[tuple[str, int], dict] = {}
    for entry in entries:
        key = (entry["dir"], int(entry["opcode"]))
        if key in out:
            raise SystemExit(f"duplicate handler map entry {key}")
        out[key] = entry
    return out


def defined_functions(path: Path) -> set[str]:
    return set(FUNC_RE.findall(path.read_text(encoding="utf-8")))


def validate(messages: list[dict], entries: list[dict], server_cpp: Path, client_cpp: Path) -> None:
    mapped = index_map(entries)
    server_fns = defined_functions(server_cpp)
    client_fns = defined_functions(client_cpp)
    client_sigs = function_signatures(client_cpp.read_text(encoding="utf-8"))
    missing: list[str] = []
    stale: list[str] = []
    seen: set[tuple[str, int]] = set()
    for msg in messages:
        if is_unknown(msg):
            continue
        key = (msg["dir"], int(msg["opcode"]))
        seen.add(key)
        entry = mapped.get(key)
        name = cip_name(msg)
        if entry is None:
            missing.append(f"  {key[0][0].upper()} 0x{key[1]:02X} {name}")
            continue
        if entry.get("name") != name:
            missing.append(f"  {key[0][0].upper()} 0x{key[1]:02X} {name} (map still says {entry.get('name')})")
            continue
        if key[0] == "server":
            for fn in re.findall(r"\b(parse\w+)\s*\(", entry.get("call", "")):
                if fn not in server_fns:
                    missing.append(f"  S 0x{key[1]:02X} {name} missing {fn}")
        else:
            func = entry.get("func", "")
            if func not in client_fns or func not in client_sigs:
                missing.append(f"  C 0x{key[1]:02X} {name} missing {func}")
    for key, entry in mapped.items():
        if key not in seen:
            stale.append(f"  {key[0][0].upper()} 0x{key[1]:02X} {entry.get('name')}")
    if missing or stale:
        if missing:
            print("protocol dispatch: messages.json opcodes with no handler:", file=sys.stderr)
            print("\n".join(missing), file=sys.stderr)
        if stale:
            print("protocol dispatch: handler map entries no longer in messages.json:", file=sys.stderr)
            print("\n".join(stale), file=sys.stderr)
        raise SystemExit(1)


def render(messages: list[dict], entries: list[dict], client_cpp: Path) -> str:
    mapped = index_map(entries)
    sigs = function_signatures(client_cpp.read_text(encoding="utf-8"))
    server_cases: list[str] = []
    client_cases: list[str] = []
    for msg in messages:
        if is_unknown(msg):
            continue
        key = (msg["dir"], int(msg["opcode"]))
        entry = mapped[key]
        name = cip_name(msg)
        opcode = int(msg["opcode"])
        if key[0] == "server":
            call = entry["call"].rstrip()
            if "\n" in call:
                body = "\n".join("        " + line if line else "" for line in call.splitlines())
            else:
                body = "        " + call
            server_cases.append(f"    case {opcode}: // {name}\n{body}\n        return true;")
        else:
            func = entry["func"]
            call = dummy_call(func, sigs[func])
            client_cases.append(f"    case {opcode}: // {name}\n        {call};\n        return true;")

    server_body = "\n".join(server_cases)
    client_body = "\n".join(client_cases)
    return f"""// Generated by client/tibia/protocol/gen_dispatch.py from upstream/protocol/messages.json.
// Do not edit. A CIP opcode with no handler_map.json entry fails the build.

#include "client/protocolgame.h"
#include "client/protocolcodes.h"
#include "tibia/world/game.h"
#include "framework/core/gamehooks.h"

bool ProtocolGame::dispatchServerMessage(const uint8_t opcode, const InputMessagePtr& msg)
{{
    switch (opcode) {{
{server_body}
    default:
        return false;
    }}
}}

bool ProtocolGame::dispatchClientMessage(const uint8_t opcode)
{{
    // Compile-time binding of every CIP client opcode to its writer.
    // Called only so a missing or mistyped writer fails the build; runtime
    // sends go through the named ProtocolGame methods.
    ProtocolGame* self = this;
    switch (opcode) {{
{client_body}
    default:
        return false;
    }}
}}
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--messages", type=Path, required=True)
    ap.add_argument("--codes", type=Path, required=True)
    ap.add_argument("--server", type=Path, required=True)
    ap.add_argument("--client", type=Path, required=True)
    ap.add_argument("--map", type=Path, required=True)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--bootstrap", action="store_true", help="write handler_map.json from the current parse switch")
    ap.add_argument("--check", action="store_true", help="fail if --out would change")
    args = ap.parse_args()

    messages = load_messages(args.messages)
    if args.bootstrap:
        entries = bootstrap(messages, args.codes, args.server, args.client)
        args.map.write_text(json.dumps(entries, indent=1) + "\n", encoding="utf-8")
        print(f"wrote {args.map} ({len(entries)} handlers)")
        return 0

    entries = json.loads(args.map.read_text(encoding="utf-8"))
    validate(messages, entries, args.server, args.client)
    if args.out is None and not args.bootstrap:
        print("protocol dispatch: handlers ok")
        return 0
    if args.out is None:
        return 0
    text = render(messages, entries, args.client)
    if args.check:
        current = args.out.read_text(encoding="utf-8") if args.out.is_file() else ""
        if current != text:
            print(f"protocol dispatch: {args.out} is stale; run gen_dispatch.py", file=sys.stderr)
            return 1
        print("protocol dispatch: ok")
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
