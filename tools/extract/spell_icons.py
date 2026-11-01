#!/usr/bin/env python3
"""Sync qtclient spell icon strips and client IDs from a Tibia install."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import sys
import urllib.request
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except ImportError:  # Keep --help and module imports usable without Pillow.
    Image = None  # type: ignore[assignment]


ROOT = Path(__file__).resolve().parents[2]
SPELLS_LUA = ROOT / "apps/qtclient/modules/gamelib/spells.lua"
SPELL_INFO_JSON = ROOT / "packages/asset-pipeline/data/spellInfo.generated.json"
MAP_PY = ROOT / "apps/qtclient/docs/generate_spell_data.py"
OUT32 = ROOT / "apps/qtclient/data/images/game/spells/spell-icons-32x32.png"
OUT20 = ROOT / "apps/qtclient/data/images/game/spells/spell-icons-20x20.png"
OUT_GROUP = (
    ROOT / "apps/qtclient/data/images/game/spells/spellgroup-icons-20x20.png"
)

STANCE_CLIENT_IDS = {
    "utito tempo": 95,
    "utamo tempo": 118,
    "utamo tempo san": 116,
    "utori con": 193,
    "utori hur": 194,
    "utito dru": 190,
    "utura sio": 191,
    "uteta flam": 187,
    "uteta vis": 188,
    "uteta mort": 189,
    "exori moe": 200,
    "exori kor": 199,
    "exori kor tempo": 200,
    "exori moe tempo": 199,
    "utori virtu": 159,
    "utito virtu": 160,
    "utura tio": 161,
}

STANCE_FLAG_WORDS = {
    "utito tempo",
    "utamo tempo",
    "utori con",
    "utori hur",
    "utito dru",
    "utura sio",
    "uteta flam",
    "uteta vis",
    "uteta mort",
    "exori kor tempo",
    "exori moe tempo",
    "utori virtu",
    "utito virtu",
    "utura tio",
}

WIKI_STANCE_GIFS = {
    "Blood Rage": (
        "https://static.wikia.nocookie.net/tibia/images/4/47/"
        "Blood_Rage.gif/revision/latest?cb=20111113181847&path-prefix=en"
    ),
    "Protector": (
        "https://static.wikia.nocookie.net/tibia/images/c/cd/"
        "Protector.gif/revision/latest?cb=20111113182637&path-prefix=en"
    ),
    "Sharpshooter": (
        "https://static.wikia.nocookie.net/tibia/images/b/b8/"
        "Sharpshooter.gif/revision/latest?cb=20260623062017&path-prefix=en"
    ),
    "Divine Defiance": (
        "https://static.wikia.nocookie.net/tibia/images/8/88/"
        "Divine_Defiance.gif/revision/latest?cb=20260513162633&path-prefix=en"
    ),
    "Elemental Synthesis": (
        "https://static.wikia.nocookie.net/tibia/images/6/6e/"
        "Elemental_Synthesis.gif/revision/latest?cb=20260513162840&path-prefix=en"
    ),
    "Shared Conservation": (
        "https://static.wikia.nocookie.net/tibia/images/b/b8/"
        "Shared_Conservation.gif/revision/latest?cb=20260513162936&path-prefix=en"
    ),
    "Master of Flames": (
        "https://static.wikia.nocookie.net/tibia/images/d/d5/"
        "Master_of_Flames.gif/revision/latest?cb=20260513184856&path-prefix=en"
    ),
    "Master of Thunder": (
        "https://static.wikia.nocookie.net/tibia/images/3/3f/"
        "Master_of_Thunder.gif/revision/latest?cb=20260513184906&path-prefix=en"
    ),
    "Master of Decay": (
        "https://static.wikia.nocookie.net/tibia/images/a/a5/"
        "Master_of_Decay.gif/revision/latest?cb=20260513184919&path-prefix=en"
    ),
}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _require_pillow() -> None:
    if Image is None:
        raise RuntimeError(
            "Pillow is required for spell icon extraction; "
            "install it with `python3 -m pip install Pillow`"
        )


def extract_pngs(rcc: bytes) -> list[bytes]:
    """Carve embedded PNG files from a Qt RCC byte stream."""
    pngs: list[bytes] = []
    offset = 0
    while True:
        start = rcc.find(PNG_SIGNATURE, offset)
        if start < 0:
            break
        iend = rcc.find(b"IEND", start)
        if iend < 0:
            break
        end = iend + 8
        pngs.append(rcc[start:end])
        offset = end
    return pngs


def decode_png(blob: bytes) -> Any:
    _require_pillow()
    assert Image is not None
    with Image.open(io.BytesIO(blob)) as source:
        return source.convert("RGBA")


def encode_png_rgb(image: Any) -> bytes:
    output = io.BytesIO()
    image.convert("RGB").save(output, format="PNG")
    return output.getvalue()


def find_strip(pngs: list[bytes], width: int, height: int) -> Any:
    for blob in pngs:
        try:
            image = decode_png(blob)
        except Exception:
            continue
        if image.size == (width, height):
            return image
    raise RuntimeError(f"No {width}x{height} PNG strip in RCC")


def find_strip_named(pngs: list[bytes], prefer_width: int, prefer_height: int) -> Any:
    try:
        return find_strip(pngs, prefer_width, prefer_height)
    except RuntimeError:
        best = None
        for blob in pngs:
            try:
                image = decode_png(blob)
            except Exception:
                continue
            width, height = image.size
            if (
                height == prefer_height
                and width % prefer_height == 0
                and width > prefer_height * 50
                and (best is None or width > best.width)
            ):
                best = image
        if best is None:
            raise RuntimeError(
                f"No {prefer_width}x{prefer_height} PNG strip in RCC"
            )
        return best


def _crop(image: Any, x: int, y: int, width: int, height: int) -> Any:
    return image.crop((x, y, x + width, y + height))


def _nearest_resize(image: Any, width: int, height: int) -> Any:
    if image.size == (width, height):
        return image
    assert Image is not None
    resampling = getattr(Image, "Resampling", Image)
    return image.resize((width, height), resample=resampling.NEAREST)


def icon_hash(sheet: Any, index: int, size: int) -> str:
    tile = _crop(sheet, index * size, 0, size, size)
    return hashlib.md5(tile.tobytes(), usedforsecurity=False).hexdigest()


def build_remap(
    old_sheet: Any, new_sheet: Any, size: int
) -> tuple[dict[int, int], list[int]]:
    new_map: dict[str, list[int]] = {}
    for index in range(new_sheet.width // size):
        new_map.setdefault(icon_hash(new_sheet, index, size), []).append(index)

    remap: dict[int, int] = {}
    unmapped: list[int] = []
    claimed_new: set[int] = set()
    for index in range(old_sheet.width // size):
        hits = new_map.get(icon_hash(old_sheet, index, size), [])
        chosen = next((hit for hit in hits if hit not in claimed_new), None)
        if chosen is not None:
            remap[index] = chosen
            claimed_new.add(chosen)
        else:
            # Duplicate old tiles (or exhausted new matches) stay unique by
            # appending the old icon in sync_spell_icons.
            unmapped.append(index)
    return remap, unmapped


def append_icons(base: Any, extras: list[Any], size: int) -> Any:
    if not extras:
        return base
    assert Image is not None
    result = Image.new("RGBA", (base.width + size * len(extras), size))
    result.paste(base, (0, 0))
    first_extra = base.width // size
    for offset, icon in enumerate(extras):
        tile = _nearest_resize(icon, size, size)
        result.paste(tile, ((first_extra + offset) * size, 0))
    return result


def map_client_id(
    old: int, words: str, remap: dict[int, int], max_icon: int
) -> int:
    normalized = (words or "").lower().strip()
    if normalized in STANCE_CLIENT_IDS:
        return STANCE_CLIENT_IDS[normalized]
    if old in remap:
        return remap[old]
    if 0 <= old <= max_icon:
        return old
    return remap.get(105, min(105, max_icon))


def update_spells_lua(
    remap: dict[int, int], max_icon: int, dry_run: bool = False
) -> int:
    # Legacy qtclient Lua module — retired with the Lua client. Keep the remap
    # path for optional checkouts that still ship modules/gamelib/spells.lua.
    if not SPELLS_LUA.is_file():
        return 0
    lua = SPELLS_LUA.read_text(encoding="utf-8")
    changes = 0
    entry_re = re.compile(r"(\['(?:[^']+)'\]\s*=\s*\{)(.*?)(\},)", re.DOTALL)

    def replace_entry(match: re.Match[str]) -> str:
        nonlocal changes
        prefix, body, suffix = match.groups()
        client_match = re.search(r"clientId\s*=\s*(-?\d+)", body)
        if client_match is None:
            return match.group(0)
        words_match = re.search(r"words\s*=\s*'([^']*)'", body)
        words = words_match.group(1) if words_match else ""
        old = int(client_match.group(1))
        mapped = map_client_id(old, words, remap, max_icon)
        if mapped == old:
            return match.group(0)
        changes += 1
        patched = (
            body[: client_match.start()]
            + f"clientId = {mapped}"
            + body[client_match.end() :]
        )
        return f"{prefix}{patched}{suffix}"

    updated = entry_re.sub(replace_entry, lua)
    if not dry_run:
        SPELLS_LUA.write_text(updated, encoding="utf-8")
    return changes


def update_spell_info_json(
    remap: dict[int, int], max_icon: int, dry_run: bool = False
) -> int:
    info = json.loads(SPELL_INFO_JSON.read_text(encoding="utf-8"))
    defaults = info["SpellInfo"]["Default"]
    changes = 0
    for spell in defaults.values():
        if not isinstance(spell, dict) or "clientId" not in spell:
            continue
        old = int(spell["clientId"])
        words = str(spell.get("words", ""))
        mapped = map_client_id(old, words, remap, max_icon)
        if mapped != old:
            spell["clientId"] = mapped
            changes += 1
        if words.lower() in STANCE_FLAG_WORDS and spell.get("stance") is not True:
            spell["stance"] = True
            changes += 1
    if not dry_run:
        SPELL_INFO_JSON.write_text(
            json.dumps(info, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    return changes


def update_map_icon_index(
    remap: dict[int, int], max_icon: int, dry_run: bool = False
) -> int:
    source = MAP_PY.read_text(encoding="utf-8")
    changes = 0
    entry_re = re.compile(r'"([^"]+)":\s*\((\d+|None),\s*(\d+|None)\),')

    def replace_entry(match: re.Match[str]) -> str:
        nonlocal changes
        words, icon_text, spell_id = match.groups()
        if icon_text == "None":
            return match.group(0)
        old = int(icon_text)
        mapped = map_client_id(old, words, remap, max_icon)
        if mapped == old:
            return match.group(0)
        changes += 1
        return f'"{words}": ({mapped}, {spell_id}),'

    updated = entry_re.sub(replace_entry, source)
    extras: list[str] = []
    for words, client_id in STANCE_CLIENT_IDS.items():
        marker = f'"{words}"'
        if marker not in updated:
            extras.append(f'    "{words}": ({client_id}, None),')
            continue
        stance_re = re.compile(
            rf'("{re.escape(words)}":\s*\()(\d+|None)(,\s*(?:\d+|None)\),)'
        )
        updated = stance_re.sub(rf"\g<1>{client_id}\g<3>", updated)

    if extras:
        anchor = '    "exura tio sio":'
        if anchor in updated:
            updated = updated.replace(anchor, "\n".join(extras) + "\n" + anchor, 1)
    if not dry_run:
        MAP_PY.write_text(updated, encoding="utf-8")
    return changes


def verify_wiki_matches(sheet: Any) -> None:
    print("Wiki verification (38x38 -> crop 3px -> nearest strip tile):")
    request_headers = {"User-Agent": "Mozilla/5.0"}
    for name, url in WIKI_STANCE_GIFS.items():
        try:
            request = urllib.request.Request(url, headers=request_headers)
            with urllib.request.urlopen(request, timeout=20) as response:
                image = decode_png(response.read())
            if image.size == (38, 38):
                candidate = _crop(image, 3, 3, 32, 32)
            else:
                candidate = _nearest_resize(image, 32, 32)

            candidate_rgb = candidate.convert("RGB")
            best_mse = float("inf")
            best_index = -1
            for index in range(sheet.width // 32):
                tile = _crop(sheet, index * 32, 0, 32, 32).convert("RGB")
                error = sum(
                    (left - right) ** 2
                    for left, right in zip(tile.tobytes(), candidate_rgb.tobytes())
                ) / (32 * 32)
                if error < best_mse:
                    best_mse = error
                    best_index = index
            print(f"  {name}: tile={best_index} mse={best_mse:.1f}")
        except Exception as exc:
            print(f"  {name}: FAIL {exc}")


def resolve_graphics_rcc(tibia: str) -> Path:
    """Resolve graphics_resources.rcc from Tibia.app or one of its subpaths."""
    input_path = Path(tibia).expanduser().resolve()
    if not input_path.exists():
        raise FileNotFoundError(f"Tibia path does not exist: {input_path}")
    if input_path.is_file() and input_path.name == "graphics_resources.rcc":
        return input_path

    start = input_path.parent if input_path.is_file() else input_path
    app_root = None
    current = start
    for _ in range(8):
        if current.name.endswith(".app") and current.is_dir():
            app_root = current
            break
        if current.parent == current:
            break
        current = current.parent

    candidates = [
        start / "Contents/MacOS/graphics_resources.rcc",
        start / "MacOS/graphics_resources.rcc",
        start / "graphics_resources.rcc",
    ]
    if app_root is not None:
        candidates.append(app_root / "Contents/MacOS/graphics_resources.rcc")
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    search_root = app_root or start
    try:
        match = next(search_root.rglob("graphics_resources.rcc"), None)
    except OSError:
        match = None
    if match is not None and match.is_file():
        return match
    raise FileNotFoundError(f"Could not find graphics_resources.rcc under {tibia}")


def sync_spell_icons(
    tibia: str, *, dry_run: bool = False, verify_wiki: bool = False
) -> None:
    _require_pillow()
    rcc_path = resolve_graphics_rcc(tibia)
    print(f"[spell-icons] RCC: {rcc_path}")
    pngs = extract_pngs(rcc_path.read_bytes())
    print(f"[spell-icons] PNG blobs in RCC: {len(pngs)}")

    tibia32 = find_strip_named(pngs, 6432, 32)
    tibia20 = find_strip_named(pngs, 4020, 20)
    print(
        f"[spell-icons] new 32 strip: {tibia32.width}x{tibia32.height} "
        f"icons={tibia32.width // 32}"
    )
    print(
        f"[spell-icons] new 20 strip: {tibia20.width}x{tibia20.height} "
        f"icons={tibia20.width // 20}"
    )

    otc32 = decode_png(OUT32.read_bytes())
    otc20 = decode_png(OUT20.read_bytes())
    remap, unmapped = build_remap(otc32, tibia32, 32)
    unmapped_text = ",".join(str(index) for index in unmapped) or "(none)"
    print(
        f"[spell-icons] remap {len(remap)} icons; "
        f"unmapped OTC ids: {unmapped_text}"
    )

    append_ids = [index for index in unmapped if index != 120]
    extras32 = [_crop(otc32, index * 32, 0, 32, 32) for index in append_ids]
    extras20 = [
        (
            _crop(otc20, index * 20, 0, 20, 20)
            if index < otc20.width // 20
            else _nearest_resize(extras32[offset], 20, 20)
        )
        for offset, index in enumerate(append_ids)
    ]

    new32 = append_icons(tibia32, extras32, 32)
    new20 = append_icons(tibia20, extras20, 20)
    base_count = tibia32.width // 32
    for offset, old in enumerate(append_ids):
        remap[old] = base_count + offset
    if 120 in unmapped:
        remap[120] = STANCE_CLIENT_IDS["utori con"]

    max_icon = new32.width // 32 - 1
    print(f"[spell-icons] final strip icons: 0..{max_icon}")

    if verify_wiki:
        verify_wiki_matches(new32)

    if dry_run:
        print("[spell-icons] dry-run: not writing files")
        for words, client_id in sorted(STANCE_CLIENT_IDS.items()):
            print(f"  stance {words} -> {client_id}")
        return

    OUT32.write_bytes(encode_png_rgb(new32))
    OUT20.write_bytes(encode_png_rgb(new20))
    try:
        group = find_strip(pngs, 220, 20)
        OUT_GROUP.write_bytes(encode_png_rgb(group))
        print(f"[spell-icons] wrote {OUT_GROUP}")
    except RuntimeError:
        print("[spell-icons] spellgroup strip not found; left unchanged")

    print(f"[spell-icons] wrote {OUT32}")
    print(f"[spell-icons] wrote {OUT20}")
    lua_changes = update_spells_lua(remap, max_icon)
    if SPELLS_LUA.is_file():
        print(f"[spell-icons] spells.lua changes: {lua_changes}")
    else:
        print("[spell-icons] spells.lua skipped (Lua client module not present)")
    print(
        "[spell-icons] spellInfo.json changes: "
        f"{update_spell_info_json(remap, max_icon)}"
    )
    print(
        "[spell-icons] MAP_ICON_INDEX changes: "
        f"{update_map_icon_index(remap, max_icon)}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tibia",
        required=True,
        help="Path to Tibia.app, a subdirectory, or graphics_resources.rcc",
    )
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verify-wiki", action="store_true")
    args = parser.parse_args(argv)
    try:
        sync_spell_icons(
            args.tibia,
            dry_run=args.dry_run,
            verify_wiki=args.verify_wiki,
        )
    except (FileNotFoundError, RuntimeError) as exc:
        parser.exit(1, f"spell_icons.py: error: {exc}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
