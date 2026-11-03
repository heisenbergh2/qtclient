#!/usr/bin/env bash
# Build the Qt/QML tibia app (client/tibia: CIP's UI on Qt Quick).
#
#   scripts/lifecycle/build/desktop.sh (or COMP=client|launcher|wasm)            desktop production (embedded QML + qmlcachegen)
#   scripts/lifecycle/build/desktop.sh (or COMP=client|launcher|wasm) --dev      desktop iteration: disk QML, NO_CACHEGEN, hot reload
#   scripts/lifecycle/build/desktop.sh (or COMP=client|launcher|wasm) --debug    desktop Debug (asan presets; still embedded QML)
#   scripts/lifecycle/build/desktop.sh (or COMP=client|launcher|wasm) --wasm     browser: tibia under Qt's emsdk (wasm OS)
#   scripts/lifecycle/build/desktop.sh (or COMP=client|launcher|wasm) --launcher desktop CIP launcher (Qt Widgets) instead of the game
#
# Needs: VCPKG_ROOT, pinned Qt via make qt (WASM=1 for --wasm). C++ deps from vcpkg.json.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OTC="${TIBIA_CLIENT_DIR:-$(cd "$HERE/../../.." && pwd)}"
CACHE_ENV="$(cd "$OTC/../.." && pwd)/scripts/build-cache-env.sh"
if [[ -f "$CACHE_ENV" ]]; then
  # shellcheck disable=SC1090
  source "$CACHE_ENV"
fi
QT_PREFIX_SH="$OTC/tools/qt/qt-prefix.sh"
DEBUG_BUILD=0
DEV_BUILD=0
WASM_BUILD=0
LAUNCHER_BUILD=0

# Named entry: build/desktop.sh (launcher/wasm wrappers pass --launcher/--wasm).
case "${DEV:-}" in 1|true|yes|on) DEV_BUILD=1 ;; esac
case "${DEBUG:-}" in 1|true|yes|on) DEBUG_BUILD=1 ;; esac

for arg in "$@"; do
  case "$arg" in
    --debug) DEBUG_BUILD=1 ;;
    --dev) DEV_BUILD=1 ;;
    --wasm) WASM_BUILD=1 ;;
    --launcher) LAUNCHER_BUILD=1 ;;
    -h|--help)
      sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "[tibia] unknown argument: $arg (try --help)"
      exit 1
      ;;
  esac
done

if [[ "$DEV_BUILD" -eq 1 && "$WASM_BUILD" -eq 1 ]]; then
  echo "[tibia] --dev is desktop-only (disk QML)"
  exit 1
fi

# shellcheck source=../tools/qt/qt-version.sh
source "$OTC/tools/qt/qt-version.sh"
if [[ -n "${QT_ROOT:-}" ]]; then
  QT_ROOT="$(qt_unix_path "$QT_ROOT")"
  export QT_ROOT
fi

command -v cmake >/dev/null || { echo "[tibia] need cmake"; exit 1; }
command -v ninja >/dev/null || { echo "[tibia] need ninja"; exit 1; }
NINJA="$(command -v ninja)"

VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
if [[ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
  echo "[tibia] VCPKG_ROOT missing or incomplete ($VCPKG_ROOT) — install vcpkg and export VCPKG_ROOT"
  exit 1
fi
export VCPKG_ROOT

# Shared binary cache — presets read these via $penv{VCPKG_*}.
MONOREPO="$(cd "$OTC/../.." && pwd)"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-$MONOREPO/.cache/vcpkg/archives}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;files,$VCPKG_DEFAULT_BINARY_CACHE,readwrite}"
export VCPKG_DOWNLOADS="${VCPKG_DOWNLOADS:-$HOME/vcpkg/downloads}"
export CCACHE_DIR="${CCACHE_DIR:-$MONOREPO/.cache/ccache}"
export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$MONOREPO}"
export CCACHE_NOHASHDIR="${CCACHE_NOHASHDIR:-1}"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE" "$VCPKG_DOWNLOADS" "$CCACHE_DIR"

BUILD_TYPE=Release
[[ "$DEBUG_BUILD" -eq 1 ]] && BUILD_TYPE=Debug
# Windows Git Bash may lack sysctl/nproc; never abort the build on that.
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
export CMAKE_BUILD_PARALLEL_LEVEL="$JOBS"

