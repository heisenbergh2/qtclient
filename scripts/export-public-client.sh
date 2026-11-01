#!/usr/bin/env bash
# Export a public qtclient tree: no product packs, no upstream/assets blobs,
# IdleOT branding scrubbed. Prefer a fresh git history when pushing.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:?usage: $0 <dest-dir>}"

if [[ -e "$DEST" ]]; then
  echo "refusing to overwrite existing path: $DEST" >&2
  exit 1
fi

mkdir -p "$DEST"
rsync -a \
  --exclude '.git' \
  --exclude '.git/' \
  --exclude '**/.git' \
  --exclude '**/.git/' \
  --exclude 'build/' \
  --exclude 'cmake-build-*/' \
  --exclude 'vcpkg_installed/' \
  --exclude 'upstream/' \
  --exclude 'client/upstream/' \
  --exclude 'launcher/upstream/' \
  --exclude 'cip/' \
  --exclude 'data/assets/' \
  --exclude 'data/things/' \
  --exclude 'data/sounds/' \
  --exclude 'client/mods/idleot/' \
  --exclude 'IdleOT.app/' \
  --exclude 'IdleOT*.app/' \
  --exclude 'cmake/IdleOT.icns' \
  --exclude '**/session*.dmp' \
  --exclude 'third_party/qt5-cip/' \
  --exclude 'third_party/Qt5LegacyControls/' \
  --exclude 'docs/otc-cleanup-checklist.md' \
  --exclude 'IdleOT.app' \
  --exclude '**/idleot.entitlements' \
  --exclude 'tools/publish/installer/idleot.entitlements' \
  --exclude 'meta.lua' \
  --exclude 'data/locales/' \
  --exclude 'data/particles/' \
  --exclude 'data/cursors/' \
  --exclude 'tools/gen_lang_template.sh' \
  --exclude 'records/*.cam' \
  --exclude 'smoke-world.cam' \
  --exclude 'aqtinstall.log' \
  --exclude 'qtclient.log' \
  --exclude 'crystalotc.log' \
  --exclude 'packet.log' \
  --exclude '.github/workflows/ci.yml' \
  --exclude '.github/workflows/clean-cache.yaml' \
  --exclude '.github/workflows/cron-stale.yml' \
  --exclude '.github/workflows/issue.yml' \
  --exclude '.github/workflows/pr-labeler.yml' \
  --exclude '.github/workflows/reusable-build-*.yml' \
  --exclude '.github/workflows/reusable-checks.yml' \
  "$ROOT/" "$DEST/"

# Recreate submodule gitlinks via .gitmodules only — clone populates them.
# Copy .gitmodules; consumers run: git submodule update --init …

# Public gitignore: mods allowlist without private idleot; ignore upstream.
GITIGNORE="$DEST/.gitignore"
if [[ -f "$GITIGNORE" ]]; then
  grep -vE '!/client/mods/idleot|/data/sounds/idleot|IdleOT|idleot/' "$GITIGNORE" >"$GITIGNORE.tmp" || true
  mv "$GITIGNORE.tmp" "$GITIGNORE"
  # Ensure upstream ignored
  if ! grep -qE '^/upstream/' "$GITIGNORE"; then
    printf '\n# Public: builders run make sync (BYO Tibia.app)\n/upstream/\n' >>"$GITIGNORE"
  fi
  # Drop private force-unignore leftovers
  sed -i.bak -E '/^!\/mods\/idleot/d;/^!\/data\/sounds\/idleot/d' "$GITIGNORE" 2>/dev/null || true
  rm -f "$GITIGNORE.bak"
fi

rm -rf "$DEST/client/mods/idleot" "$DEST/launcher/mods/idleot" \
  "$DEST/client/src/idleot" "$DEST/client/qml/idleot" "$DEST/qml/idleot" \
  "$DEST/cmake/IdleOT.icns" "$DEST/IdleOT.app" 2>/dev/null || true

# Brand → qtclient in exported tree (source tree may still say IdleOT privately).
export_scrub() {
  local f="$1"
  [[ -f "$f" ]] || return 0
  # Portable in-place sed
  sed -i.bak \
    -e 's/IdleOT\.app/QtClient.app/g' \
    -e 's/IdleOT/qtclient/g' \
    -e 's/otquartz/qtclient/g' \
    -e 's/OTQUARTZ/QTCLIENT/g' \
    -e 's/idleot/qtclient/g' \
    -e 's/IDLEOT/QTCLIENT/g' \
    -e 's|ghcr\.io/idleot/qt-cip|ghcr.io/qtclient/qt-cip|g' \
    -e 's|ghcr\.io/otquartz/qt-cip|ghcr.io/qtclient/qt-cip|g' \
    -e 's|github\.com/idleot/qtclient|github.com/qtclient/qtclient|g' \
    -e 's|github\.com/otquartz/otquartz|github.com/qtclient/qtclient|g' \
    -e 's|static\.idleot\.com|static.tibia.com|g' \
    "$f" 2>/dev/null || true
  rm -f "${f}.bak"
}

