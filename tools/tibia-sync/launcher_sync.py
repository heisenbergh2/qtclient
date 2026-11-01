#!/usr/bin/env python3
"""Track CIP launcher and client extras that tibia-sync's QML vendor does not cover.

`make tibia-sync` vendors QML, the moc contract, protocol, and strings. The
launcher is a separate binary, and hints (plus their images) live on
static.tibia.com. This script fingerprints both, refreshes the hint feed and
its images, and writes a drift report for everything that still has to be
ported by hand (stylesheet, chrome art, C++).

  launcher_sync.py --check
  launcher_sync.py --launcher /Applications/Tibia.app --client <packages/Tibia.app> --apply

--check (CI, no CIP app required) fails when hints.json names an image that
is not in data/images/news, or when the saved fingerprint does not match
those files.

--apply downloads hints.json, downloads new or changed images, and rewrites
image URLs onto /images/news/. It also vendors the launcher stylesheet and
chrome from the CIP launcher binary into upstream/chrome/. It does not
overwrite the product's launcher background or the two logos. A changed launcher
binary, stylesheet, resource name, or client marker is reported in
upstream/DRIFT.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
# Monorepo umbrella copy uses repo root; in-tree qtclient copy uses qtclient root.
_OTC = ROOT / "apps/qtclient" if (ROOT / "apps/qtclient" / "client/tibia").is_dir() else ROOT
DATA = _OTC / "data"
HINTS = DATA / "hints.json"
NEWS = DATA / "images/news"
UPSTREAM = _OTC / "launcher/upstream"
FINGERPRINT = UPSTREAM / "FINGERPRINT.json"
DRIFT = UPSTREAM / "DRIFT.md"
CHROME = UPSTREAM  # chrome tree is launcher/upstream itself
OURS_DIR = _OTC / "launcher/mods/qtclient/images/launcher"

DEFAULT_LAUNCHER = Path("/Applications/Tibia.app")
DEFAULT_CLIENT = Path.home() / "Library/Application Support/CipSoft GmbH/Tibia/packages/Tibia.app"
HINTS_URL = "https://static.tibia.com/hints.json"

# product replacements of CIP art. A sync must not copy CIP pixels over these.
OURS = {"BackgroundImage.jpg", "TibiaLogo_Classic.png", "CipSoftLogo.png"}

MARKERS = (
    "hintsUrl=",
    "--patch",
    "--show-ui",
    "launcher_start_pointless_due_to_files_uptodate",
    "launcher_start_failed",
    "parseHints",
    "hintAreaTitle",
    ":/images/launcher/",
)


def log(msg: str) -> None:
    print(f"[launcher-sync] {msg}", flush=True)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def mach_binary(app: Path, names: tuple[str, ...]) -> Path | None:
    for name in names:
        for candidate in (app / "Contents/MacOS" / name, app / "MacOS" / name, app):
            if candidate.is_file():
                return candidate
    return None


def cstrings(data: bytes, minimum: int = 6) -> list[str]:
    out: list[str] = []
    start = None
    for i, b in enumerate(data):
        if 32 <= b < 127:
            if start is None:
                start = i
        elif start is not None:
            if i - start >= minimum:
                out.append(data[start:i].decode("ascii", "ignore"))
            start = None
    return out


def resource_names(strings: list[str]) -> list[str]:
    names: set[str] = set()
    for s in strings:
        for match in re.findall(r":/images/launcher/[A-Za-z0-9_./-]+", s):
            names.add(match)
    return sorted(names)


def qss_blob(strings: list[str]) -> str:
    parts = [s for s in strings if ":/images/launcher/" in s and len(s) > 40]
    return "\n".join(parts)


def text_blobs(data: bytes, minimum: int = 40) -> list[str]:
    """ASCII strings that keep tabs and newlines, so a QSS blob stays whole."""
    out: list[str] = []
    start = None
    for i, b in enumerate(data):
        if 9 <= b <= 13 or 32 <= b < 127:
            if start is None:
                start = i
        elif start is not None:
            if i - start >= minimum:
                out.append(data[start:i].decode("ascii", "ignore"))
            start = None
    return out


STYLE_RULES = (
    ("window.qss", ("QMainWindow", "BackgroundImage.jpg")),
    ("hint-group.qss", ("hint-background.png", "QGroupBox")),
    ("package-group.qss", ("clientpackage-background.png", "QGroupBox")),
    ("progress.qss", ("QProgressBar", "ProgressBar_BG.png")),
    ("checkbox.qss", ("QCheckBox", "checkbox-unchecked.png")),
    ("textbrowser.qss", ("QTextBrowser", "scrollbar-texture-vertical.png")),
    ("textedit.qss", ("QTextEdit", "background-color: transparent")),
    ("label-11.qss", ("QLabel", "font-size: 11px", "#909090")),
    ("label-13.qss", ("QLabel", "font-size: 13px", "#bfbfbf")),
    ("textbrowser-links.qss", ("text-decoration", "color: #c0c0c0")),
)


def extract_styles(binary: Path) -> dict[str, str]:
    found: dict[str, str] = {}
    for blob in text_blobs(binary.read_bytes()):
        text = blob.strip()
        if not text.startswith("Q") and not text.startswith("a {"):
            continue
        for name, needles in STYLE_RULES:
            if name in found:
                continue
            if all(needle in text for needle in needles):
                found[name] = text + "\n"
    missing = [name for name, _ in STYLE_RULES if name not in found]
    if missing:
        raise SystemExit(f"launcher stylesheet blobs missing: {', '.join(missing)}")
    return found


def vendor_chrome(launcher: Path, styles: dict[str, str]) -> list[str]:
    """Copy CIP chrome and styles. the product's three images stay in images/launcher."""
    import subprocess
    import tempfile

    binary = mach_binary(launcher, ("Tibia",))
    if binary is None:
        raise SystemExit(f"no launcher binary under {launcher}")
    styles_dir = CHROME / "styles"
    styles_dir.mkdir(parents=True, exist_ok=True)
    for name, text in styles.items():
        dest = styles_dir / name
        if not dest.is_file() or dest.read_text() != text:
            dest.write_text(text)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(
            [sys.executable, str(ROOT / "tools/re/qt_rcc.py"), "--carve", str(binary), tmp],
            check=True,
        )
        carved = next(Path(tmp).glob("root_*/images/launcher"))
        image_dir = CHROME / "images/launcher"
        image_dir.mkdir(parents=True, exist_ok=True)
        names: list[str] = []
        for src in sorted(carved.iterdir()):
            if not src.is_file() or src.name in OURS:
                continue
            dest = image_dir / src.name
            data = src.read_bytes()
            if not dest.is_file() or dest.read_bytes() != data:
                dest.write_bytes(data)
            names.append(src.name)
        eula_src = next(Path(tmp).glob("root_*/documents/tibia-eula.md"))
        eula_dest = CHROME / "documents/tibia-eula.md"
        eula_dest.parent.mkdir(parents=True, exist_ok=True)
        eula = eula_src.read_bytes()
        if not eula_dest.is_file() or eula_dest.read_bytes() != eula:
            eula_dest.write_bytes(eula)
    # images/launcher keeps only the product replacements.
    if OURS_DIR.is_dir():
        for path in OURS_DIR.iterdir():
            if path.is_file() and path.name not in OURS:
                path.unlink()
    return names


