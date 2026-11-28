#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I "$ROOT/esp" \
  "$ROOT/tests/native/scanner_qr_test.cpp" -o "$tmpdir/scanner_qr_test"
"$tmpdir/scanner_qr_test"
echo "scanner QR native regression: PASS"
