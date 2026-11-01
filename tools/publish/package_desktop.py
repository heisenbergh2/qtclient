#!/usr/bin/env python3
"""Store the qtclient desktop engine and runtime for the launcher (no things).

The qtclient launcher installs and updates the game; this writes the desktop
slice of the CDN bundle it syncs from (see qtclient/release, content_store.py):
  <out>/content/<aa>/<sha256>                 content-addressed blobs
  <out>/packs/<aa>/<sha256>.zip               zip packs of those blobs
  <out>/.qtclient/v3/engine-<platform>.json     signed engine (exe+DLLs / QtClient.app / binary)
  <out>/.qtclient/v3/content-desktop.json       runtime tree incl. release_env.lua (once, on Linux)
The launcher runs the engine with the content dir as cwd (discoverWorkDir
checks the current dir first) and TIBIA_LAUNCHER=1. Things come from the
content-things list (publish_assets.sh).

Ships the Qt/QML tibia app (client/tibia) under QtClient.app / client.exe / client.
The engine carries the Qt runtime; content is data/setup.otml (+ typing icon),
config.ini and release_env.lua. --bin is the build dir holding client.app /
client.exe / client; --engine-bundle DIR stages the deployed engine;
--engine-dir DIR stores it. Qt from QT_PREFIX or tools/qt/qt-prefix.sh.

macOS: --engine-bundle DIR copies the branded bundle to DIR/QtClient.app. Sign
that copy, then pass it as --engine-app.

--site-url / --asset-base (or TIBIA_SITE_URL / TIBIA_ASSET_BASE) write
release_env.lua, including TIBIA_REVISION, so a release build logs in to that
site.

Usage:
  package_desktop.py --platform linux-x64 --bin build/linux-release/bin --out dist/slices/desktop \
      --engine-files --content-files
  package_desktop.py --platform windows-x64 --bin build/windows-release/bin --out dist/slices/desktop --engine-files
  package_desktop.py --platform macos-arm64 --bin build/macos-release/bin --engine-bundle build/engine
  package_desktop.py --platform macos-arm64 --out dist/slices/desktop --engine-files --engine-app build/engine/QtClient.app
  package_desktop.py --platform windows-x64 --out dist/slices/desktop --engine-files --engine-dir build/engine
Repeat with the same --out/--revision to add platforms to one revision.
"""
from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))
import content_store  # noqa: E402

OTC = Path(__file__).resolve().parents[2]
PLATFORMS = ("linux-x64", "windows-x64", "macos-arm64")
# Stock host brands as qtclient; product release (IdleOT pack) sets QTCLIENT_APP_NAME=IdleOT.
APP_NAME = os.environ.get("QTCLIENT_APP_NAME", "qtclient")
# CipSoft in-package Play basename (Contents/MacOS/client).
GAME_EXE = "client"
BUNDLE_ID = os.environ.get("QTCLIENT_BUNDLE_ID", "com.qtclient.client")
# CDN list dir: stock .qtclient; IdleOT assemble expects .idleot (QTCLIENT_STORE_META=.idleot).
STORE_META = os.environ.get("QTCLIENT_STORE_META", ".qtclient")
# The tibia app compiles its QML (CIP's and pack) and the Qt pin into the engine.
TIBIA_ENGINE_PATHS = (
    "src", "cmake", "CMakeLists.txt", "CMakePresets.json",
    "client/tibia", "client/upstream", "tools/qt/qt-version.sh",
)
# gamecore finds its work dir by data/setup.otml (whose typing-icon it loads) and reads
# config.ini; things come from content-things.
TIBIA_RUNTIME_FILES = ("data/setup.otml", "data/images/game/console/typing.png", "config.ini")
# qtclient: OUTPUT_NAME client → client.app / client.exe / client; brand to APP_NAME.app.
# qtclient cmake OUTPUT_NAME under build/<preset>/bin (TOGGLE_BIN_FOLDER=ON).
TIBIA_BUILD = {
    "macos-arm64": "client.app",
    "windows-x64": "client.exe",
    "linux-x64": "client",
}
# Linux: the Qt plugin directories the app may load (platform, GL, images, TLS).
LINUX_QT_PLUGINS = ("platforms", "xcbglintegrations", "wayland-shell-integration", "wayland-graphics-integration-client",
                    "wayland-decoration-client", "platforminputcontexts", "imageformats", "iconengines", "tls",
                    "networkinformation")


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(OTC), *args], capture_output=True, text=True, check=True).stdout.strip()


def source_commit() -> str:
    return os.environ.get("SOURCE_COMMIT") or git("rev-parse", "HEAD")


