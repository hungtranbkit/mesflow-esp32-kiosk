#!/usr/bin/env bash
# Explicit serial-only app release; shares the canonical compiler/verifier.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# == 1 ]] || { echo "Usage: $0 serial-profile-id" >&2; exit 1; }
exec bash "$ROOT/scripts/build-ota-release.sh" --serial-profile "$1"
