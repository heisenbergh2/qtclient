"""CIP things-pack readers shared by the AAC sprite renderers (outfits, items).

Stdlib only: minimal protobuf wire decoding of appearances.dat, LZMA1 sprite
sheets (same decode as qtclient spriteappearances.cpp) and PNG/APNG writers.
"""
from __future__ import annotations

import hashlib
import json
import lzma
import os
import re
import shutil
import struct
import zlib
from collections.abc import Callable
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
THINGS_ROOT = ROOT / "apps/qtclient/data/things"
ASSETS_ROOT = ROOT / "apps/qtclient/data/assets"

SHEET_SIZE = 384
# SpriteLayout → (width, height); see qtclient spriteappearances.cpp.
SPRITE_SIZES = [
    (32, 32), (32, 64), (64, 32), (64, 64), (32, 96), (32, 128), (32, 192),
    (32, 384), (64, 96), (64, 128), (64, 192), (64, 384), (96, 32), (96, 64),
    (96, 96), (96, 128), (96, 192), (96, 384), (128, 32), (128, 64), (128, 96),
    (128, 128), (128, 192), (128, 384), (192, 32), (192, 64), (192, 96),
    (192, 128), (192, 192), (192, 384), (384, 32), (384, 64), (384, 96),
    (384, 128), (384, 192), (384, 384),
]
FRAME_GROUP_IDLE = 0
FRAME_GROUP_MOVING = 1
APPEARANCES_OBJECT = 1
APPEARANCES_OUTFIT = 2
FLAG_TAKE = 18
MAGENTA = re.compile(b"\xff\x00\xff")


# --- protobuf (minimal wire decoder for appearances.dat) -------------------


def _varint(buf: bytes, pos: int) -> tuple[int, int]:
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, pos
        shift += 7


def fields(buf: bytes):
    pos, end = 0, len(buf)
    while pos < end:
        key, pos = _varint(buf, pos)
        num, wire = key >> 3, key & 7
        if wire == 0:
            val, pos = _varint(buf, pos)
        elif wire == 2:
            ln, pos = _varint(buf, pos)
            val = buf[pos : pos + ln]
            pos += ln
        elif wire == 5:
            val = buf[pos : pos + 4]
            pos += 4
        elif wire == 1:
            val = buf[pos : pos + 8]
            pos += 8
        else:
            raise ValueError(f"unsupported wire type {wire}")
        yield num, wire, val


@dataclass
class SpriteInfo:
    pattern_x: int = 1
    pattern_y: int = 1
    pattern_z: int = 1
    layers: int = 1
    durations: list[int] = field(default_factory=lambda: [0])
    sprite_ids: list[int] = field(default_factory=list)

    @property
    def phases(self) -> int:
        return len(self.durations)

    def sprite_id(self, phase: int = 0, x: int = 0, y: int = 0, z: int = 0, layer: int = 0) -> int | None:
        idx = (((phase * self.pattern_z + z) * self.pattern_y + y) * self.pattern_x + x) * self.layers + layer
        return self.sprite_ids[idx] if idx < len(self.sprite_ids) else None


def _parse_animation(buf: bytes) -> list[int]:
    durations = []
    for num, _, val in fields(buf):
        if num == 6:
            durations.append(next((v for n, _, v in fields(val) if n == 1), 0))
    return durations or [0]


def _parse_sprite_info(buf: bytes) -> SpriteInfo:
    info = SpriteInfo()
    for num, wire, val in fields(buf):
        if num == 1:
            info.pattern_x = val
        elif num == 2:
            info.pattern_y = val
        elif num == 3:
            info.pattern_z = val
        elif num == 4:
            info.layers = val
        elif num == 5:
            if wire == 2:
                pos = 0
                while pos < len(val):
                    sid, pos = _varint(val, pos)
                    info.sprite_ids.append(sid)
            else:
                info.sprite_ids.append(val)
        elif num == 6:
            info.durations = _parse_animation(val)
    return info


