#!/usr/bin/env bash
# Publish the CIP-derived slice of the CDN bundle (see qtclient/release).
#
# Slice layout (bundle-relative; manifests never hold absolute URLs):
#   .qtclient/v3/content-things.json — CIP-flat file list
#                                    (Contents/Resources/assets/… + fonts),
#                                    added to the web + desktop targets by assemble.py
#   content/<aa>/<sha256>          — its blobs; packs/<aa>/<sha256>.zip — its zip packs
#   images/store/                  — CIP store icons (coinImagesURL), from
#                                    apps/portal/apps/web/static/images/store
#   images/payment/                — webshop product art (optional placeholders)
#   assets/store/                  — legacy alias (copied from images/store for old tips)
#   assets/{outfits,items}/manifest.json — {"revision", "path": "<rev>/"} (portal)
#   assets/outfits/<rev>/<looktype>/     — outfit frames rendered by `make extract`
#   assets/items/<rev>/<client id>.png   — item sprites rendered by `make extract`
#   .qtclient/assets.json            — pointer read by the bundle build
#
# Assets, outfits and items come from `make extract` on a machine with Tibia,
# so CI never builds this slice; --push uploads it as the files-only image
# ghcr.io/qtclient/cdn-assets.
#
# Usage:
#   python3 apps/qtclient/tools/publish/cli.py assets              # local → TIBIA_ASSETS_SLICE
#   python3 apps/qtclient/tools/publish/cli.py assets --push       # + push ghcr cdn-assets
#   python3 apps/qtclient/tools/publish/cli.py assets things       # one part (things|store|outfits|items)
#
# Env:
#   TIBIA_ASSETS_SLICE    local output, default ./dist/slices/assets
#   TIBIA_ASSETS_IMAGE    default ghcr.io/qtclient/cdn-assets (needs `docker login ghcr.io`)
#   CLIENT_VERSION         protocol stamp for .qtclient metadata (default 1533)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OTC="$(cd "$SCRIPT_DIR/../.." && pwd)"
if [[ "$(basename "$(dirname "$OTC")")" == "apps" ]]; then
  ROOT="$(cd "$OTC/../.." && pwd)"
else
  ROOT="$(cd "$OTC/.." && pwd)"
fi
cd "$ROOT"

VERSION="${CLIENT_VERSION:-1533}"
SLICE="${TIBIA_ASSETS_SLICE:-$ROOT/dist/slices/assets}"
IMAGE="${TIBIA_ASSETS_IMAGE:-ghcr.io/qtclient/cdn-assets}"
PUSH=0
ONLY=""

for arg in "$@"; do
  case "$arg" in
    --push) PUSH=1 ;;
    things|store|outfits|items) ONLY="$arg" ;;
    -h|--help)
      sed -n '2,27p' "$0"
      exit 0
      ;;
    *)
      echo "unknown arg: $arg" >&2
      exit 1
      ;;
  esac
done