# Portable sha256 (Git Bash Windows often has neither in a stripped MSVC PATH).
_sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$@"
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$@"
  elif command -v openssl >/dev/null 2>&1; then
    # openssl dgst prints "SHA256(file)= hex" — normalize to "hex  file"
    if [[ $# -eq 0 ]]; then
      openssl dgst -sha256 | awk '{print $2}'
    else
      local f
      for f in "$@"; do
        openssl dgst -sha256 "$f" | awk -v f="$f" '{print $2 "  " f}'
      done
    fi
  else
    echo "[tibia] need sha256sum, shasum, or openssl" >&2
    return 127
  fi
}

osx_triplet() {
  case "$(uname -m)" in
    arm64) echo "arm64-osx-static" ;;
    *) echo "x64-osx-static" ;;
  esac
}

_qt_ok() { [[ -n "${1:-}" && -f "$1/lib/cmake/Qt6/Qt6Config.cmake" ]]; }

if [[ "$WASM_BUILD" -eq 1 ]]; then
  QT_WASM="$(bash "$QT_PREFIX_SH" --wasm)"
  # CI: QT_PREFIX from install-qt --wasm, or QT_ROOT for qt-prefix.sh discovery.
  if ! _qt_ok "$QT_WASM" && _qt_ok "${QT_PREFIX:-}"; then
    case "$QT_PREFIX" in *wasm*) QT_WASM="$QT_PREFIX" ;; esac
  fi
  EMSDK_DIR="$(bash "$QT_PREFIX_SH" --emsdk)"
  if ! _qt_ok "$QT_WASM" || [[ -z "$EMSDK_DIR" ]]; then
    echo "[tibia] Qt for WebAssembly not installed — run: make qt WASM=1 (QT_ROOT=${QT_ROOT:-unset})"
    exit 1
  fi
  QT_DESKTOP="$(bash "$QT_PREFIX_SH")"
  if ! _qt_ok "$QT_DESKTOP"; then
    # Host kit is still required to drive wasm cmake (qt.toolchain).
    echo "[tibia] desktop Qt kit missing under QT_ROOT=${QT_ROOT:-unset} (needed for wasm toolchain)"
    exit 1
  fi
  export EMSDK="$EMSDK_DIR"
  # shellcheck disable=SC1091
  source "$EMSDK_DIR/emsdk_env.sh" >/dev/null

  QT_WASM_TOOLCHAIN="$QT_WASM/lib/cmake/Qt6/qt.toolchain.cmake"
  export QT_WASM_TOOLCHAIN
  export VCPKG_CHAINLOAD_TOOLCHAIN_FILE="$QT_WASM_TOOLCHAIN"
  export VCPKG_TARGET_TRIPLET=wasm32-emscripten

  RCC="$(ls "$OTC"/data/things/*/graphics_resources.rcc 2>/dev/null | sort | tail -1 || true)"
  [[ -z "$RCC" ]] && echo "[tibia] WARN: no graphics_resources.rcc (make extract); the UI will have no art"

  BUILD_DIR="$OTC/build/tibia-wasm"
  WASM_INITIAL_MEMORY="${WASM_INITIAL_MEMORY:-536870912}"
  echo "[tibia] wasm qt=$QT_WASM emsdk=$EMSDK_DIR type=$BUILD_TYPE vcpkg=$VCPKG_ROOT"
  echo "[tibia] INITIAL_MEMORY=$WASM_INITIAL_MEMORY"

  mkdir -p "$BUILD_DIR"

  HELPER="$MONOREPO/scripts/cmake-configure-if-needed.sh"
  # Always pass one -D each — empty "${arr[@]}" trips `set -u`.
  # STOCK_HOST=1 → stock CIP tip. Product tip: QTCLIENT_LINKED_PACK=…/client.
  _mods_cmake=-DQTCLIENT_ENABLE_MODS=ON
  if [[ "${STOCK_HOST:-0}" == "1" || "${QTCLIENT_ENABLE_MODS:-}" =~ ^(0|OFF|off|false|FALSE)$ ]]; then
    _mods_cmake=-DQTCLIENT_ENABLE_MODS=OFF
  fi
  _launcher_mods_cmake=-DQTCLIENT_ENABLE_LAUNCHER_MODS=OFF
  case "${QTCLIENT_ENABLE_LAUNCHER_MODS:-}" in
    1|ON|on|true|TRUE|yes|YES) _launcher_mods_cmake=-DQTCLIENT_ENABLE_LAUNCHER_MODS=ON ;;
  esac
  _mods_list_cmake=()
  if [[ -n "${QTCLIENT_MODS:-}" ]]; then
    _mods_list_cmake=(-DQTCLIENT_MODS="${QTCLIENT_MODS}")
  fi
  _linked_pack_cmake=()
  if [[ -n "${QTCLIENT_LINKED_PACK:-}" ]]; then
    _linked_pack_cmake=(-DQTCLIENT_LINKED_PACK="${QTCLIENT_LINKED_PACK}")
    _mods_cmake=-DQTCLIENT_ENABLE_MODS=ON
  fi
  WASM_CMAKE_ARGS=(
    -G Ninja -S "$OTC" -B "$BUILD_DIR"
    -DCMAKE_MAKE_PROGRAM="$NINJA"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$QT_WASM_TOOLCHAIN"
    -DVCPKG_TARGET_TRIPLET=wasm32-emscripten
    -DQT_HOST_PATH="$QT_DESKTOP"
    -DCMAKE_PREFIX_PATH="$QT_WASM"
    -DWASM=ON
    -DTIBIA_BUILD_APP=ON
    -DTIBIA_BUILD_TESTS=OFF
    -DTOGGLE_BIN_FOLDER=ON
    -DOPTIONS_ENABLE_IPO=OFF
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
    -DCMAKE_CXX_STANDARD=20
    -DCMAKE_CXX_SCAN_FOR_MODULES=OFF
    -DTIBIA_WASM_GRAPHICS_RCC="$RCC"
    -DWASM_INITIAL_MEMORY="$WASM_INITIAL_MEMORY"
    -DWASM_USE_WASM_EXCEPTIONS=OFF
    "$_mods_cmake"
    "$_launcher_mods_cmake"
    "${_mods_list_cmake[@]}"
    "${_linked_pack_cmake[@]}"
  )
  FP="$(
    {
    echo "tibia-wasm-vcpkg-v3-linked-tip"
    echo "stock-host=${STOCK_HOST:-0}"
    echo "mods=${QTCLIENT_ENABLE_MODS:-ON}"
    echo "mods-list=${QTCLIENT_MODS:-}"
    echo "linked-pack=${QTCLIENT_LINKED_PACK:-}"
    echo "launcher-mods=${QTCLIENT_ENABLE_LAUNCHER_MODS:-OFF}"
    printf '%s\n' "${WASM_CMAKE_ARGS[@]}"
    _sha256 "$OTC/vcpkg.json" "$OTC/vcpkg-configuration.json" 2>/dev/null || true
  } | _sha256 | awk '{print $1}'
)"
  if [[ -f "$HELPER" ]]; then
    bash "$HELPER" "$BUILD_DIR" "$FP" -- cmake "${WASM_CMAKE_ARGS[@]}"
  else
    cmake "${WASM_CMAKE_ARGS[@]}"
  fi

  # STATIC packs link into tibia — one tip target.
  cmake --build "$BUILD_DIR" --target tibia -j"$JOBS"
  cmake --build "$BUILD_DIR" --target launcher -j"$JOBS"

  # Prefer tibia.* (CDN / portal). If a stale OUTPUT_NAME=client build left
  # client.html, rename the set so pack/CDN keep a stable basename.
  OUT_HTML="$BUILD_DIR/tibia.html"
  if [[ ! -f "$OUT_HTML" ]]; then
    OUT_HTML="$(find "$BUILD_DIR" -name 'tibia.html' -type f 2>/dev/null | head -1 || true)"
  fi
  if [[ -z "$OUT_HTML" || ! -f "$OUT_HTML" ]]; then
    CLIENT_HTML="$(find "$BUILD_DIR" -name 'client.html' -type f 2>/dev/null | head -1 || true)"
    if [[ -n "$CLIENT_HTML" && -f "$CLIENT_HTML" ]]; then
      OUT_DIR="$(dirname "$CLIENT_HTML")"
      echo "[tibia] renaming client.* → tibia.* under $OUT_DIR (CDN basename)"
      for ext in html js wasm data js.gz wasm.gz js.br wasm.br; do
        [[ -e "$OUT_DIR/client.$ext" ]] || continue
        rm -f "$OUT_DIR/tibia.$ext"
        mv "$OUT_DIR/client.$ext" "$OUT_DIR/tibia.$ext"
      done
      OUT_HTML="$OUT_DIR/tibia.html"
    fi
  fi
  if [[ -z "$OUT_HTML" || ! -f "$OUT_HTML" ]]; then
    echo "[tibia] build finished but tibia.html missing under $BUILD_DIR"
    find "$BUILD_DIR" -maxdepth 4 \( -name '*.html' -o -name '*.wasm' -o -name '*.js' \) 2>/dev/null | head -40 || true
    exit 1
  fi
  OUT_DIR="$(dirname "$OUT_HTML")"
  for f in tibia.js tibia.wasm; do
    src="$OUT_DIR/$f"
    [[ -f "$src" ]] || continue
    rm -f "$src.gz" "$src.br"
    gzip -6 -k -n -c "$src" > "$src.gz" || true
    if command -v brotli >/dev/null 2>&1; then
      brotli -q 9 -k -f -o "$src.br" "$src" || true
    fi
  done

  if [[ "$OUT_DIR" != "$BUILD_DIR" ]]; then
    rel_out="$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$OUT_DIR" "$BUILD_DIR")"
    for f in tibia.html tibia.js tibia.wasm tibia.js.gz tibia.wasm.gz \
             tibia.js.br tibia.wasm.br tibia.data qtloader.js qtlogo.svg \
             launcher.html launcher.js launcher.wasm; do
      [[ -f "$OUT_DIR/$f" ]] || continue
      rm -f "$BUILD_DIR/$f"
      ln -s "${rel_out}/$f" "$BUILD_DIR/$f"
    done
  fi
  LAUNCHER_HTML="$(find "$BUILD_DIR" -name 'launcher.html' -type f 2>/dev/null | head -1 || true)"
  if [[ -n "$LAUNCHER_HTML" ]]; then
    launcher_dir="$(dirname "$LAUNCHER_HTML")"
    rel_launcher="$(python3 -c 'import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))' "$launcher_dir" "$BUILD_DIR")"
    for f in launcher.html launcher.js launcher.wasm; do
      [[ -f "$launcher_dir/$f" ]] || continue
      [[ "$launcher_dir" == "$BUILD_DIR" ]] && continue
      rm -f "$BUILD_DIR/$f"
      ln -s "${rel_launcher}/$f" "$BUILD_DIR/$f"
    done
    if [[ ! -e "$BUILD_DIR/qtloader.js" && -f "$launcher_dir/qtloader.js" ]]; then
      ln -s "${rel_launcher}/qtloader.js" "$BUILD_DIR/qtloader.js"
    fi
  fi

  NOTES="$BUILD_DIR/tibia-wasm-size.txt"
  {
    echo "tibia wasm artifact sizes ($(date -u +%Y-%m-%dT%H:%MZ))"
    echo "BUILD_TYPE=$BUILD_TYPE INITIAL_MEMORY=$WASM_INITIAL_MEMORY"
    echo "EXCEPTIONS=SJLJ (Qt wasm_multithread; wasm32-emscripten triplet)"
    echo "VCPKG_BASELINE=$(python3 -c 'import json;print(json.load(open("'"$OTC"'/vcpkg.json")).get("builtin-baseline","")[:12])' 2>/dev/null || echo unknown)"
    for f in tibia.html tibia.js tibia.wasm tibia.data qtloader.js; do
      path="$OUT_DIR/$f"
      [[ -f "$path" ]] || continue
      sz="$(wc -c < "$path" | tr -d ' ')"
      echo "$f $sz"
    done
    if [[ -f "$OUT_DIR/tibia.wasm.gz" ]]; then
      echo "tibia.wasm.gz $(wc -c < "$OUT_DIR/tibia.wasm.gz" | tr -d ' ')"
    fi
    if [[ -f "$OUT_DIR/tibia.js.gz" ]]; then
      echo "tibia.js.gz $(wc -c < "$OUT_DIR/tibia.js.gz" | tr -d ' ')"
    fi
  } | tee "$NOTES"
  [[ "$OUT_DIR" != "$BUILD_DIR" ]] && cp -f "$NOTES" "$OUT_DIR/tibia-wasm-size.txt"

  echo "[tibia] ok → $BUILD_DIR/tibia.html (from $OUT_HTML)"
  echo "[tibia] serve: python3 $OTC/tools/emscripten-web-serve.py --root $OUT_DIR --port 8000  (open /tibia.html)"
  exit 0