@dataclass
class Appearance:
    id: int
    groups: dict[int, SpriteInfo]
    flags: set[int]

    def group(self, *preferred: int) -> SpriteInfo:
        for kind in preferred:
            if kind in self.groups:
                return self.groups[kind]
        return next(iter(self.groups.values()))


def load_appearances(dat: Path, kind: int) -> dict[int, Appearance]:
    """Appearances of one kind (object / outfit) with frame groups + set flags."""
    out: dict[int, Appearance] = {}
    for num, _, val in fields(dat.read_bytes()):
        if num != kind:
            continue
        app = Appearance(0, {}, set())
        for anum, _, aval in fields(val):
            if anum == 1:
                app.id = aval
            elif anum == 2:
                group, info = FRAME_GROUP_IDLE, None
                for gnum, _, gval in fields(aval):
                    if gnum == 1:
                        group = gval
                    elif gnum == 3:
                        info = _parse_sprite_info(gval)
                if info is not None:
                    app.groups.setdefault(group, info)
            elif anum == 3:
                app.flags = {fnum for fnum, wire, fval in fields(aval) if wire != 0 or fval}
        if app.id and app.groups:
            out[app.id] = app
    return out


# --- CIP sprite sheets ------------------------------------------------------


def _decode_sheet(raw: bytes) -> bytes:
    """32-byte CIP header + raw LZMA1 BMP → top-down RGBA (384x384)."""
    pos = 0
    while raw[pos] == 0:
        pos += 1
    pos += 5  # 0x70 0x0A 0xFA 0x80 0x24
    while raw[pos] & 0x80:
        pos += 1
    pos += 1
    props = raw[pos]
    dict_size = struct.unpack_from("<I", raw, pos + 1)[0]
    pos += 5 + 8  # props + dict size + CIP compressed size
    lc, rem = props % 9, props // 9
    filters = [{"id": lzma.FILTER_LZMA1, "lc": lc, "lp": rem % 5, "pb": rem // 5, "dict_size": dict_size}]
    bmp = lzma.LZMADecompressor(lzma.FORMAT_RAW, filters=filters).decompress(raw[pos:])
    offset = struct.unpack_from("<I", bmp, 10)[0]
    row = SHEET_SIZE * 4
    data = bmp[offset : offset + row * SHEET_SIZE]
    px = bytearray(b"".join(data[(SHEET_SIZE - 1 - y) * row : (SHEET_SIZE - y) * row] for y in range(SHEET_SIZE)))
    px[0::4], px[2::4] = px[2::4], px[0::4]  # BGRA → RGBA
    for m in MAGENTA.finditer(px):
        i = m.start()
        if i % 4 == 0:
            px[i : i + 4] = b"\0\0\0\0"
    return bytes(px)


@dataclass
class Sheet:
    file: Path
    layout: int
    first: int
    last: int
    pixels: bytes | None = None

    @property
    def size(self) -> tuple[int, int]:
        return SPRITE_SIZES[self.layout] if self.layout < len(SPRITE_SIZES) else (32, 32)


class SpriteStore:
    def __init__(self, things: Path):
        catalog = json.loads((things / "catalog-content.json").read_text())
        self.sheets = sorted(
            (
                Sheet(things / e["file"], int(e.get("spritetype", 0)), e["firstspriteid"], e["lastspriteid"])
                for e in catalog
                if e.get("type") == "sprite"
            ),
            key=lambda s: s.first,
        )
        dat = next((e["file"] for e in catalog if e.get("type") == "appearances"), None)
        if not dat:
            raise SystemExit(f"no appearances entry in {things}/catalog-content.json")
        self.appearances = things / dat

    def sprite(self, sprite_id: int | None) -> tuple[int, int, bytes] | None:
        if sprite_id is None:
            return None
        sheet = next((s for s in self.sheets if s.first <= sprite_id <= s.last), None)
        if sheet is None or not sheet.file.is_file():
            return None
        if sheet.pixels is None:
            sheet.pixels = _decode_sheet(sheet.file.read_bytes())
        w, h = sheet.size
        cols = SHEET_SIZE // w
        offset = sprite_id - sheet.first
        sx, sy = (offset % cols) * w, (offset // cols) * h
        row = SHEET_SIZE * 4
        px = sheet.pixels
        return w, h, b"".join(px[y * row + sx * 4 : y * row + (sx + w) * 4] for y in range(sy, sy + h))


# --- PNG / APNG -------------------------------------------------------------


def _chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))


def _idat(w: int, h: int, rgba: bytes) -> bytes:
    stride = w * 4
    return zlib.compress(b"".join(b"\0" + rgba[y * stride : (y + 1) * stride] for y in range(h)), 9)


def png(w: int, h: int, rgba: bytes) -> bytes:
    return (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + _chunk(b"IDAT", _idat(w, h, rgba))
        + _chunk(b"IEND", b"")
    )


def apng(w: int, h: int, frames: list[tuple[bytes, int]]) -> bytes:
    """Looping APNG from (rgba, delay_ms) frames; a single frame is a plain PNG."""
    if len(frames) == 1:
        return png(w, h, frames[0][0])
    out = [
        b"\x89PNG\r\n\x1a\n",
        _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)),
        _chunk(b"acTL", struct.pack(">II", len(frames), 0)),
    ]
    seq = 0
    for i, (rgba, delay) in enumerate(frames):
        out.append(_chunk(b"fcTL", struct.pack(">IIIIIHHBB", seq, w, h, 0, 0, max(delay, 10), 1000, 0, 0)))
        seq += 1
        data = _idat(w, h, rgba)
        if i == 0:
            out.append(_chunk(b"IDAT", data))
        else:
            out.append(_chunk(b"fdAT", struct.pack(">I", seq) + data))
            seq += 1
    out.append(_chunk(b"IEND", b""))
    return b"".join(out)