def markers(strings: list[str]) -> list[str]:
    found: set[str] = set()
    for s in strings:
        for marker in MARKERS:
            if marker in s:
                found.add(marker)
    return sorted(found)


def image_refs(hints: list) -> list[str]:
    names: set[str] = set()
    for hint in hints:
        for src in re.findall(r'src="([^"]+)"', hint.get("richText", "")):
            names.add(src.rstrip("/").split("/")[-1])
    return sorted(names)


def rewrite_hints(hints: list) -> list:
    for hint in hints:
        text = hint.get("richText") or ""
        text = text.replace("https://static.tibia.com/images/news/", "/images/news/")
        text = text.replace("http://static.tibia.com/images/news/", "/images/news/")
        hint["richText"] = text
    return hints


def fetch(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=60) as response:
        return response.read()


def snapshot(launcher: Path | None, client: Path | None) -> dict:
    snap: dict = {"launcher_resources": [], "launcher_qss_sha256": "", "markers": []}
    if launcher:
        binary = mach_binary(launcher, ("Tibia",))
        if binary is None:
            raise SystemExit(f"no launcher binary under {launcher}")
        data = binary.read_bytes()
        strings = cstrings(data)
        snap["launcher_sha256"] = sha256_bytes(data)
        snap["launcher_resources"] = resource_names(strings)
        snap["launcher_qss_sha256"] = sha256_bytes(qss_blob(strings).encode())
        snap["markers"] = markers(strings)
    if client:
        binary = mach_binary(client, ("client",))
        if binary is None:
            raise SystemExit(f"no client binary under {client}")
        data = binary.read_bytes()
        strings = cstrings(data)
        snap["client_sha256"] = sha256_bytes(data)
        snap["markers"] = sorted(set(snap["markers"]) | set(markers(strings)))
    return snap