def cip_client_version() -> str:
    """CIP launcher stamp: `<cip_version>.<6 hex>` (e.g. 15.33.c9e45e)."""
    cip = "15.33"
    version_json = OTC / "cip" / "VERSION.json"
    if version_json.is_file():
        try:
            cip = json.loads(version_json.read_text()).get("cip_version") or cip
        except (OSError, ValueError):
            pass
    return f"{cip}.{source_commit()[:6]}"


def default_revision() -> str:
    # Content-store list paths still use a dated stamp; CIP package.json.version
    # (shown under Play) uses cip_client_version() via emit_cip.
    return f"{dt.datetime.now(dt.timezone.utc):%Y%m%d}-{source_commit()[:7]}"


def engine_id() -> str:
    if os.environ.get("TIBIA_ENGINE_ID"):
        return os.environ["TIBIA_ENGINE_ID"]
    try:
        trees = [git("rev-parse", f"HEAD:{p}") for p in TIBIA_ENGINE_PATHS if (OTC / p).exists()]
    except (OSError, subprocess.CalledProcessError):
        trees = [source_commit()]
    trees.append("tibia")
    return hashlib.sha1("\n".join(trees).encode()).hexdigest()[:16]


def macos_app(bin_dir: Path) -> Path:
    named = bin_dir / f"{APP_NAME}.app"
    if named.is_dir():
        return named
    app = next(iter(sorted(bin_dir.glob("*.app"))), None)
    if app is None:
        raise SystemExit(f"[desktop] no .app bundle in {bin_dir}")
    return app


def release_env_entry(
    site_url: str,
    asset_base: str,
    revision: str,
    tmpdir: Path,
    *,
    login_profile: str = "mod",
) -> tuple[Path, str]:
    """Write release_env.lua (revision + optional origin/asset).

    Login paths live in pack mod.json `urls` (demo myaac / IdleOT CIP).
    login_profile legacy values myaac|cip still bake paths as a fallback when
    the tip has no mod; prefer `mod` (default) — origin only for domain remap.
    """
    url = urlsplit(site_url)
    if url.scheme not in ("http", "https") or not url.hostname:
        raise SystemExit(f"[desktop] invalid --site-url {site_url!r}")
    values = {
        "TIBIA_REVISION": revision,
        "QTCLIENT_REVISION": revision,
    }
    # Origin for LauncherEnv when mod.json omits loginBase (or as lower-precedence fallback).
    if login_profile != "mod" or site_url:
        values["QTCLIENT_LOGIN_SCHEME"] = url.scheme
        values["QTCLIENT_LOGIN_HOST"] = url.hostname
        if url.port:
            values["QTCLIENT_LOGIN_PORT"] = str(url.port)
        elif login_profile == "cip" and url.hostname in ("127.0.0.1", "localhost"):
            values["QTCLIENT_LOGIN_PORT"] = "5173"

    if login_profile == "cip":
        values["QTCLIENT_LOGIN_PATH"] = "/clientservices/loginservice.php"
        values["QTCLIENT_CLIENTSERVICES_PATH"] = "/clientservices/clientservices.php"
        values["QTCLIENT_CREATE_ACCOUNT_PATH"] = "/clientservices/accountcreationclientservices.php"
    elif login_profile == "myaac":
        values["QTCLIENT_LOGIN_PATH"] = "/login.php"
        values["QTCLIENT_CLIENTSERVICES_PATH"] = "/login.php"
        values["QTCLIENT_CREATE_ACCOUNT_PATH"] = "/login.php"

    if asset_base:
        values["QTCLIENT_ASSET_BASE"] = asset_base.rstrip("/")
        values["TIBIA_ASSET_BASE"] = asset_base.rstrip("/")
    body = "".join(f"    {k} = {json.dumps(v)},\n" for k, v in values.items())
    path = tmpdir / "release_env.lua"
    path.write_text(f"return {{\n{body}}}\n")
    return path, "release_env.lua"


def brand_icns() -> Path | None:
    """Product pack ships resources/<App>.icns under client/mods/<pack>/."""
    names = (APP_NAME, APP_NAME.lower(), "IdleOT", "qtclient")
    packs = (APP_NAME.lower(), "idleot", "qtclient")
    for pack in packs:
        for name in names:
            path = OTC / "client" / "mods" / pack / "resources" / f"{name}.icns"
            if path.is_file():
                return path
    return None


