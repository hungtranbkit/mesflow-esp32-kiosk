#!/usr/bin/env bash
set -Eeuo pipefail

log(){ printf '\n[Arduino Setup] %s\n' "$*"; }
die(){ echo "[ERROR] $*" >&2; exit 1; }

if ! command -v curl >/dev/null 2>&1; then
  die "Thiếu curl. Cài: sudo apt install curl"
fi

if ! command -v arduino-cli >/dev/null 2>&1; then
  log "Arduino CLI chưa có."
  echo "Cài theo installer chính thức của Arduino CLI."
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
    | BINDIR="$HOME/.local/bin" sh
  export PATH="$HOME/.local/bin:$PATH"
fi

command -v arduino-cli >/dev/null 2>&1 || die "Cài Arduino CLI thất bại hoặc PATH chưa có ~/.local/bin"

log "Arduino CLI"
arduino-cli version

CFG="$(arduino-cli config dump 2>/dev/null || true)"
if [[ -z "$CFG" ]]; then
  arduino-cli config init
fi

ESP_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"

if ! arduino-cli config dump | grep -Fq "$ESP_URL"; then
  log "Thêm ESP32 Boards Manager URL"
  arduino-cli config add board_manager.additional_urls "$ESP_URL"
fi

log "Update indexes"
arduino-cli core update-index

if ! arduino-cli core list | awk '{print $1}' | grep -qx 'esp32:esp32'; then
  log "Cài ESP32 Arduino core"
  arduino-cli core install esp32:esp32
else
  log "ESP32 core đã có"
  arduino-cli core list | grep '^esp32:esp32' || true
fi

log "Board hiện đang kết nối"
arduino-cli board list || true

echo
echo "Nếu Ubuntu báo permission serial, chạy:"
echo "  sudo usermod -aG dialout \"$USER\""
echo "sau đó logout/login lại."