# Prefer dedicated public docs if present.
if [[ -f "$ROOT/docs/public/README.md" ]]; then
  cp "$ROOT/docs/public/README.md" "$DEST/README.md"
fi
if [[ -f "$ROOT/docs/public/AGENTS.md" ]]; then
  cp "$ROOT/docs/public/AGENTS.md" "$DEST/AGENTS.md"
fi
if [[ -f "$ROOT/docs/public/CONTRIBUTING.md" ]]; then
  cp "$ROOT/docs/public/CONTRIBUTING.md" "$DEST/CONTRIBUTING.md"
fi

for f in \
  "$DEST/README.md" \
  "$DEST/Makefile" \
  "$DEST/AGENTS.md" \
  "$DEST/CONTRIBUTING.md" \
  "$DEST/docs/open-source.md" \
  "$DEST/docs/mods.md" \
  "$DEST/docs/building.md" \
  "$DEST/docs/development/build-tooling.md" \
  "$DEST/tools/qt/qt-version.sh" \
  "$DEST/tools/qt/README.md" \
  "$DEST/.github/workflows/build-qt-cip.yml" \
  "$DEST/.github/workflows/ci-stock.yml"
do
  export_scrub "$f"
done

# Default GHCR image for public consumers
if [[ -f "$DEST/tools/qt/qt-version.sh" ]]; then
  sed -i.bak 's|ghcr\.io/idleot/qt-cip|ghcr.io/qtclient/qt-cip|g;s|ghcr\.io/otquartz/qt-cip|ghcr.io/qtclient/qt-cip|g;s|ghcr\.io/qtclient/qt-cip|ghcr.io/qtclient/qt-cip|g' \
    "$DEST/tools/qt/qt-version.sh"
  if ! grep -q 'ghcr.io/qtclient/qt-cip' "$DEST/tools/qt/qt-version.sh"; then
    sed -i.bak 's|QT_GHCR_IMAGE="${QT_GHCR_IMAGE:-.*}"|QT_GHCR_IMAGE="${QT_GHCR_IMAGE:-ghcr.io/qtclient/qt-cip}"|' \
      "$DEST/tools/qt/qt-version.sh"
  fi
  rm -f "$DEST/tools/qt/qt-version.sh.bak"
fi

# Makefile banner
if [[ -f "$DEST/Makefile" ]]; then
  sed -i.bak '1s/.*/# qtclient (otqt) — CipSoft-compatible Qt\/QML Tibia client/' "$DEST/Makefile" || true
  sed -i.bak 's/qtclient (IdleOT tibia)/qtclient (otqt)/g;s/qtclient — IdleOT tibia client/qtclient (otqt)/g;s/otquartz/qtclient/g' "$DEST/Makefile" || true
  rm -f "$DEST/Makefile.bak"
fi

# Record submodule SHAs for consumers (rsync skipped checked-out trees).
mkdir -p "$DEST/third_party"
if [[ -f "$ROOT/.gitmodules" ]]; then
  cp "$ROOT/.gitmodules" "$DEST/.gitmodules"
fi
# Write SHA pins so CI can resolve without a full qt5-cip checkout
{
  echo "# Generated by export-public-client.sh — submodule pins"
  if git -C "$ROOT" rev-parse "HEAD:third_party/qt5-cip" >/dev/null 2>&1; then
    echo "qt5-cip $(git -C "$ROOT" rev-parse HEAD:third_party/qt5-cip)"
  elif [[ -d "$ROOT/third_party/qt5-cip/.git" || -f "$ROOT/third_party/qt5-cip/.git" ]]; then
    echo "qt5-cip $(git -C "$ROOT/third_party/qt5-cip" rev-parse HEAD)"
  fi
  if git -C "$ROOT" rev-parse "HEAD:third_party/Qt5LegacyControls" >/dev/null 2>&1; then
    echo "Qt5LegacyControls $(git -C "$ROOT" rev-parse HEAD:third_party/Qt5LegacyControls)"
  elif [[ -d "$ROOT/third_party/Qt5LegacyControls/.git" || -f "$ROOT/third_party/Qt5LegacyControls/.git" ]]; then
    echo "Qt5LegacyControls $(git -C "$ROOT/third_party/Qt5LegacyControls" rev-parse HEAD)"
  fi
} >"$DEST/third_party/SUBMODULE_PINS.txt"