def brand_bundle(bin_dir: Path) -> Path:
    """Rename the build's .app to APP_NAME.app; CipSoft Play binary stays `client`."""
    app = macos_app(bin_dir)
    target = bin_dir / f"{APP_NAME}.app"
    if app != target:
        shutil.rmtree(target, ignore_errors=True)
        app.rename(target)
        app = target
    contents = app / "Contents"
    plist_path = contents / "Info.plist"
    with open(plist_path, "rb") as fh:
        plist = plistlib.load(fh)
    old_exe = plist.get("CFBundleExecutable", GAME_EXE)
    if old_exe != GAME_EXE and (contents / "MacOS" / old_exe).is_file():
        (contents / "MacOS" / old_exe).rename(contents / "MacOS" / GAME_EXE)
    old_icon = plist.get("CFBundleIconFile", "")
    plist.update(
        CFBundleExecutable=GAME_EXE,
        CFBundleName=APP_NAME,
        CFBundleDisplayName=APP_NAME,
        CFBundleIdentifier=BUNDLE_ID,
        CFBundleIconFile=APP_NAME,
    )
    with open(plist_path, "wb") as fh:
        plistlib.dump(plist, fh)
    resources = contents / "Resources"
    resources.mkdir(parents=True, exist_ok=True)
    if old_icon and old_icon != APP_NAME:
        (resources / f"{Path(old_icon).stem}.icns").unlink(missing_ok=True)
    icns = brand_icns()
    if icns is not None:
        shutil.copy2(icns, resources / f"{APP_NAME}.icns")
    return app


def tibia_runtime_entries() -> list[tuple[Path, str]]:
    return [(OTC / name, name) for name in TIBIA_RUNTIME_FILES if (OTC / name).is_file()]


def qt_prefix() -> Path:
    prefix = os.environ.get("QT_PREFIX") or subprocess.run(
        ["bash", str(OTC / "tools" / "qt" / "qt-prefix.sh")], capture_output=True, text=True, check=True).stdout.strip()
    if not prefix or not Path(prefix).is_dir():
        raise SystemExit("[desktop] Qt not found: set QT_PREFIX or run make qt")
    return Path(prefix)


def tibia_build(bin_dir: Path, platform: str) -> Path:
    """qtclient client at <bin_dir>/client[.app|/exe] (TOGGLE_BIN_FOLDER=ON)."""
    name = TIBIA_BUILD[platform]
    path = bin_dir / name
    want_dir = name.endswith(".app")
    if path.exists() and (path.is_dir() if want_dir else path.is_file()):
        return path
    raise SystemExit(
        f"[desktop] missing {path} (build: make qtclient client build)"
    )


def qml_dirs() -> list[str]:
    candidates = (
        OTC / "client" / "upstream" / "qml",
        OTC / "client" / "qml",
        OTC / "client" / "overrides" / "qml",
        OTC / "cip" / "qml",
        OTC / "qml",
    )
    return [str(p) for p in candidates if p.is_dir()]


@contextlib.contextmanager
def without_qt_sqldrivers(qt: Path):
    """macdeployqt copies every SQL plugin and then otool-walks ODBC, Mimer, and
    Postgres, which are not part of this client. Hide that plugin dir for the call."""
    sql = qt / "plugins" / "sqldrivers"
    hidden = sql.with_name("sqldrivers.qtclient-hidden")
    moved = False
    if sql.is_dir():
        if hidden.exists():
            shutil.rmtree(hidden)
        sql.rename(hidden)
        moved = True
    try:
        yield
    finally:
        if moved and hidden.exists() and not sql.exists():
            hidden.rename(sql)


def tibia_macos_bundle(build: Path, dest: Path, qt: Path) -> Path:
    shutil.rmtree(dest / f"{APP_NAME}.app", ignore_errors=True)
    shutil.rmtree(dest / build.name, ignore_errors=True)
    dest.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ditto", str(build), str(dest / build.name)], check=True)
    deploy = [str(qt / "bin" / "macdeployqt"), str(dest / build.name), "-always-overwrite", "-verbose=1",
              "-appstore-compliant", f"-qmlimport={qt / 'qml'}", *(f"-qmldir={d}" for d in qml_dirs())]
    with without_qt_sqldrivers(qt):
        subprocess.run(deploy, check=True)
    shutil.rmtree(dest / build.name / "Contents" / "PlugIns" / "sqldrivers", ignore_errors=True)
    app = brand_bundle(dest)
    # arm64 refuses unsigned code; release signing re-signs over this ad-hoc seal.
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
    return app


