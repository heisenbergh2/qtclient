#!/usr/bin/env bash
# Build Soft wasm tip (optional QTCLIENT_LINKED_PACK → STATIC product).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec bash "$HERE/desktop.sh" --wasm "$@"
