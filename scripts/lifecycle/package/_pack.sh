#!/usr/bin/env bash
# shellcheck shell=bash
# Shared pack_* helpers (HOST_ID, OTC must be set).
pack_sdk() {
  local DEST="$1"
  rm -rf "$DEST"
  mkdir -p "$DEST/include/qtclient" "$DEST/cmake" "$DEST/client/tibia" "$DEST/cipgen"

  cp -a "$OTC/include/qtclient/." "$DEST/include/qtclient/"
  rsync -a --include '*/' --include '*.h' --include '*.hpp' --exclude '*' \
    "$OTC/client/tibia/" "$DEST/client/tibia/"
  mkdir -p "$DEST/include"
  rsync -a --include '*/' --include '*.h' --include '*.hpp' --exclude '*' \
    "$OTC/client/tibia/" "$DEST/include/"

  cp -a "$OTC/cmake/QtClientMod.cmake" "$DEST/cmake/"
  cp -a "$OTC/cmake/QtClientHost.cmake" "$DEST/cmake/"
  cp -a "$OTC/cmake/TibiaMods.cmake" "$DEST/cmake/" 2>/dev/null || true
  cp -a "$OTC/cmake/GeneratePackQmldir.cmake" "$DEST/cmake/"
  cp -a "$OTC/cmake/QtClientConfig.cmake.in" "$DEST/cmake/" 2>/dev/null || true
  # Linked tip: FS presence + tip merge helpers (no host_exports / SIDE_MODULE).
  mkdir -p "$DEST/scripts/lifecycle/lib" "$DEST/cmake"
  cp -a "$OTC/scripts/lifecycle/lib/check-tip-pack-presence.sh" "$DEST/scripts/lifecycle/lib/"
  cp -a "$OTC/scripts/lifecycle/lib/merge-pack-into-tip.sh" "$DEST/scripts/lifecycle/lib/"

  python3 "$OTC/tools/cip/gen_cpp.py" \
    "$OTC/client/upstream/contract" \
    "$DEST/cipgen" \
    --chunks 12 \
    --qml "$OTC/client/upstream/qml/qmlcomponents"
  test -f "$DEST/cipgen/cip_gen.h"

  # CIP protobuf C++ stubs for out-of-tree packs (#include <appearances.pb.h>).
  # Source tree only has .proto; Soft host cmake generates into the build dir.
  _proto_dir="$OTC/client/upstream/proto"
  _proto_out="$DEST/include/protobuf"
  mkdir -p "$_proto_out"
  _protoc=""
  if command -v protoc >/dev/null 2>&1; then
    _protoc="$(command -v protoc)"
  elif [[ -n "${VCPKG_ROOT:-}" ]]; then
    for _cand in \
      "$VCPKG_ROOT/installed/${VCPKG_TARGET_TRIPLET:-}/tools/protobuf/protoc" \
      "$VCPKG_ROOT/installed/"*/tools/protobuf/protoc
    do
      [[ -x "$_cand" ]] && _protoc="$_cand" && break
    done
  fi
  if [[ -z "$_protoc" ]]; then
    echo "[package] warning: protoc missing — client-sdk will lack appearances.pb.h" >&2
  else
    "$_protoc" -I"$_proto_dir" --cpp_out="$_proto_out" \
      appearances.proto shared.proto sounds.proto sounds-common.proto
    test -f "$_proto_out/appearances.pb.h"
    # Mirror under client/tibia/protobuf for Soft host-shaped include layouts.
    mkdir -p "$DEST/client/tibia/protobuf"
    cp -a "$_proto_out/." "$DEST/client/tibia/protobuf/"
  fi

  cat >"$DEST/cmake/QtClientConfig.cmake" <<EOF
# qtclient client-sdk (hostBuildId=${HOST_ID})
set(QTCLIENT_ROOT "\${CMAKE_CURRENT_LIST_DIR}/..")
set(QTCLIENT_HOST_BUILD_ID "${HOST_ID}")
set(TIBIA_ROOT "\${QTCLIENT_ROOT}" CACHE PATH "qtclient SDK root" FORCE)
list(APPEND CMAKE_MODULE_PATH "\${QTCLIENT_ROOT}/cmake")
include("\${QTCLIENT_ROOT}/cmake/QtClientMod.cmake")
if(NOT TARGET QtClient::Host)
  add_library(QtClient::Host INTERFACE IMPORTED)
  set_target_properties(QtClient::Host PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "\${QTCLIENT_ROOT}/include;\${QTCLIENT_ROOT}/include/engine;\${QTCLIENT_ROOT}/cipgen;\${QTCLIENT_ROOT}/client/tibia;\${QTCLIENT_ROOT}/client/tibia/engine")
endif()
set(QtClient_FOUND TRUE)
EOF

  cat >"$DEST/client-sdk.json" <<EOF
{
  "hostBuildId": "${HOST_ID}",
  "schema": 1,
  "builtAt": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
  cp -f "$DEST/client-sdk.json" "$DEST/host-sdk.json"
  echo "[package] sdk hostBuildId=${HOST_ID} → ${DEST}"
}

pack_desktop() {
  local PLATFORM="$1" BIN="$2" DEST="$3"
  export QTCLIENT_APP_NAME="${QTCLIENT_APP_NAME:-qtclient}"
  export QTCLIENT_BUNDLE_ID="${QTCLIENT_BUNDLE_ID:-com.qtclient.client}"
  export QTCLIENT_STORE_META="${QTCLIENT_STORE_META:-.qtclient}"

  rm -rf "$DEST"
  mkdir -p "$DEST/bin" "$DEST/engine"

  python3 "$OTC/tools/publish/package_desktop.py" \
    --platform "$PLATFORM" \
    --bin "$BIN" \
    --engine-bundle "$DEST/engine"

  for name in client launcher; do
    if [[ -f "$BIN/$name" ]]; then
      cp -a "$BIN/$name" "$DEST/bin/$name"
    elif [[ -f "$BIN/$name.exe" ]]; then
      cp -a "$BIN/$name.exe" "$DEST/bin/$name.exe"
    elif [[ -d "$BIN/$name.app" ]]; then
      cp -a "$BIN/$name.app" "$DEST/bin/$name.app"
    fi
  done

  cat >"$DEST/host.json" <<EOF
{
  "hostBuildId": "${HOST_ID}",
  "platform": "${PLATFORM}",
  "schema": 1,
  "builtAt": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
  echo "[package] desktop ${PLATFORM} hostBuildId=${HOST_ID} → ${DEST}"
}

pack_wasm() {
  local SRC="$1" DEST="$2"
  export TIBIA_ENGINE_SHA="$HOST_ID"
  rm -rf "$DEST"
  mkdir -p "$DEST"
  bash "$OTC/scripts/lifecycle/package/pack-tibia-wasm.sh" "$SRC" "$DEST"
  cat >"$DEST/host.json" <<EOF
{
  "hostBuildId": "${HOST_ID}",
  "platform": "wasm",
  "schema": 1,
  "builtAt": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
  echo "[package] wasm hostBuildId=${HOST_ID} → ${DEST}"
}