def tibia_windows_dir(build: Path, dest: Path, qt: Path) -> Path:
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    # CipSoft Play basename is client.exe (matches write_tibia_engine_files entry).
    exe = dest / f"{GAME_EXE}.exe"
    shutil.copy2(build, exe)
    for dll in build.parent.glob("*.dll"):
        shutil.copy2(dll, dest / dll.name)
    deploy = [str(qt / "bin" / "windeployqt.exe"), "--release", "--no-translations", "--no-system-d3d-compiler",
              "--no-opengl-sw", "--qmlimport", str(qt / "qml"), "--dir", str(dest)]
    for d in qml_dirs():
        deploy += ["--qmldir", d]
    subprocess.run([*deploy, str(exe)], check=True)
    return dest


def ldd_qt_libs(elf: Path, qt_lib: Path) -> dict[str, Path]:
    """The Qt libraries elf loads (recursively, as ldd resolves them), by soname."""
    out = subprocess.run(["ldd", str(elf)], capture_output=True, text=True).stdout
    libs: dict[str, Path] = {}
    for line in out.splitlines():
        parts = line.split("=>")
        if len(parts) != 2:
            continue
        name, target = parts[0].strip(), parts[1].split("(")[0].strip()
        if target and Path(target).resolve().is_relative_to(qt_lib.resolve()):
            libs[name] = Path(target)
    return libs


def set_rpath(elf: Path, rpath: str) -> None:
    if shutil.which("patchelf") is None:
        raise SystemExit("[desktop] linux tibia needs patchelf")
    subprocess.run(["patchelf", "--set-rpath", rpath, str(elf)], check=True)


def tibia_linux_dir(build: Path, dest: Path, qt: Path) -> Path:
    """dest/client with Qt beside it: lib/, plugins/, qml/ and qt.conf (what linuxdeploy's Qt
    plugin would bundle; system libraries such as libxcb and libGL stay the host's)."""
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    # CipSoft Play basename is client (matches write_tibia_engine_files entry).
    binary = dest / GAME_EXE
    shutil.copy2(build, binary)
    binary.chmod(0o755)

    elves = [binary]
    for category in LINUX_QT_PLUGINS:
        src = qt / "plugins" / category
        if src.is_dir():
            shutil.copytree(src, dest / "plugins" / category, symlinks=False)
            elves += sorted((dest / "plugins" / category).glob("*.so"))
    scan = subprocess.run([str(qt / "libexec" / "qmlimportscanner"), "-importPath", str(qt / "qml"),
                           *(arg for d in qml_dirs() for arg in ("-rootPath", d))],
                          capture_output=True, text=True, check=True)
    for module in json.loads(scan.stdout):
        path = Path(module.get("path", ""))
        if not module.get("path") or not path.resolve().is_relative_to((qt / "qml").resolve()):
            continue
        target = dest / "qml" / path.resolve().relative_to((qt / "qml").resolve())
        if not target.exists():
            shutil.copytree(path, target, symlinks=False)
    elves += sorted((dest / "qml").rglob("*.so"))

    libs: dict[str, Path] = {}
    for elf in elves:
        libs.update(ldd_qt_libs(elf, qt / "lib"))
    (dest / "lib").mkdir()
    for name, src in sorted(libs.items()):
        shutil.copy2(src.resolve(), dest / "lib" / name)
    (dest / "qt.conf").write_text("[Paths]\nPrefix = .\nLibraries = lib\nPlugins = plugins\nQmlImports = qml\n")

    set_rpath(binary, "$ORIGIN/lib")
    for lib in (dest / "lib").iterdir():
        set_rpath(lib, "$ORIGIN")
    for elf in elves[1:]:
        depth = len(elf.relative_to(dest).parts) - 1
        set_rpath(elf, "$ORIGIN/" + "/".join([".."] * depth) + "/lib")
    return dest


def tibia_engine_bundle(platform: str, bin_dir: Path, dest: Path) -> Path:
    """The tibia app with its Qt runtime, staged in dest (macOS: dest/QtClient.app)."""
    build, qt = tibia_build(bin_dir, platform), qt_prefix()
    if platform == "macos-arm64":
        return tibia_macos_bundle(build, dest, qt)
    if platform == "windows-x64":
        return tibia_windows_dir(build, dest, qt)
    return tibia_linux_dir(build, dest, qt)


