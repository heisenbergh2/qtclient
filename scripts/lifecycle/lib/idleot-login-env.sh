# Deprecated: login URLs live in pack mod.json (env > mod > release_env > CipSoft).
# IdleOT: tip mods/idleot after make qtclient client install.
# Demo: tip mods/default (myAAC). --no-mods → www.tibia.com.
# QTCLIENT_LOGIN_* remains an escape hatch — do not source this file from start scripts.
#
# Optional tip/CDN helpers for local tooling only (not login paths):
# shellcheck shell=bash
export QTCLIENT_PORTAL_URL="${QTCLIENT_PORTAL_URL:-http://127.0.0.1:5173}"
export QTCLIENT_CDN_URL="${QTCLIENT_CDN_URL:-$QTCLIENT_PORTAL_URL/cdn}"
export QTCLIENT_ASSET_BASE="${QTCLIENT_ASSET_BASE:-$QTCLIENT_CDN_URL}"