# --- paths / revisions ------------------------------------------------------


def find_things_dir() -> Path | None:
    if (ASSETS_ROOT / "catalog-content.json").is_file():
        return ASSETS_ROOT
    if not THINGS_ROOT.is_dir():
        return None
    versions = sorted(
        (p for p in THINGS_ROOT.iterdir() if (p / "catalog-content.json").is_file()),
        key=lambda p: int(p.name) if p.name.isdigit() else -1,
    )
    return versions[-1] if versions else None


def things_revision(things: Path, fmt: str) -> str:
    digest = hashlib.sha256((things / "catalog-content.json").read_bytes()).hexdigest()
    return f"{digest[:16]}-{fmt}"


def render_pack(
    label: str,
    things: Path,
    dest: Path,
    fmt: str,
    ids: list[int],
    init: Callable[[str, str], None],
    render_chunk: Callable[[list[int]], int],
) -> str:
    """Render `ids` into a fresh `dest` in parallel; swap in atomically, stamp `.revision`.

    `init(things, tmp_dest)` / `render_chunk(ids) -> rendered` run in worker
    processes and must be module-level functions.
    """
    rev = things_revision(things, fmt)
    stamp = dest / ".revision"
    if stamp.is_file() and stamp.read_text().strip() == rev:
        print(f"[{label}] up to date ({rev}) → {dest}", flush=True)
        return rev
    tmp = dest.with_name(dest.name + ".tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    workers = max(1, min(os.cpu_count() or 1, 8))
    size = max(1, len(ids) // (workers * 4))
    chunks = [ids[i : i + size] for i in range(0, len(ids), size)]
    print(f"[{label}] rendering {len(ids)} from {things} ({workers} workers)", flush=True)
    with ProcessPoolExecutor(workers, initializer=init, initargs=(str(things), str(tmp))) as pool:
        rendered = sum(pool.map(render_chunk, chunks))
    (tmp / ".revision").write_text(rev + "\n")
    if dest.is_symlink() or dest.is_file():
        dest.unlink()
    elif dest.exists():
        shutil.rmtree(dest)
    tmp.rename(dest)
    print(f"[{label}] {rendered} rendered ({rev}) → {dest}", flush=True)
    return rev