# Bulk-scrub leftover product tokens in text sources (logging categories, comments).
# Structural product islands were already deleted above.
if command -v rg >/dev/null; then
  while IFS= read -r -d '' f; do
    sed -i.bak \
      -e 's/IdleOT\.app/QtClient.app/g' \
      -e 's/IdleOT/qtclient/g' \
      -e 's/IDLEOT_/QTCLIENT_/g' \
      -e 's/_idleot_/_qtclient_/g' \
      -e 's/idleot\./qtclient./g' \
      -e 's|ghcr\.io/idleot/|ghcr.io/qtclient/|g' \
      -e 's|ghcr\.io/otquartz/|ghcr.io/qtclient/|g' \
      -e 's/otquartz/qtclient/g' \
      -e 's/idleot/qtclient/g' \
      -e 's|static\.idleot\.com|static.tibia.com|g' \
      "$f" 2>/dev/null || true
    rm -f "${f}.bak"
  done < <(rg -i 'idleot|otquartz' "$DEST" -l -0 \
    --glob '!**/NOTICE*' --glob '!**/LICENSE*' --glob '!**/LICENSES/**' \
    --glob '!**/third_party/**' --glob '!**/*.png' --glob '!**/*.ico' \
    --glob '!**/*.icns' --glob '!**/*.qm' --glob '!**/*.qsb' \
    --glob '!**/export-public-client.sh' --glob '!**/push-otquartz.sh' \
    --glob '!**/push-qtclient.sh' \
    2>/dev/null || true)
fi

# Refresh sync/extract/resolve from the umbrella monorepo when present so the
# public tree can `make sync` / `make extract` without tibiajs.
UMBRELLA="$(cd "$ROOT/../.." && pwd)"
if [[ -f "$UMBRELLA/tools/tibia_paths.py" ]]; then
  mkdir -p "$DEST/tools"
  cp "$UMBRELLA/tools/tibia_paths.py" "$DEST/tools/tibia_paths.py"
  rsync -a --delete --exclude '__pycache__' --exclude '*.pyc' \
    "$UMBRELLA/tools/tibia-sync/" "$DEST/tools/tibia-sync/"
  rsync -a --delete --exclude '__pycache__' --exclude '*.pyc' \
    "$UMBRELLA/tools/extract/" "$DEST/tools/extract/"
  rsync -a --delete --exclude '__pycache__' --exclude '*.pyc' \
    "$UMBRELLA/tools/re/" "$DEST/tools/re/"
  echo "vendored tools/{tibia-sync,extract,re,tibia_paths} from umbrella"
fi
# Ensure resolve-tibia + fetch-assets are executable
chmod +x "$DEST/scripts/resolve-tibia.sh" 2>/dev/null || true
chmod +x "$DEST/tools/fetch-assets/qtclient-fetch-assets" 2>/dev/null || true

# Public docs already prefer docs/public/*; keep release helper README at tools/
if [[ -f "$DEST/tools/fetch-assets/README.txt" ]]; then
  cp "$DEST/tools/fetch-assets/README.txt" "$DEST/RELEASE-ASSETS.txt"
fi

# Ship hybrid tip example for Releases
mkdir -p "$DEST/docs/public"
[[ -f "$ROOT/docs/public/tibiametadata.hybrid.json" ]] && \
  cp "$ROOT/docs/public/tibiametadata.hybrid.json" "$DEST/docs/public/"
[[ -f "$ROOT/docs/public/hybrid-tip.md" ]] && \
  cp "$ROOT/docs/public/hybrid-tip.md" "$DEST/docs/public/"

echo "Exported to $DEST"

# Structural gates (must not ship product islands / CIP includes).
if [[ -d "$DEST/client/mods/idleot" || -d "$DEST/launcher/mods/idleot" \
   || -d "$DEST/qml/idleot" || -d "$DEST/client/qml/idleot" || -d "$DEST/client/src/idleot" ]]; then
  echo "export dirty: product island directory still present" >&2
  exit 1
fi
if rg -n 'idleot::|#include\s*"idleot/' "$DEST/client/src" 2>/dev/null; then
  echo "export dirty: idleot types still in client/src" >&2
  exit 1
fi
echo "CIP namespace check: clean"

# Brand residue report (non-fatal warnings for maintainers).
if command -v rg >/dev/null; then
  leftover="$(rg -i 'idleot' "$DEST" \
    --glob '!**/NOTICE*' --glob '!**/LICENSE*' --glob '!**/LICENSES/**' \
    --glob '!**/third_party/**' --glob '!**/export-public-client.sh' \
    --glob '!**/push-otquartz.sh' -l 2>/dev/null || true)"
  if [[ -n "$leftover" ]]; then
    echo "warning: idleot token still in (review):" >&2
    echo "$leftover" >&2
  else
    echo "brand check: clean"
  fi
fi
