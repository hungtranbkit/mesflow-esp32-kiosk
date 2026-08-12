#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

grep -RhoE '^[[:space:]]*#include[[:space:]]*[<"][^>"]+[>"]' "$ROOT" \
  --include='*.ino' --include='*.h' --include='*.hpp' --include='*.cpp' \
  2>/dev/null | sed -E 's/^[[:space:]]*//' | sort -u || true