def load_fingerprint() -> dict:
    if not FINGERPRINT.is_file():
        return {}
    return json.loads(FINGERPRINT.read_text())


def check_tree() -> int:
    """Fail when the vendored hint feed and its images disagree."""
    bad = 0
    if not HINTS.is_file():
        log(f"missing {HINTS}")
        return 1
    hints = json.loads(HINTS.read_text())
    if not isinstance(hints, list):
        log("hints.json is not a CIP list of {title, richText}")
        return 1
    refs = image_refs(hints)
    missing = [name for name in refs if not (NEWS / name).is_file()]
    for name in missing:
        log(f"hint image missing: {name}")
        bad += 1
    finger = load_fingerprint()
    if finger.get("hint_images") and finger["hint_images"] != refs:
        log("FINGERPRINT.json hint image list does not match hints.json; re-run launcher_sync.py --apply")
        bad += 1
    if not finger:
        log(f"no {FINGERPRINT}; run launcher_sync.py --apply against the CIP launcher and client")
        bad += 1
    styles = finger.get("launcher_styles") or {}
    if not styles:
        log("FINGERPRINT.json has no launcher_styles; run launcher_sync.py --apply")
        bad += 1
    for name, digest in styles.items():
        path = CHROME / "styles" / name
        if not path.is_file() or sha256_file(path) != digest:
            log(f"vendored stylesheet drift: {name}")
            bad += 1
    chrome = finger.get("launcher_chrome") or []
    if not chrome:
        log("FINGERPRINT.json has no launcher_chrome; run launcher_sync.py --apply")
        bad += 1
    for name in chrome:
        if not (CHROME / "images/launcher" / name).is_file():
            log(f"vendored chrome missing: {name}")
            bad += 1
    eula = CHROME / "documents/tibia-eula.md"
    if finger.get("launcher_eula_sha256") and (not eula.is_file() or sha256_file(eula) != finger["launcher_eula_sha256"]):
        log("vendored tibia-eula.md drift")
        bad += 1
    extra = sorted(p.name for p in NEWS.glob("*") if p.is_file() and p.name not in refs) if NEWS.is_dir() else []
    if extra:
        log(f"hint images not referenced ({len(extra)}): {', '.join(extra[:8])}")
    log("hints are complete" if not bad else f"hints drift: {bad}")
    return 1 if bad else 0


def diff_list(label: str, old: list, new: list) -> list[str]:
    removed = sorted(set(old) - set(new))
    added = sorted(set(new) - set(old))
    lines = []
    if added:
        lines.append(f"{label} added:")
        lines.extend(f"- {item}" for item in added)
    if removed:
        lines.append(f"{label} removed:")
        lines.extend(f"- {item}" for item in removed)
    return lines