fi

QT_DESKTOP="$(bash "$QT_PREFIX_SH")"
if ! _qt_ok "$QT_DESKTOP" && _qt_ok "${QT_PREFIX:-}"; then
  QT_DESKTOP="$QT_PREFIX"
fi
if ! _qt_ok "$QT_DESKTOP"; then
  echo "[tibia] Qt not installed — run: make qt (QT_ROOT=${QT_ROOT:-unset})"
  exit 1
fi

case "$(uname -s)" in
  Darwin)
    export VCPKG_TARGET_TRIPLET="$(osx_triplet)"
    PRESET=macos-release
    [[ "$DEBUG_BUILD" -eq 1 ]] && PRESET=macos-debug
    [[ "$DEV_BUILD" -eq 1 ]] && PRESET=macos-dev
    ;;
  Linux)
    export VCPKG_TARGET_TRIPLET=x64-linux-static
    PRESET=linux-release
    [[ "$DEBUG_BUILD" -eq 1 ]] && PRESET=linux-debug
    [[ "$DEV_BUILD" -eq 1 ]] && PRESET=linux-dev
    ;;
  MINGW*|MSYS*|CYGWIN*)
    # Git Bash / MSYS on Windows (Actions windows-2022). Soft CI uses the same
    # windows-release preset; DEV maps to release (no windows-dev preset).
    export VCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-windows-static-md}"
    PRESET=windows-release
    [[ "$DEBUG_BUILD" -eq 1 ]] && PRESET=windows-debug
    ;;
  *)
    echo "[tibia] unsupported host: $(uname -s)"
    exit 1
    ;;