publish_things() {
  local src=""
  if [[ -f "$OTC/data/assets/catalog-content.json" ]]; then
    src="$OTC/data/assets"
  elif [[ -f "$OTC/data/things/$VERSION/catalog-content.json" ]]; then
    src="$OTC/data/things/$VERSION"
  elif [[ -f "$ROOT/.cache/qtclient/things/catalog-content.json" ]]; then
    src="$ROOT/.cache/qtclient/things"
  else
    src="$OTC/data/assets"
  fi
  if [[ ! -f "$src/catalog-content.json" ]]; then
    echo "[assets] missing $src/catalog-content.json — run: make extract" >&2
    exit 1
  fi
  if [[ ! -f "$src/graphics_resources.rcc" ]]; then
    echo "[assets] missing graphics_resources.rcc — run: make extract / tibia-sync" >&2
    exit 1
  fi
  # CipSoft minimap: CIP Resources/minimap/ tiles (copied by make extract). Optional
  # tibiamaps minimap.otmm is legacy (`make extract … EXTRACT_FLAGS=--otmm`).
  if [[ ! -d "$src/minimap" && ! -f "$src/minimap.otmm" ]]; then
    echo "[assets] WARN: no CIP minimap/ (or minimap.otmm) under $src — CipSoft starts with an empty map" >&2
  fi

  # CIP satellite-/minimap-*.bmp.lzma chunks and the backdrop map stay out.
  # Only the catalog's appearances ship (older ones linger after an extract).
  local appearances
  appearances="$(python3 -c 'import json,sys; print(next(e["file"] for e in json.load(open(sys.argv[1])) if e.get("type") == "appearances"))' \
    "$src/catalog-content.json")"
  local tree assets_dir
  tree="$(mktemp -d "${TMPDIR:-/tmp}/qtclient-assets.XXXXXX")"
  # CIP install paths under the package root (launcher installAssets).
  assets_dir="$tree/Contents/Resources/assets"
  mkdir -p "$assets_dir" "$tree/Contents/MacOS"
  cleanup_tree() { rm -rf "$tree"; }
  trap cleanup_tree RETURN
  # CipSoft: Resources/minimap is a sibling of Resources/assets (not nested under assets).
  rsync -a --delete --exclude 'minimap-*.bmp.lzma' --exclude 'satellite-*.bmp.lzma' \
    --exclude 'backdrop_map-*' --exclude 'sounds' --exclude 'graphics_resources.rcc' \
    --exclude 'minimap' --exclude 'minimap.otmm' \
    --include "$appearances" --exclude 'appearances-*' "$src/" "$assets_dir/"

  # Engine UI art — CIP ships this in the client package; keep it on the assets
  # tip too so WASM/desktop Play has chrome before the engine slice is complete.
  cp "$src/graphics_resources.rcc" "$tree/Contents/MacOS/graphics_resources.rcc"

  if [[ -d "$src/minimap" ]]; then
    mkdir -p "$tree/Contents/Resources/minimap"
    rsync -a --delete "$src/minimap/" "$tree/Contents/Resources/minimap/"
    echo "[assets] CipSoft minimap → Contents/Resources/minimap"
  fi
  if [[ -f "$src/minimap.otmm" ]]; then
    mkdir -p "$tree/Contents/Resources/minimap"
    cp "$src/minimap.otmm" "$tree/Contents/Resources/minimap/minimap.otmm"
  fi

  local tables="$ROOT/packages/asset-pipeline/data/tibia-client/game-tables/spells.json"
  if [[ -f "$tables" ]]; then
    cp "$tables" "$assets_dir/spells.json"
  elif [[ ! -f "$assets_dir/spells.json" ]]; then
    echo "[assets] WARN: no spells.json (make extract); the spell list stays empty" >&2
  fi

  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$assets_dir/catalog-content.json" | awk '{print $1}' >"$assets_dir/assets.json.sha256"
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$assets_dir/catalog-content.json" | awk '{print $1}' >"$assets_dir/assets.json.sha256"
  else
    printf 'content-things-%s\n' "$VERSION" >"$assets_dir/assets.json.sha256"
  fi

  # CIP: Resources/sounds is a sibling of Resources/assets (not nested under assets).
  local sounds=""
  if [[ -f "$src/sounds/catalog-sound.json" ]]; then
    sounds="$src/sounds"
  elif [[ -f "$OTC/data/assets/sounds/catalog-sound.json" ]]; then
    sounds="$OTC/data/assets/sounds"
  elif [[ -f "$OTC/data/sounds/$VERSION/catalog-sound.json" ]]; then
    sounds="$OTC/data/sounds/$VERSION"
  fi
  if [[ -n "$sounds" ]]; then
    mkdir -p "$tree/Contents/Resources/sounds"
    rsync -a "$sounds/" "$tree/Contents/Resources/sounds/"
    echo "[assets] sounds → Contents/Resources/sounds"
  else
    echo "[assets] WARN: no sound bank (make extract); the client stays silent" >&2
  fi

  # Verdana for WASM (desktop has system fonts). qtclient-only tip files.
  local fonts="$tree/Contents/Resources/assets/fonts"
  mkdir -p "$fonts"
  copy_font() {
    local src_font="$1" dest_name="$2"
    [[ -f "$src_font" ]] || return 1
    cp "$src_font" "$fonts/$dest_name"
  }
  copy_font "/System/Library/Fonts/Supplemental/Verdana.ttf" Verdana.ttf \
    || copy_font "/Library/Fonts/Verdana.ttf" Verdana.ttf \
    || copy_font "/usr/share/fonts/truetype/msttcorefonts/Verdana.ttf" Verdana.ttf \
    || true
  copy_font "/System/Library/Fonts/Supplemental/Verdana Bold.ttf" Verdana-Bold.ttf \
    || copy_font "/usr/share/fonts/truetype/msttcorefonts/Verdana_Bold.ttf" Verdana-Bold.ttf \
    || true
  copy_font "/System/Library/Fonts/Supplemental/Verdana Italic.ttf" Verdana-Italic.ttf \
    || copy_font "/usr/share/fonts/truetype/msttcorefonts/Verdana_Italic.ttf" Verdana-Italic.ttf \
    || true
  copy_font "/System/Library/Fonts/Supplemental/Verdana Bold Italic.ttf" Verdana-Bold-Italic.ttf \
    || copy_font "/usr/share/fonts/truetype/msttcorefonts/Verdana_Bold_Italic.ttf" Verdana-Bold-Italic.ttf \
    || true
  if [[ -f "$fonts/Verdana.ttf" ]]; then
    echo "[assets] fonts → Contents/Resources/assets/fonts"
  else
    echo "[assets] WARN: Verdana not found on this machine; browser text will not match desktop" >&2
    rmdir "$fonts" 2>/dev/null || true
  fi

  # No --prefix: paths are already CIP package-relative.
  rm -rf "$SLICE/content" "$SLICE/packs" "$SLICE/.qtclient/v3" "$SLICE/assets/things"
  python3 "$SCRIPT_DIR/content_store.py" --root "$tree" --out "$SLICE" --name content-things \
    --revision auto --engine "$VERSION" --prefix ""
  cleanup_tree
  trap - RETURN
}

