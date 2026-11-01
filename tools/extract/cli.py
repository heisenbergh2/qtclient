#!/usr/bin/env python3
"""Unified Tibia.app ingest for qtclient (CipSoft / CIP client).

Default (CipSoft play path) — one read of a CipSoft install produces:
  - packages/asset-pipeline/data/tibia-client/   CIP catalogs + masks (pipeline)
  - packages/asset-pipeline/data/tibia-client/game-tables/
                                                spells, charms, Wheel of Destiny
                                                from the client binary
  - apps/qtclient/data/assets/                  CIP-flat assets (catalog,
                                                appearances, map, sprites,
                                                Resources/minimap, graphics_resources.rcc)
  - apps/qtclient/data/sounds/                  CIP sound bank (Resources sibling
                                                of assets/, not nested under it)

CipSoft already mounts CIP art from graphics_resources.rcc and the bundled
minimap; staticdata is loaded with official field numbers. Opt-in extras that
used to always run (Lua-era / portal-only):

  --spells        carve spell icon strips + remap spellInfo.generated.json
  --otmm          build tibiamaps minimap.otmm (+ markers JSON)
  --aac-sprites   render portal outfits_anim/ + items/ PNGs

Usage:
  make extract TIBIA=/path/to/Tibia.app
  make extract TIBIA=… EXTRACT_FLAGS='--aac-sprites --otmm'
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

_EXTRACT_DIR = Path(__file__).resolve().parent
_TOOLS_DIR = _EXTRACT_DIR.parent
_REPO = _TOOLS_DIR.parent
# Monorepo: tools/extract → repo with apps/qtclient. Standalone qtclient: tools/ next to client/tibia.
if (_REPO / "apps/qtclient/client/tibia").is_dir():
    ROOT = _REPO
    QTCLIENT_DIR = ROOT / "apps/qtclient"
elif (_REPO / "client/tibia").is_dir():
    ROOT = _REPO
    QTCLIENT_DIR = ROOT
else:
    ROOT = _REPO
    QTCLIENT_DIR = ROOT / "apps/qtclient" if (ROOT / "apps/qtclient").is_dir() else ROOT

TIBIA_CLIENT_DIR = ROOT / "packages/asset-pipeline/data/tibia-client"
HAS_ASSET_PIPELINE = (ROOT / "packages/asset-pipeline").is_dir()
DEFAULT_CLIENT_VERSION = os.environ.get("CLIENT_VERSION", "1533")
DEFAULT_ASSETS_DIR = QTCLIENT_DIR / "data" / "assets"

# Shared OS defaults (CipSoft package paths).
sys.path.insert(0, str(_TOOLS_DIR))
sys.path.insert(0, str(ROOT / "tools"))
from tibia_paths import resolve_tibia, usage_hint  # noqa: E402

SUBAREA_MASK_RE = re.compile(r"^subarea-.+\.bmp\.lzma$", re.I)
OFFICIAL_MASK_RE = re.compile(r"^subarea-\d+-[0-9a-f]{64}\.bmp\.lzma$", re.I)
GENERATED_MASK_RE = re.compile(r"^subarea-.+-generated\.bmp\.lzma$", re.I)
CATALOG_DAT_RE = re.compile(
    r"^(map|staticdata|staticmapdata|appearances)-.+\.dat$", re.I
)
PROFICIENCIES_RE = re.compile(r"^proficiencies-.+\.json$", re.I)


def log(msg: str) -> None:
    print(f"[extract] {msg}", flush=True)


def is_dir(p: Path) -> bool:
    try:
        return p.is_dir()
    except OSError:
        return False


def is_file(p: Path) -> bool:
    try:
        return p.is_file()
    except OSError:
        return False


def newest_matching(dir_path: Path, pattern: re.Pattern[str]) -> Path | None:
    if not is_dir(dir_path):
        return None
    hits = [
        dir_path / name
        for name in os.listdir(dir_path)
        if pattern.search(name) and is_file(dir_path / name)
    ]
    if not hits:
        return None
    hits.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return hits[0]


def find_app_root(start: Path) -> Path | None:
    cur = start
    for _ in range(8):
        if cur.name.endswith(".app") and is_dir(cur):
            return cur
        parent = cur.parent
        if parent == cur:
            break
        cur = parent
    return None


def resolve_tibia_install(tibia_path: str) -> dict[str, Path | None]:
    """Normalize --tibia= to assets/, MacOS/, and key catalog files."""
    input_path = Path(tibia_path).expanduser().resolve()
    if not input_path.exists():
        raise SystemExit(f"Tibia path does not exist: {input_path}")

    start = input_path.parent if input_path.is_file() else input_path
    app_root = find_app_root(start)

    mac_candidates = [
        start / "Contents" / "MacOS",
        start / "MacOS",
        start,
    ]
    if app_root:
        mac_candidates.append(app_root / "Contents" / "MacOS")

    resources_candidates = [
        start / "Contents" / "Resources",
        start / "Resources",
        start,
    ]
    if app_root:
        resources_candidates.append(app_root / "Contents" / "Resources")

    mac_os = next(
        (
            p
            for p in mac_candidates
            if is_dir(p) and is_file(p / "graphics_resources.rcc")
        ),
        next((p for p in mac_candidates if is_dir(p)), None),
    )
    resources = next(
        (p for p in resources_candidates if is_dir(p) and is_dir(p / "assets")),
        next((p for p in resources_candidates if is_dir(p)), None),
    )

    assets_candidates = [
        start / "assets",
        resources / "assets" if resources else None,
        start,
    ]
    assets = next(
        (
            p
            for p in assets_candidates
            if p and is_dir(p) and is_file(p / "catalog-content.json")
        ),
        next((p for p in assets_candidates if p and is_dir(p)), None),
    )

    if not assets:
        raise SystemExit(f"No Tibia assets/ under {input_path}")

    return {
        "input": input_path,
        "app_root": app_root,
        "mac_os": mac_os,
        "resources": resources,
        "assets": assets,
        "graphics_rcc": (
            mac_os / "graphics_resources.rcc"
            if mac_os and is_file(mac_os / "graphics_resources.rcc")
            else None
        ),
        "map": newest_matching(assets, re.compile(r"^map-.+\.dat$", re.I)),
        "staticdata": newest_matching(
            assets, re.compile(r"^staticdata-.+\.dat$", re.I)
        ),
        "staticmapdata": newest_matching(
            assets, re.compile(r"^staticmapdata-.+\.dat$", re.I)
        ),
        "appearances": newest_matching(
            assets, re.compile(r"^appearances-.+\.dat$", re.I)
        ),
        "proficiencies": newest_matching(
            assets, re.compile(r"^proficiencies-.+\.json$", re.I)
        ),
    }


def clear_matching(dir_path: Path, pattern: re.Pattern[str]) -> None:
    if not is_dir(dir_path):
        return
    for name in os.listdir(dir_path):
        if pattern.search(name):
            (dir_path / name).unlink(missing_ok=True)


def copy_into(dest_dir: Path, src: Path) -> Path:
    dest = dest_dir / src.name
    shutil.copy2(src, dest)
    return dest


def extract_cip(install: dict[str, Path | None], out_dir: Path) -> int:
    """Copy CIP catalogs + official subarea masks into data/tibia-client/."""
    assets = install["assets"]
    assert assets is not None
    for key in ("map", "staticdata", "staticmapdata", "appearances", "proficiencies"):
        if install[key] is None:
            raise SystemExit(f"missing {key} under {assets}")

    out_dir.mkdir(parents=True, exist_ok=True)
    clear_matching(out_dir, CATALOG_DAT_RE)
    clear_matching(out_dir, PROFICIENCIES_RE)
    clear_matching(out_dir, OFFICIAL_MASK_RE)

    copied = {
        "map": copy_into(out_dir, install["map"]),  # type: ignore[arg-type]
        "staticdata": copy_into(out_dir, install["staticdata"]),  # type: ignore[arg-type]
        "staticmapdata": copy_into(out_dir, install["staticmapdata"]),  # type: ignore[arg-type]
        "appearances": copy_into(out_dir, install["appearances"]),  # type: ignore[arg-type]
        "proficiencies": copy_into(out_dir, install["proficiencies"]),  # type: ignore[arg-type]
    }
    catalog = [
        {"type": "map", "file": copied["map"].name},
        {"type": "staticdata", "file": copied["staticdata"].name},
        {"type": "staticmapdata", "file": copied["staticmapdata"].name},
        {"type": "appearances", "file": copied["appearances"].name},
        {"type": "proficiencies", "file": copied["proficiencies"].name},
    ]
    (out_dir / "catalog-content.json").write_text(
        json.dumps(catalog, indent=2) + "\n", encoding="utf-8"
    )
    stamp_assets_identifier(out_dir)

    mask_count = 0
    for name in os.listdir(assets):
        if not SUBAREA_MASK_RE.match(name):
            continue
        src = assets / name
        if not is_file(src):
            continue
        shutil.copy2(src, out_dir / name)
        mask_count += 1

    log(f"CIP catalogs → {out_dir} ({mask_count} subarea masks)")
    return mask_count


def stamp_assets_identifier(assets_dir: Path) -> None:
    """Write assets.json.sha256 next to catalog-content.json for local --data boots.

    CipSoft keeps this filename at the package root after launcher install; the
    game also accepts it beside the catalog (publish_assets / legacy things/).
    """
    catalog = assets_dir / "catalog-content.json"
    if not catalog.is_file():
        return
    digest = hashlib.sha256(catalog.read_bytes()).hexdigest()
    (assets_dir / "assets.json.sha256").write_text(digest + "\n", encoding="utf-8")


def extract_things(
    install: dict[str, Path | None],
    things_dir: Path,
    skip_minimap: bool,
) -> None:
    assets = install["assets"]
    assert assets is not None
    if things_dir.exists():
        shutil.rmtree(things_dir)
    things_dir.mkdir(parents=True, exist_ok=True)

    file_count = 0
    mask_count = 0
    for dirpath, _dirnames, filenames in os.walk(assets):
        rel = Path(dirpath).relative_to(assets)
        for name in filenames:
            src = Path(dirpath) / name
            dest = things_dir / rel / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest)
            file_count += 1
            if SUBAREA_MASK_RE.match(name):
                mask_count += 1

    # CipSoft loads official CIP staticdata field numbers (no OTC remap).

    log(
        f"things → {things_dir} ({file_count} files, {mask_count} subarea masks)"
    )

    # Qt UI art pack lives next to the CIP binary (Contents/MacOS), not under
    # assets/. The tibia app mounts this as a resource set at startup.
    graphics_rcc = install.get("graphics_rcc")
    if graphics_rcc and is_file(graphics_rcc):
        copy_into(things_dir, graphics_rcc)
        log(f"graphics_resources.rcc → {things_dir / 'graphics_resources.rcc'}")
    else:
        log(
            "WARN: graphics_resources.rcc not found under Tibia.app "
            "(Contents/MacOS); QML chrome will be blank until it is copied"
        )

    # CIP default minimap (Resources/minimap/Minimap_{Color,WaypointCost}_*.png),
    # imported by the tibia app into an empty user minimap.
    resources = install["resources"]
    cip_minimap = resources / "minimap" if resources else None
    if cip_minimap and cip_minimap.is_dir():
        dest = things_dir / "minimap"
        shutil.copytree(cip_minimap, dest)
        log(f"CIP default minimap → {dest} ({len(os.listdir(dest))} files)")

    if skip_minimap:
        return
    # Full-world OTMM (color + walkability) from tibiamaps Color/WaypointCost PNGs.
    otmm_py = (
        ROOT / "packages/asset-pipeline/src/features/minimap/build-otmm-minimap.py"
    )
    if not otmm_py.is_file():
        log("WARN: build-otmm-minimap.py missing; skipped")
        return
    otmm_dest = things_dir / "minimap.otmm"
    try:
        subprocess.run(
            [
                sys.executable,
                str(otmm_py),
                f"--dest={otmm_dest}",
            ],
            cwd=str(ROOT),
            check=False,
        )
    except OSError as exc:
        log(f"WARN: otmm minimap build skipped: {exc}")

    # Minimap marker layer (same tibiamaps provenance as minimap.otmm).
    markers_py = (
        ROOT / "packages/asset-pipeline/src/features/minimap/build-minimap-markers.py"
    )
    if markers_py.is_file():
        try:
            subprocess.run(
                [
                    sys.executable,
                    str(markers_py),
                    f"--dest={things_dir / 'minimap-markers.json'}",
                ],
                cwd=str(ROOT),
                check=False,
            )
        except OSError as exc:
            log(f"WARN: minimap markers build skipped: {exc}")


def extract_sounds(
    install: dict[str, Path | None],
    sounds_dir: Path,
) -> None:
    """Copy CIP Resources/sounds → data/sounds/ (sibling of data/assets/).

    CipSoft resolves Resources as the search root and opens short names
    `sounds/<file>`. Prefer a full CIP Tibia.app — some PlayTC trees omit the .dat.
    """
    resources = install.get("resources")
    src = resources / "sounds" if resources else None
    if src is None or not is_dir(src) or not is_file(src / "catalog-sound.json"):
        log("WARN: sounds skipped — no Resources/sounds/catalog-sound.json")
        return

    sounds_dir.mkdir(parents=True, exist_ok=True)
    # Replace CIP bank files only — keep product anthem (startup.ogg).
    for path in sounds_dir.iterdir():
        name = path.name
        if path.is_file() and (
            name == "catalog-sound.json"
            or name.startswith("sound-")
            or (name.startswith("sounds-") and name.endswith(".dat"))
        ):
            path.unlink()

    file_count = 0
    for name in os.listdir(src):
        path = src / name
        if not is_file(path):
            continue
        shutil.copy2(path, sounds_dir / name)
        file_count += 1

    nested = sounds_dir.parent / "assets" / "sounds"
    if nested.is_dir() and nested.resolve() != sounds_dir.resolve():
        shutil.rmtree(nested)
        log(f"removed nested {nested} (CipSoft sounds are sibling of assets/)")

    catalog_path = sounds_dir / "catalog-sound.json"
    missing_dat: list[str] = []
    try:
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        for entry in catalog:
            if not isinstance(entry, dict) or entry.get("type") != "sounds":
                continue
            dat_name = entry.get("file")
            if dat_name and not is_file(sounds_dir / str(dat_name)):
                missing_dat.append(str(dat_name))
    except (json.JSONDecodeError, OSError) as exc:
        log(f"WARN: catalog-sound.json unreadable: {exc}")

    if missing_dat:
        log(
            f"WARN: sounds catalog references missing .dat ({', '.join(missing_dat)}); "
            "OTC/CIP need the protobuf sounds-*.dat for effect-id → ogg mapping. "
            "15.30 PlayTC trees sometimes omit it — use a full CIP Tibia.app."
        )
    log(f"sounds → {sounds_dir} ({file_count} files)")


def extract_spells(tibia: str) -> None:
    """Carve and remap spell icons from the Tibia RCC in-process."""
    if __package__:
        from .spell_icons import sync_spell_icons
    else:
        from spell_icons import sync_spell_icons

    log("spell icons via Python RCC sync")
    sync_spell_icons(tibia)


def extract_game_tables(install: dict[str, Path | None], out_dir: Path) -> None:
    """Spells, charms and Wheel of Destiny tables carried by the client binary."""
    mac_os = install["mac_os"]
    client = mac_os / "client" if mac_os else None
    if client is None or not is_file(client):
        log("WARN: game tables skipped — no macOS client binary (Contents/MacOS/client)")
        return
    sys.path.insert(0, str(ROOT / "packages/asset-pipeline/src"))
    from tibia.game_tables import GameTableError, write_game_tables

    dest = out_dir / "game-tables"
    try:
        tables = write_game_tables(client, dest)
    except GameTableError as exc:
        log(f"WARN: game tables skipped — {exc} (see docs/reverse-engineering/game-data-tables.md)")
        return
    log(
        f"game tables → {dest} ({len(tables['spells.json'])} spells, "
        f"{len(tables['charms.json'])} charms, "
        f"{len(tables['wheel.json']['supreme_mods'])} supreme gem mods)"
    )


def extract_aac_sprites(things_dir: Path) -> None:
    """Render AAC outfit frames + item sprites from the freshly extracted things pack."""
    if __package__:
        from . import items, outfits
    else:
        import items
        import outfits

    outfits.render_all(things_dir)
    items.render_all(things_dir)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tibia",
        default=os.environ.get("TIBIA") or None,
        help="Path to Tibia.app / Contents/Resources / assets/",
    )
    parser.add_argument(
        "--client-version",
        default=DEFAULT_CLIENT_VERSION,
        help=f"Protocol version stamp for sounds/legacy (default: {DEFAULT_CLIENT_VERSION})",
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=DEFAULT_ASSETS_DIR,
        help=f"CIP-flat assets dump (default: {DEFAULT_ASSETS_DIR})",
    )
    parser.add_argument("--skip-cip", action="store_true")
    parser.add_argument("--skip-things", action="store_true")
    parser.add_argument(
        "--skip-game-tables",
        action="store_true",
        help="skip spells/charms/wheel tables read from the client binary",
    )
    parser.add_argument(
        "--spells",
        action="store_true",
        help="carve spell icon strips + remap spellInfo (CipSoft uses :/images/spells from RCC)",
    )
    parser.add_argument(
        "--otmm",
        action="store_true",
        help="build tibiamaps minimap.otmm + markers (CipSoft uses CIP Resources/minimap)",
    )
    parser.add_argument(
        "--aac-sprites",
        action="store_true",
        help="render portal outfits_anim/ + items/ (website CDN; not needed for CipSoft)",
    )
    # Compat aliases (default CipSoft path already skips these).
    parser.add_argument("--skip-spells", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--skip-minimap", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--skip-aac-sprites", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=TIBIA_CLIENT_DIR,
        help="CIP catalog output (default: packages/asset-pipeline/data/tibia-client)",
    )
    args = parser.parse_args(argv)

    tibia = args.tibia
    if not tibia:
        try:
            tibia = str(resolve_tibia())
            log(f"using Tibia install: {tibia}")
        except FileNotFoundError:
            parser.error(usage_hint())

    version = str(args.client_version).strip()
    assets_dir = Path(args.assets_dir).resolve()
    # CipSoft: Resources/{assets,sounds} siblings. Dev --data uses data/{assets,sounds}.
    if assets_dir.name == "assets":
        sounds_dir = assets_dir.parent / "sounds"
    else:
        sounds_dir = QTCLIENT_DIR / "data" / "sounds" / version

    install = resolve_tibia_install(tibia)
    log(f"Tibia install: {install['input']}")
    log(f"CIP assets src: {install['assets']}")
    log(f"assets dest: {assets_dir}")
    log(f"sounds dest: {sounds_dir}")

    if not args.skip_cip:
        if HAS_ASSET_PIPELINE or args.out_dir != TIBIA_CLIENT_DIR:
            extract_cip(install, args.out_dir.resolve())
        else:
            log("asset-pipeline absent — skipping CIP catalog copy (client assets only)")

    if not args.skip_game_tables:
        if HAS_ASSET_PIPELINE or args.out_dir != TIBIA_CLIENT_DIR:
            extract_game_tables(install, args.out_dir.resolve())
        else:
            log("asset-pipeline absent — skipping game tables")

    if not args.skip_things:
        extract_things(install, assets_dir, skip_minimap=not args.otmm)
        stamp_assets_identifier(assets_dir)

    extract_sounds(install, sounds_dir)

    if args.spells and not args.skip_spells:
        extract_spells(tibia)
    else:
        log("spell icons skipped (CipSoft: graphics_resources.rcc; pass --spells to carve)")

    if args.aac_sprites and not args.skip_aac_sprites:
        if (assets_dir / "catalog-content.json").is_file():
            extract_aac_sprites(assets_dir)
        else:
            log(f"WARN: AAC outfits/items skipped — no assets at {assets_dir}")
    else:
        log("portal outfits/items skipped (pass --aac-sprites for website CDN sprites)")

    log("done.")
    log("next: make generate  # catalog pass (reads CIP + Crystal)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