esac

cd "$OTC"
# Canonical tree: apps/qtclient/build/<preset> (umbrella DEV → macos-dev / linux-dev).
# Do not point cmake elsewhere or wipe this dir mid-build — use this script / make client-build.
BUILD_DIR="$OTC/build/$PRESET"
echo "[tibia] desktop preset=$PRESET build=$BUILD_DIR qt=$QT_DESKTOP triplet=$VCPKG_TARGET_TRIPLET"
# pgrep is often missing on Windows Git Bash; never treat that as fatal.
if [[ -f "$BUILD_DIR/build.ninja" ]] && command -v pgrep >/dev/null 2>&1 \
  && pgrep -f "ninja.*-C $BUILD_DIR|ninja -C $BUILD_DIR|cmake --build $BUILD_DIR|cmake --build --preset $PRESET" >/dev/null 2>&1; then
  echo "[tibia] WARN: another ninja/cmake build may already be using $BUILD_DIR — wait for it instead of starting a second one" >&2
fi
HELPER="$MONOREPO/scripts/cmake-configure-if-needed.sh"
_mods_cmake=-DQTCLIENT_ENABLE_MODS=ON
if [[ "${STOCK_HOST:-0}" == "1" || "${QTCLIENT_ENABLE_MODS:-}" =~ ^(0|OFF|off|false|FALSE)$ ]]; then
  _mods_cmake=-DQTCLIENT_ENABLE_MODS=OFF