def write_tibia_engine_files(out: Path, platform: str, revision: str, engine: str, engine_dir: Path) -> None:
    store = out / "content"
    if platform == "macos-arm64":
        if engine_dir.suffix != ".app" or not (engine_dir / "Contents" / "MacOS" / GAME_EXE).is_file():
            raise SystemExit(
                "[desktop] --engine-app must be QtClient.app with Contents/MacOS/client (CipSoft Play basename)"
            )
        files = content_store.tree_entries(store, engine_dir, f"{APP_NAME}.app", skip_symlinks=True)
        entry_point = f"/{APP_NAME}.app/Contents/MacOS/{GAME_EXE}"
    else:
        files = content_store.tree_entries(store, engine_dir, skip_symlinks=True)
        entry_point = f"/{GAME_EXE}.exe" if platform == "windows-x64" else f"/{GAME_EXE}"
    content_store.write_list(
        out, f"engine-{platform}", revision, engine, files, entry_point,
        dest=out / STORE_META / "v3" / f"engine-{platform}.json",
    )


def write_tibia_content_files(out: Path, revision: str, engine: str, runtime: list[tuple[Path, str]]) -> None:
    """The tibia app reads release_env.lua as plain text, so its content is stored unencrypted."""
    files = {f"/{arc}": content_store.entry(out / "content", src.read_bytes()) for src, arc in runtime}
    content_store.write_list(
        out, "content-desktop", revision, engine, files,
        dest=out / STORE_META / "v3" / "content-desktop.json",
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--platform", choices=PLATFORMS, required=True)
    ap.add_argument("--bin", type=Path, help="directory holding the built engine")
    ap.add_argument("--out", type=Path, default=OTC.parent.parent / "dist" / "slices" / "desktop")
    ap.add_argument("--revision", default=os.environ.get("TIBIA_DESKTOP_REVISION") or None)
    ap.add_argument("--site-url", default=(
                        os.environ.get("TIBIA_SITE_URL")
                        or os.environ.get("IDLEOT_SITE_URL")
                        or ""
                    ),
                    help="the portal origin baked into release_env.lua, e.g. https://www.example.invalid")
    ap.add_argument("--asset-base", default=(
                        os.environ.get("TIBIA_ASSET_BASE")
                        or os.environ.get("IDLEOT_ASSET_BASE")
                        or ""
                    ),
                    help="CDN origin baked into release_env.lua, e.g. https://cdn.example.invalid")
    ap.add_argument(
        "--login-profile",
        choices=("mod", "myaac", "cip"),
        default=os.environ.get("QTCLIENT_LOGIN_PROFILE", "mod"),
        help="release_env.lua: mod (revision/origin only; paths from pack urls) or legacy myaac|cip",
    )
    ap.add_argument("--engine-bundle", type=Path, metavar="DIR",
                    help=f"macOS: copy the branded bundle without runtime to DIR/{APP_NAME}.app (sign it next), then exit")
    ap.add_argument("--engine-files", action="store_true",
                    help=f"store the engine and write {STORE_META}/v3/engine-<platform>.json")
    ap.add_argument("--engine-app", type=Path,
                    help=f"macOS --engine-files: the signed --engine-bundle {APP_NAME}.app")
    ap.add_argument("--engine-dir", type=Path,
                    help="Windows/Linux --engine-files: the --engine-bundle directory")
    ap.add_argument("--content-files", action="store_true",
                    help=f"store the runtime and write {STORE_META}/v3/content-desktop.json")
    args = ap.parse_args()

    return tibia_main(ap, args, args.bin.resolve() if args.bin else None)


def tibia_main(ap: argparse.ArgumentParser, args: argparse.Namespace, bin_dir: Path | None) -> int:
    if args.engine_bundle:
        if bin_dir is None:
            ap.error("--engine-bundle needs --bin")
        staged = tibia_engine_bundle(args.platform, bin_dir, args.engine_bundle.resolve())
        print(f"[desktop] tibia engine bundle -> {staged}")
        return 0
    if not (args.engine_files or args.content_files):
        ap.error("nothing to do: pass --engine-files and/or --content-files")

    out = args.out.resolve()
    revision = args.revision or default_revision()
    engine = engine_id()
    if args.engine_files:
        staged = args.engine_app if args.platform == "macos-arm64" else args.engine_dir
        if staged is None:
            ap.error("--engine-files needs the staged --engine-bundle (--engine-app on macOS, else --engine-dir)")
        write_tibia_engine_files(out, args.platform, revision, engine, staged.resolve())
    if args.content_files:
        with tempfile.TemporaryDirectory() as tmp:
            runtime = tibia_runtime_entries()
            if args.site_url:
                runtime.append(
                    release_env_entry(
                        args.site_url,
                        args.asset_base,
                        revision,
                        Path(tmp),
                        login_profile=args.login_profile,
                    )
                )
            write_tibia_content_files(out, revision, engine, runtime)
    print(f"[desktop] tibia {args.platform} revision={revision} engine={engine} -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
