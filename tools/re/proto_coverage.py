#!/usr/bin/env python3
"""Build the Tibia protocol coverage tables.

Inputs:
  --msgmap   JSONL from ghidra/TibiaMessageMap.java (class -> vtable address points)
  --binary   the Tibia x86_64 Mach-O client the msgmap was produced from
  --root     monorepo root (reads qtclient protocolcodes/parse/send, Crystal protocolgame + modules.xml)

Every payload message is a protobuf-lite extension of GameserverMessage /
GameclientMessage. Its static ExtensionIdentifier is {u64 number, T* default_instance}
in __data, and *default_instance is T's vtable address point; the extension
number is the wire opcode.

Writes JSON (--json) and Markdown tables (--markdown).
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from collections import defaultdict
from pathlib import Path


def macho_segments(buf):
    ncmds = struct.unpack_from("<I", buf, 16)[0]
    pos, segs = 32, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", buf, pos)
        if cmd == 0x19:
            name = buf[pos + 8 : pos + 24].rstrip(b"\0").decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", buf, pos + 24)
            nsects = struct.unpack_from("<I", buf, pos + 64)[0]
            sects = []
            for i in range(nsects):
                s = pos + 72 + i * 80
                sname = buf[s : s + 16].rstrip(b"\0").decode()
                addr, ssize = struct.unpack_from("<QQ", buf, s + 32)
                sects.append((sname, addr, ssize))
            segs.append((name, vmaddr, vmsize, fileoff, filesize, sects))
        pos += size
    return segs


def extension_numbers(binary, msgmap):
    buf = Path(binary).read_bytes()
    segs = macho_segments(buf)

    def off(va):
        for _, vm, vs, fo, fs, _ in segs:
            if vm <= va < vm + min(vs, fs):
                return fo + va - vm
        return None

    vtables = {}
    for line in Path(msgmap).read_text().splitlines():
        x = json.loads(line)
        for v in x["vtables"]:
            vtables[int(v, 16)] = x["class"]
    data = next(s for seg in segs if seg[0] == "__DATA" for s in seg[5] if s[0] == "__data")
    lo, hi = data[1], data[1] + data[2]
    found = defaultdict(set)
    for va in range(lo, hi - 16, 8):
        n, p = struct.unpack_from("<QQ", buf, off(va))
        if n >= 0x10000:
            continue
        po = off(p)
        if po is None:
            continue
        cls = vtables.get(struct.unpack_from("<Q", buf, po)[0])
        if cls:
            found[cls].add(n)
    return {c: sorted(ns) for c, ns in found.items()}


def otc_sets(root):
    src = (root / "apps/qtclient/client/tibia/client/engine/client/protocolcodes.h").read_text()

    def enum(name):
        body = re.search(r"enum\s+" + name + r"[^{]*\{(.*?)\};", src, re.S).group(1)
        out = defaultdict(list)
        for k, v in re.findall(r"(\w+)\s*=\s*(\d+|0x[0-9a-fA-F]+)", body):
            out[int(v, 0)].append(k)
        return out

    server, client = enum("GameServerOpcodes"), enum("ClientOpcodes")
    s_names = {n: op for op, ns in server.items() for n in ns}
    c_names = {n: op for op, ns in client.items() for n in ns}
    parse = (root / "apps/qtclient/client/tibia/client/protocol/servermessages.cpp").read_text()
    send = (root / "apps/qtclient/client/tibia/client/protocol/clientmessages.cpp").read_text()
    parsed = {s_names[n] for n in re.findall(r"case Proto::(\w+)\s*:", parse) if n in s_names}
    sent = {c_names[n] for n in re.findall(r"Proto::(Client\w+)", send) if n in c_names}
    return server, client, parsed, sent


def crystal_sets(root):
    cs = (root / "apps/crystalserver/src/server/network/protocol/protocolgame.cpp").read_text()
    sent = {int(x, 16) for x in re.findall(r"addByte\(0x([0-9A-Fa-f]{2})\)", cs)}
    crystal = root / "apps/crystalserver"
    for cpp in (crystal / "src").rglob("*.cpp"):
        sent |= {int(x, 16) for x in re.findall(r"addByte\(0x([0-9A-Fa-f]{2})\)", cpp.read_text(errors="replace"))}
    for lua in [*(crystal / "data").rglob("*.lua"), *(crystal / "data-global").rglob("*.lua")]:
        text = lua.read_text(errors="replace")
        sent |= {int(x, 16) for x in re.findall(r"addByte\(0x([0-9A-Fa-f]{2})\)", text)}
        for table in re.findall(r"(?:ServerPackets|SendingPackets)\s*=\s*\{(.*?)\}", text, re.S):
            sent |= {int(x, 16) for x in re.findall(r"=\s*0x([0-9A-Fa-f]{2})", table)}
    pp = re.search(r"void ProtocolGame::parsePacketFromDispatcher.*?\n}\n", cs, re.S)
    recv = {int(x, 16) for x in re.findall(r"case\s+0x([0-9A-Fa-f]{2})\s*:", pp.group(0))}
    modules = (root / "apps/crystalserver/data/modules/modules.xml").read_text()
    recv |= {int(x, 0) for x in re.findall(r'byte="(0x[0-9A-Fa-f]+|\d+)"', modules)}
    if "onRecvFirstMessage" in cs:
        recv.add(0x0A)
    return sent, recv


def build(args):
    root = Path(args.root)
    ext = extension_numbers(args.binary, args.msgmap)
    server, client, otc_parsed, otc_sent = otc_sets(root)
    crys_sent, crys_recv = crystal_sets(root)
    rows = []
    for d, prefix, otc_enum, otc_impl, crys in (
        ("server", "GameserverMessage", server, otc_parsed, crys_sent),
        ("client", "GameclientMessage", client, otc_sent, crys_recv),
    ):
        by = defaultdict(list)
        for cls, ns in ext.items():
            if cls.startswith(prefix):
                for n in ns:
                    by[n].append(cls[len(prefix) :])
        for op in sorted(set(by) | set(otc_enum)):
            rows.append(
                {
                    "dir": d,
                    "opcode": op,
                    "tibia": by.get(op, []),
                    "otc_enum": otc_enum.get(op, []),
                    "otc_impl": op in otc_impl,
                    "crystal": op in crys,
                }
            )
    return rows


def markdown(rows):
    out = []
    for d, title in (("server", "Server to client"), ("client", "Client to server")):
        crys = "Crystal sends" if d == "server" else "Crystal parses"
        otc = "OTC parses" if d == "server" else "OTC sends"
        out.append(f"### {title}\n")
        out.append(f"| Opcode | Tibia message | qtclient enum | {otc} | {crys} |")
        out.append("|---|---|---|---|---|")
        for r in rows:
            if r["dir"] != d:
                continue
            tick = lambda b: "yes" if b else "no"
            out.append(
                f"| 0x{r['opcode']:02X} | {', '.join(r['tibia']) or '—'} | "
                f"{', '.join(r['otc_enum']) or '—'} | {tick(r['otc_impl'])} | {tick(r['crystal'])} |"
            )
        out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--msgmap", required=True)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--root", default=".")
    ap.add_argument("--json")
    ap.add_argument("--markdown")
    args = ap.parse_args()
    rows = build(args)
    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1))
    if args.markdown:
        Path(args.markdown).write_text(markdown(rows))
    for d in ("server", "client"):
        sub = [r for r in rows if r["dir"] == d]
        tib = [r for r in sub if r["tibia"]]
        print(
            f"{d}: tibia={len(tib)} otc_enum_missing={sum(1 for r in tib if not r['otc_enum'])} "
            f"otc_impl_missing={sum(1 for r in tib if not r['otc_impl'])} "
            f"otc_only={sum(1 for r in sub if not r['tibia'])} crystal_missing={sum(1 for r in tib if not r['crystal'])}"
        )


if __name__ == "__main__":
    main()