fi
_launcher_mods_cmake=-DQTCLIENT_ENABLE_LAUNCHER_MODS=OFF
case "${QTCLIENT_ENABLE_LAUNCHER_MODS:-}" in
  1|ON|on|true|TRUE|yes|YES) _launcher_mods_cmake=-DQTCLIENT_ENABLE_LAUNCHER_MODS=ON ;;
esac
_linked_pack_cmake=()
if [[ -n "${QTCLIENT_LINKED_PACK:-}" ]]; then
  _linked_pack_cmake=(-DQTCLIENT_LINKED_PACK="${QTCLIENT_LINKED_PACK}")
  _mods_cmake=-DQTCLIENT_ENABLE_MODS=ON
fi
FP="$(
  {
    echo "tibia-desktop-vcpkg-v2-linked-tip"
    echo "preset=$PRESET"
    echo "qt=$QT_DESKTOP"
    echo "triplet=$VCPKG_TARGET_TRIPLET"
    echo "qml-disk=$DEV_BUILD"
    echo "stock-host=${STOCK_HOST:-0}"
    echo "mods=${QTCLIENT_ENABLE_MODS:-ON}"
    echo "linked-pack=${QTCLIENT_LINKED_PACK:-}"
    echo "launcher-mods=${QTCLIENT_ENABLE_LAUNCHER_MODS:-OFF}"
    _sha256 "$OTC/CMakePresets.json" "$OTC/vcpkg.json" 2>/dev/null || true
  } | _sha256 | awk '{print $1}'
)"
command -v cmake >/dev/null || { echo "[tibia] cmake not on PATH after env setup" >&2; exit 127; }
if [[ -f "$HELPER" ]]; then
  bash "$HELPER" "$BUILD_DIR" "$FP" -- \
    cmake --preset "$PRESET" \
    -DTOGGLE_BIN_FOLDER=ON \
    -DCMAKE_PREFIX_PATH="$QT_DESKTOP" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
    "$_mods_cmake" \
    "$_launcher_mods_cmake" \
    "${_linked_pack_cmake[@]}"
