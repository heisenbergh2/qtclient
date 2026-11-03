#!/usr/bin/env python3
"""Point an existing dist/cdn at a local build without repacking other slices.

`make cdn` still does the full assemble. These flags only rewrite
`latest/dev.json` (desktop) or replace the wasm files already named by
`latest/release.json` (browser).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OTC = HERE.parents[1]
UMBRELLA = OTC.parent.parent if OTC.parent.name == "apps" else OTC.parent
CDN = UMBRELLA / "dist" / "cdn"
DEV = CDN / "latest" / "dev.json"
RELEASE = CDN / "latest" / "release.json"

WASM_FILES = (
    "tibia.html",
    "tibia.js",
    "tibia.wasm",
    "tibia.data",
    "qtloader.js",
    "qtlogo.svg",
)

LAUNCHER_WASM_FILES = (
    "launcher.html",
    "launcher.js",
    "launcher.wasm",
    "qtloader.js",
    "qtlogo.svg",
)


def fail(message: str) -> int:
    print(f"[dev-cdn] {message}", file=sys.stderr)
    return 1


def cdn_ready() -> bool:
    if RELEASE.is_file():
        return True
    print("[dev-cdn] dist/cdn is missing — run make cdn once", file=sys.stderr)
    return False


def load_dev() -> dict:
    try:
        return json.loads(DEV.read_text())
    except (OSError, ValueError):
        return {}


def write_dev(doc: dict) -> None:
    DEV.parent.mkdir(parents=True, exist_ok=True)
    DEV.write_text(json.dumps(doc, indent=2) + "\n")


def find_desktop_binary() -> Path | None:
    """qtclient Play binary at build/<preset>/bin/… (see scripts/lifecycle/lib/desktop-paths.sh)."""
    import os
    import platform as py_platform

    system = py_platform.system()
    if system == "Darwin":
        os_name = "macos"
    elif system == "Linux":
        os_name = "linux"
    else:
        return None
    base = "release"
    dev = os.environ.get("DEV", "1")
    if dev.lower() not in ("0", "false", "no", "off"):
        base = "dev"
    debug = os.environ.get("DEBUG", "")
    if debug.lower() in ("1", "true", "yes", "on"):
        base = "debug"
    preset = os.environ.get("TIBIA_PRESET", f"{os_name}-{base}")
    bin_dir = OTC / "build" / preset / "bin"
    if system == "Darwin":
        candidate = bin_dir / "client.app" / "Contents" / "MacOS" / "client"
    else:
        candidate = bin_dir / "client"
    return candidate if candidate.is_file() else None


def place(src: Path, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() or dest.is_symlink():
        try:
            if src.samefile(dest):
                return
        except OSError:
            pass
        dest.unlink()
    try:
        os.link(src, dest)
    except OSError:
        shutil.copy2(src, dest)


def desktop() -> int:
    if not cdn_ready():
        return 0
    binary = find_desktop_binary()
    if binary is None:
        return fail("no desktop client binary — run make client-build")
    # AAC /api/login gates on latest/release.json revision (errorCode 7). Never
    # stamp mtime here — that made every DEV Play look "out of date".
    try:
        release_rev = str(json.loads(RELEASE.read_text()).get("revision") or "").strip()
    except (OSError, ValueError):
        release_rev = ""
    doc = load_dev()
    doc["desktop"] = {
        "exe": str(binary),
        "cwd": str(OTC),
        "revision": release_rev or "dev",
    }
    write_dev(doc)
    print(f"[dev-cdn] desktop -> {binary} revision={doc['desktop']['revision']}")
    return 0


def _wasm_src_dir() -> Path | None:
    """Prefer build/tibia-wasm/bin (TOGGLE_BIN_FOLDER) over stale root symlinks.

    Older cmake layouts left tibia.wasm at the build root (or a symlink into
    client/tibia/). Linked-tip installs write the live tip under bin/; if we
    pick the root first, make qtclient wasm install republishes the old Soft
    and /play keeps SIDE_MODULE dlopen against native/mod.
    """
    root = Path(os.environ.get("TIBIA_WASM_DIR", OTC / "build" / "tibia-wasm"))
    candidates = (root / "bin", root)
    best: Path | None = None
    best_mtime = -1.0
    for cand in candidates:
        wasm = cand / "tibia.wasm"
        if not wasm.is_file():
            continue
        try:
            mtime = wasm.resolve().stat().st_mtime
        except OSError:
            mtime = wasm.stat().st_mtime
        if mtime >= best_mtime:
            best = cand
            best_mtime = mtime
    return best


def _cip_stamp(fallback_path: Path) -> str:
    # Match CIP Play-button stamps (15.33.<6 hex>). Prefer SOURCE_COMMIT
    # (CI), else content hash of the tip binary so CipSoft reinstalls after a
    # local wasm rebuild even when git HEAD is unchanged.
    cip = "15.33"
    version_json = OTC / "cip" / "VERSION.json"
    if version_json.is_file():
        try:
            cip = json.loads(version_json.read_text()).get("cip_version") or cip
        except (OSError, ValueError):
            pass
    sha = os.environ.get("SOURCE_COMMIT", "").strip()
    if not sha and fallback_path.is_file():
        sha = hashlib.sha256(fallback_path.read_bytes()).hexdigest()
    if not sha:
        try:
            import subprocess

            sha = subprocess.run(
                ["git", "-C", str(OTC), "rev-parse", "HEAD"],
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
        except (OSError, subprocess.CalledProcessError, ValueError):
            sha = f"{int(fallback_path.stat().st_mtime_ns):x}"
    return f"{cip}.{sha[:6]}"


def wasm() -> int:
    if not cdn_ready():
        return 0
    release = json.loads(RELEASE.read_text())
    wasm_rel = (release.get("targets") or {}).get("web", {}).get("wasm")
    if not wasm_rel:
        return fail("release.json has no web wasm directory — run make cdn once")
    src_dir = _wasm_src_dir()
    if src_dir is None:
        return fail("no wasm build — run make browser-build")
    dest_dir = CDN / wasm_rel
    for name in WASM_FILES:
        src = src_dir / name
        if src.is_file():
            place(src, dest_dir / name)
    wasm_bin = src_dir / "tibia.wasm"
    for ext in ("gz", "br"):
        src = wasm_bin.with_name(f"tibia.wasm.{ext}")
        dest = dest_dir / src.name
        js = src_dir / f"tibia.js.{ext}"
        if src.is_file():
            place(src, dest)
        elif dest.exists():
            dest.unlink()
        js_dest = dest_dir / js.name
        if js.is_file():
            place(js, js_dest)
        elif js_dest.exists():
            js_dest.unlink()
    stat = wasm_bin.stat()
    doc = load_dev()
    doc["web"] = {"stamp": f"{stat.st_size}-{stat.st_mtime_ns}"}
    write_dev(doc)
    print(f"[dev-cdn] wasm -> {dest_dir} ({doc['web']['stamp']})")
    _resync_tip_pack(src_dir, dest_dir)
    # CipSoft tip installs qtclient from tibiaclient-web-current — keep it in sync
    # with the content-addressed slice (Play loads tibia from that package).
    rc = tibia_client_web(src_dir)
    if rc != 0:
        return rc
    return launcher()


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _cip_file_record(dest_dir: Path, localfile: str, data: bytes) -> dict:
    """CIP package.json file entry + tip-local .lzma + content-addressed blob.

    /play always fetches `/content/<aa>/<packedhash>` (see tibia-browser
    contentUrl). Writing only beside the tip leaves package.json hashes 404.
    """
    # Late import: content_store lives next to this module.
    import content_store  # noqa: PLC0415

    packed = lzma.compress(data, format=lzma.FORMAT_ALONE)
    packed_hash = _sha256(packed)
    lzma_name = f"{localfile}.lzma"
    (dest_dir / lzma_name).write_bytes(packed)
    # Same store full assemble uses — required for tipWarm / contentUrl boots.
    content_store.put_blob(CDN / "content", packed)
    return {
        "url": lzma_name,
        "localfile": localfile,
        "packedhash": packed_hash,
        "packedsize": len(packed),
        "unpackedhash": _sha256(data),
        "unpackedsize": len(data),
    }


def tibia_client_web(src_dir: Path | None = None) -> int:
    """Hot-swap CipSoft tip client under launcher/tibiaclient-web-current."""
    if not cdn_ready():
        return 0
    if src_dir is None:
        src_dir = _wasm_src_dir()
    if src_dir is None:
        return fail("no tibia.wasm — run make browser-build")
    dest_dir = CDN / "launcher" / "tibiaclient-web-current"
    dest_dir.mkdir(parents=True, exist_ok=True)
    files = []
    for name in ("tibia.js", "tibia.wasm", "qtloader.js", "tibia.data"):
        src = src_dir / name
        if not src.is_file() and name == "qtloader.js":
            src = src_dir.parent / name
        if not src.is_file():
            if name == "tibia.data":
                continue
            return fail(f"missing {name} in {src_dir}")
        place(src, dest_dir / name)
        files.append(_cip_file_record(dest_dir, name, src.read_bytes()))
    for name in ("tibia.html", "qtlogo.svg"):
        src = src_dir / name
        if src.is_file():
            place(src, dest_dir / name)
    stamp = _cip_stamp(dest_dir / "tibia.wasm")
    pkg = {
        "version": stamp,
        "files": files,
        "executable": "tibia.js",
        "creation_time": 0,
        "generation": "second",
    }
    (dest_dir / "package.json").write_text(json.dumps(pkg, indent=2) + "\n")
    (dest_dir / "package.json.version").write_text(stamp + "\n")
    # Keep tip pack when Soft wasm rebuild overwrites floating tip files.
    _resync_tip_pack(src_dir, dest_dir)
    print(f"[dev-cdn] tibiaclient-web -> {dest_dir} ({stamp})")
    return 0


def _resync_tip_pack(src_dir: Path, dest_dir: Path) -> None:
    """Copy mods/<id> from the wasm build tip onto the CDN floating tip if present."""
    build_root = src_dir
    # bin/ vs flat layout
    for cand in (src_dir, src_dir.parent, OTC / "build" / "tibia-wasm"):
        mods = cand / "mods"
        if mods.is_dir():
            build_root = cand
            break
    else:
        return
    packs = [p for p in (build_root / "mods").iterdir() if p.is_dir() and (p / "mod.json").is_file()]
    if not packs:
        return
    # Single tip pack slot — prefer idleot, else highest-priority name.
    pack = next((p for p in packs if p.name == "idleot"), packs[0])
    out = dest_dir / "mods" / pack.name
    if out.exists():
        shutil.rmtree(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(pack, out)
    # Linked tip: never ship SIDE_MODULE leftovers onto the CDN floating tip.
    for junk in ("native", "lib"):
        junk_path = out / junk
        if junk_path.exists():
            shutil.rmtree(junk_path)
    mod_json = out / "mod.json"
    if mod_json.is_file():
        try:
            doc = json.loads(mod_json.read_text())
        except (OSError, ValueError):
            doc = None
        if isinstance(doc, dict) and "native" in doc:
            doc.pop("native", None)
            mod_json.write_text(json.dumps(doc, indent=2) + "\n")
    files_list = [
        str(p.relative_to(out)).replace("\\", "/")
        for p in sorted(out.rglob("*"))
        if p.is_file() and not str(p.relative_to(out)).startswith(("native/", "lib/"))
    ]
    (dest_dir / "mods" / f"{pack.name}.files").write_text(
        "\n".join(files_list) + ("\n" if files_list else "")
    )
    print(f"[dev-cdn] tip pack -> {out}")


def launcher() -> int:
    """Hot-swap /play's Qt launcher WASM under launcher/launcher-web-current."""
    if not cdn_ready():
        return 0
    src_dir = Path(os.environ.get("TIBIA_WASM_DIR", OTC / "build" / "tibia-wasm"))
    # Prefer the real binary dir (qt may nest under launcher/src/).
    candidates = [
        src_dir,
        src_dir / "src" / "tibia" / "launcher",
    ]
    launch_src = next((d for d in candidates if (d / "launcher.wasm").is_file()), None)
    if launch_src is None:
        return fail(f"no launcher.wasm under {src_dir} — run make browser-build")
    dest_dir = CDN / "launcher" / "launcher-web-current"
    dest_dir.mkdir(parents=True, exist_ok=True)
    files = []
    for name in ("launcher.js", "launcher.wasm", "qtloader.js"):
        src = launch_src / name
        if not src.is_file() and name == "qtloader.js":
            src = src_dir / name
        if not src.is_file():
            return fail(f"missing {name} in {launch_src}")
        place(src, dest_dir / name)
        files.append(_cip_file_record(dest_dir, name, src.read_bytes()))
    for name in ("launcher.html", "qtlogo.svg"):
        src = launch_src / name
        if src.is_file():
            place(src, dest_dir / name)
    stamp = _cip_stamp(dest_dir / "launcher.wasm")
    pkg = {
        "version": stamp,
        "executable": "launcher",
        "files": files,
    }
    (dest_dir / "package.json").write_text(json.dumps(pkg, indent=2) + "\n")
    (dest_dir / "package.json.version").write_text(stamp + "\n")
    # /play stage backdrop (same files CIP packs under /images/launcher/).
    chrome = OTC / "src" / "tibia" / "launcher" / "images" / "launcher"
    tip_img = CDN / "images" / "launcher"
    if chrome.is_dir():
        tip_img.mkdir(parents=True, exist_ok=True)
        for path in chrome.iterdir():
            if path.is_file() and path.suffix.lower() in {".jpg", ".jpeg", ".png", ".webp"}:
                place(path, tip_img / path.name)
    # Mirror version URL used by tibiametadata for the web launcher build.
    print(f"[dev-cdn] launcher-web -> {dest_dir} ({stamp})")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--desktop", action="store_true")
    group.add_argument("--web", action="store_true")
    group.add_argument("--launcher", action="store_true")
    args = ap.parse_args()
    if args.desktop:
        return desktop()
    if args.launcher:
        return launcher()
    return wasm()


if __name__ == "__main__":
    raise SystemExit(main())