def apply(launcher: Path | None, client: Path | None) -> int:
    previous = load_fingerprint()
    lines = ["# CIP launcher / hints drift", ""]

    try:
        raw = json.loads(fetch(HINTS_URL).decode())
    except Exception as exc:
        log(f"could not download hints.json ({exc}); keeping the vendored copy")
        raw = json.loads(HINTS.read_text()) if HINTS.is_file() else []
    if not isinstance(raw, list):
        raise SystemExit("hints.json is not a list")
    hints = rewrite_hints(raw)
    encoded = json.dumps(hints, indent=1) + "\n"
    hints_sha = sha256_bytes(encoded.encode())
    if not HINTS.is_file() or sha256_file(HINTS) != hints_sha:
        HINTS.write_text(encoded)
        log(f"updated {HINTS} ({len(hints)} hints)")
    else:
        log("hints.json unchanged")

    refs = image_refs(hints)
    NEWS.mkdir(parents=True, exist_ok=True)
    downloaded = 0
    for name in refs:
        dest = NEWS / name
        url = f"https://static.tibia.com/images/news/{name}"
        if dest.is_file():
            continue
        try:
            dest.write_bytes(fetch(url))
            downloaded += 1
        except Exception as exc:
            log(f"image failed {name}: {exc}")
            lines.append(f"- failed to download {url} ({exc})")
    if downloaded:
        log(f"downloaded {downloaded} hint images")

    snap = snapshot(launcher, client)
    snap["hints_sha256"] = hints_sha
    snap["hint_images"] = refs
    if launcher:
        binary = mach_binary(launcher, ("Tibia",))
        if binary is None:
            raise SystemExit(f"no launcher binary under {launcher}")
        styles = extract_styles(binary)
        chrome = vendor_chrome(launcher, styles)
        snap["launcher_styles"] = {name: sha256_bytes(text.encode()) for name, text in sorted(styles.items())}
        snap["launcher_chrome"] = chrome
        eula = CHROME / "documents/tibia-eula.md"
        snap["launcher_eula_sha256"] = sha256_file(eula) if eula.is_file() else ""
        log(f"vendored {len(styles)} styles and {len(chrome)} chrome images")
    else:
        snap["launcher_styles"] = previous.get("launcher_styles", {})
        snap["launcher_chrome"] = previous.get("launcher_chrome", [])
        snap["launcher_eula_sha256"] = previous.get("launcher_eula_sha256", "")

    lines.extend(diff_list("Hint images", previous.get("hint_images", []), refs))
    lines.extend(diff_list("Launcher resources", previous.get("launcher_resources", []), snap.get("launcher_resources", [])))
    lines.extend(diff_list("Client/launcher markers", previous.get("markers", []), snap.get("markers", [])))
    if previous.get("launcher_qss_sha256") and previous.get("launcher_qss_sha256") != snap.get("launcher_qss_sha256"):
        lines.append("- Launcher stylesheet changed. Compare the new `:/images/launcher/` rules with `launcher/mods/qtclient/images/launcher/cip.qss`.")
        lines.append(f"  Do not replace {', '.join(sorted(OURS))}; those are product art.")
    if previous.get("launcher_sha256") and previous.get("launcher_sha256") != snap.get("launcher_sha256"):
        lines.append("- Launcher binary changed. Port new C++ behaviour from the markers and resource list; chrome pixels are not auto-copied.")
    if previous.get("client_sha256") and previous.get("client_sha256") != snap.get("client_sha256"):
        lines.append("- Client binary changed. `make tibia-sync` covers QML and protocol. Port hint/launcher behaviour listed under markers.")
    if not lines[2:]:
        lines.append("No launcher, hint, or marker drift.")

    UPSTREAM.mkdir(parents=True, exist_ok=True)
    DRIFT.write_text("\n".join(lines) + "\n")
    FINGERPRINT.write_text(json.dumps(snap, indent=1) + "\n")
    log(f"wrote {DRIFT}")
    return 0 if not any(line.startswith("- failed") for line in lines) else 1


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="verify vendored hints and images (no CIP app required)")
    ap.add_argument("--apply", action="store_true", help="refresh hints and images, then write the drift report")
    ap.add_argument("--launcher", type=Path, help="CIP launcher .app (default /Applications/Tibia.app when it exists)")
    ap.add_argument("--client", type=Path, help="CIP client .app (MacOS/client)")
    args = ap.parse_args()

    if args.check and not args.apply:
        raise SystemExit(check_tree())

    launcher = args.launcher
    if launcher is None and DEFAULT_LAUNCHER.is_dir():
        launcher = DEFAULT_LAUNCHER
    client = args.client
    if client is None and DEFAULT_CLIENT.is_dir():
        client = DEFAULT_CLIENT
    if args.apply or launcher or client:
        apply(launcher, client)
        raise SystemExit(check_tree())
    ap.error("pass --check or --apply")


if __name__ == "__main__":
    main()