else
  cmake --preset "$PRESET" \
    -DTOGGLE_BIN_FOLDER=ON \
    -DCMAKE_PREFIX_PATH="$QT_DESKTOP" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
    "$_mods_cmake" \
    "$_launcher_mods_cmake" \
    "${_linked_pack_cmake[@]}"
fi
# Expected qtclient outputs (TOGGLE_BIN_FOLDER=ON) — no multi-path search.
case "$(uname -s)" in
  Darwin)
    CLIENT_BIN="$BUILD_DIR/bin/client.app/Contents/MacOS/client"
    LAUNCHER_BIN="$BUILD_DIR/bin/launcher.app/Contents/MacOS/launcher"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    CLIENT_BIN="$BUILD_DIR/bin/client.exe"
    LAUNCHER_BIN="$BUILD_DIR/bin/launcher.exe"
    ;;
  *)
    CLIENT_BIN="$BUILD_DIR/bin/client"
    LAUNCHER_BIN="$BUILD_DIR/bin/launcher"
    ;;
esac
if [[ "$LAUNCHER_BUILD" -eq 1 ]]; then
  cmake --build --preset "$PRESET" --target launcher -j"$JOBS"
  rm -rf "$BUILD_DIR/launcher/src/launcher.app" "$BUILD_DIR/launcher/src/launcher"
  BIN="$LAUNCHER_BIN"
else
  cmake --build --preset "$PRESET" --target tibia -j"$JOBS"
  rm -rf "$BUILD_DIR/client/tibia/tibia.app" "$BUILD_DIR/client/tibia/client.app"
  rm -f "$BUILD_DIR/client/tibia/tibia" "$BUILD_DIR/client/tibia/client" \
        "$BUILD_DIR/bin/tibia" "$BUILD_DIR/bin/tibia.exe"
  BIN="$CLIENT_BIN"
fi
if [[ ! -e "$BIN" ]]; then
  echo "[tibia] build finished but missing $BIN" >&2
  exit 1
fi
echo "[tibia] ok → $BIN"
if [[ "$LAUNCHER_BUILD" -eq 0 ]]; then
  bash "$OTC/scripts/lifecycle/lib/link-cip-resources.sh" "$BIN" "$OTC/data" || true
fi
if [[ "$DEV_BUILD" -eq 1 ]]; then
  echo "[tibia] qml-disk: edit qml under client/upstream/qml, client/mods/<pack>/qml, client/overrides — no rebuild; F5 clears cache"
fi
