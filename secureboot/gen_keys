#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# PlatformIO venv provides espsecure after: pip install esptool
export PATH="${HOME}/.platformio/penv/bin:${PATH}"

if ! command -v espsecure >/dev/null 2>&1; then
  echo "espsecure not found. Install esptool into PlatformIO's venv:" >&2
  echo "  \"\${HOME}/.platformio/penv/bin/pip\" install esptool" >&2
  exit 1
fi

cd "$SCRIPT_DIR"
KEY_DIR="keys"
mkdir -p "$KEY_DIR"

# RSA2048 (common on ESP32)
espsecure generate-signing-key --version 2 "$KEY_DIR/rsa2048_signing_key.pem"

# ECDSA256 (e.g. ESP32-C3 secure boot v2)
espsecure generate-signing-key --version 2 --scheme ecdsa256 "$KEY_DIR/ecdsa256_signing_key.pem"