publish_store() {
  local src="$ROOT/apps/portal/apps/web/static/images/store"
  if [[ ! -d "$src/64" ]]; then
    echo "[assets] store missing — expected committed $src" >&2
    exit 1
  fi
  # CIP path: /images/store/{size}/…
  local dest="$SLICE/images/store"
  mkdir -p "$dest"
  rsync -aL --delete --exclude '*.md' "$src/" "$dest/"
  # Legacy tip path for one-release compatibility.
  mkdir -p "$SLICE/assets"
  rm -rf "$SLICE/assets/store"
  ln -sfn ../images/store "$SLICE/assets/store" 2>/dev/null \
    || rsync -aL --delete "$dest/" "$SLICE/assets/store/"
  # Webshop payment art placeholders (CIP /images/payment/).
  mkdir -p "$SLICE/images/payment"
  if [[ -d "$ROOT/apps/portal/apps/web/static/images/payment" ]]; then
    rsync -aL --exclude '*.md' "$ROOT/apps/portal/apps/web/static/images/payment/" "$SLICE/images/payment/"
  fi
  echo "[assets] store → $dest (+ assets/store alias, images/payment)"
}

# AAC sprite slice rendered by `make extract` (tools/extract/<name>.py) into
# apps/portal/apps/web/<dir>, published under a revisioned path.
publish_rendered() {
  local name="$1" src="$ROOT/apps/portal/apps/web/$2"
  if [[ ! -f "$src/.revision" ]]; then
    echo "[assets] $name missing — run: make extract (or python3 tools/extract/$name.py)" >&2
    exit 1
  fi
  local rev
  rev="$(tr -d '[:space:]' <"$src/.revision")"
  local dest="$SLICE/assets/$name"
  rm -rf "$dest"
  mkdir -p "$dest/$rev"
  rsync -a --exclude .revision "$src/" "$dest/$rev/"
  cat >"$dest/manifest.json" <<EOF
{
  "revision": "${rev}",
  "path": "${rev}/"
}
EOF
  echo "[assets] $name ${rev} → $dest"
}

things_revision() {
  python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["revision"])' \
    "$SLICE/.qtclient/v3/content-things.json"
}

write_pointer() {
  [[ -f "$SLICE/.qtclient/v3/content-things.json" ]] || return 0
  cat >"$SLICE/.qtclient/assets.json" <<EOF
{
  "assetsRevision": "$(things_revision)",
  "clientVersion": "${VERSION}",
  "builtAt": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
}

push_image() {
  command -v docker >/dev/null || { echo "missing: docker" >&2; exit 1; }
  if [[ ! -f "$SLICE/.qtclient/v3/content-things.json" || ! -d "$SLICE/images/store" \
    || ! -f "$SLICE/assets/outfits/manifest.json" || ! -f "$SLICE/assets/items/manifest.json" ]]; then
    echo "[assets] --push needs things, images/store, outfits and items published into $SLICE" >&2
    exit 1
  fi
  local rev
  rev="$(things_revision)"
  # Tags are immutable: hash every part, not just the things revision.
  local digest
  digest="$(cd "$SLICE" && find assets .qtclient/v3 -type f ! -path 'assets/outfits/*/*/*' ! -path 'assets/items/*/*' \
    -print0 | sort -z | xargs -0 shasum -a 256 | shasum -a 256 | cut -c1-8)"
  local tag="${rev:0:12}-${digest}"
  echo "[assets] pushing ${IMAGE}:${tag} (+ :latest)"
  printf 'FROM scratch\nCOPY . /\n' | docker buildx build --platform linux/amd64 \
    -t "${IMAGE}:${tag}" -t "${IMAGE}:latest" --push -f - "$SLICE"

  echo "[assets] publish with: gh workflow run release.yml -R qtclient/release -f components=assets"
}

mkdir -p "$SLICE"

if [[ -z "$ONLY" || "$ONLY" == "things" ]]; then publish_things; fi
if [[ -z "$ONLY" || "$ONLY" == "store" ]]; then publish_store; fi
if [[ -z "$ONLY" || "$ONLY" == "outfits" ]]; then publish_rendered outfits outfits_anim; fi
if [[ -z "$ONLY" || "$ONLY" == "items" ]]; then publish_rendered items items; fi
write_pointer
if [[ "$PUSH" == "1" ]]; then push_image; fi

echo "[assets] done → $SLICE"
