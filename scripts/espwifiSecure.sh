#!/usr/bin/env bash
# ESPWiFi Secure Boot Management Script
# Supports:
#   gen_key [--out /filepath] - Generate a secure boot signing key
#   burn_efuse [--port /dev/ttyUSB0] - Burn secure boot eFuse (IRREVERSIBLE)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="$ROOT_DIR/.venv"
REQ_FILE="$ROOT_DIR/scripts/requirements.txt"
PY_SCRIPT="$ROOT_DIR/scripts/espwifiSecure.py"

info()  { printf "ℹ️  %s\n" "$*"; }
ok()    { printf "✅ %s\n" "$*"; }
warn()  { printf "⚠️  %s\n" "$*" >&2; }
error() { printf "❌ %s\n" "$*" >&2; exit 1; }

find_python() {
  if command -v python3 >/dev/null 2>&1; then
    printf "python3\n"; return
  fi
  if command -v python >/dev/null 2>&1; then
    printf "python\n"; return
  fi
  error "python3/python not found. Please install Python 3."
}

ensure_venv() {
  local py_bin="$1"
  if [ ! -d "$VENV_DIR" ]; then
    info "Creating venv: $VENV_DIR"
    "$py_bin" -m venv "$VENV_DIR"
    ok "Venv created."
  else
    info "Using existing venv: $VENV_DIR"
  fi
}

install_requirements() {
  [ -f "$REQ_FILE" ] || error "Requirements file not found at '$REQ_FILE'"
  "$VENV_DIR/bin/pip" install --upgrade pip >/dev/null || error "Failed to upgrade pip"
  "$VENV_DIR/bin/pip" install -r "$REQ_FILE" >/dev/null || error "Failed to install requirements"
  ok "Dependencies ready."
}

usage() {
  cat <<EOF
Usage: $0 <command> [options]

Commands:
  gen_key [--out /filepath]     Generate a secure boot signing key
  burn_efuse [--port /dev/ttyUSB0]  Burn secure boot eFuse (IRREVERSIBLE!)

Examples:
  $0 gen_key
  $0 gen_key --out /path/to/key.pem
  $0 burn_efuse
  $0 burn_efuse --port /dev/cu.usbserial-0001
EOF
  exit 1
}

main() {
  if [ $# -eq 0 ]; then
    usage
  fi

  local command="$1"
  shift

  local py_bin
  py_bin="$(find_python)"

  ensure_venv "$py_bin"
  install_requirements

  printf "\n"

  case "$command" in
    gen_key)
      exec "$VENV_DIR/bin/python" "$PY_SCRIPT" gen_key "$@"
      ;;
    burn_efuse)
      exec "$VENV_DIR/bin/python" "$PY_SCRIPT" burn_efuse "$@"
      ;;
    *)
      error "Unknown command: $command"
      usage
      ;;
  esac
}

main "$@"

