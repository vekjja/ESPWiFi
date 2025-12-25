#!/usr/bin/env bash
# Create/use a local Python venv, install requirements, and invoke the
# secure-boot key generator Python script. Args are forwarded to the Python
# script (e.g., --out path.pem --force).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_DIR="$ROOT_DIR/.venv"
REQ_FILE="$ROOT_DIR/scripts/requirements.txt"
PY_SCRIPT="$ROOT_DIR/scripts/generate_sb_keys.py"

error() {
  printf "error: %s\n" "$1" >&2
  exit 1
}

find_python() {
  if command -v python3 >/dev/null 2>&1; then
    printf "python3\n"
    return
  fi
  if command -v python >/dev/null 2>&1; then
    printf "python\n"
    return
  fi
  error "python3/python not found. Install Python 3."
}

ensure_venv() {
  local py_bin="$1"
  if [ ! -d "$VENV_DIR" ]; then
    "$py_bin" -m venv "$VENV_DIR"
  fi
}

install_requirements() {
  if [ ! -f "$REQ_FILE" ]; then
    error "requirements file not found at '$REQ_FILE'"
  fi
  "$VENV_DIR/bin/pip" install --upgrade pip >/dev/null
  "$VENV_DIR/bin/pip" install -r "$REQ_FILE"
}

main() {
  local py_bin
  py_bin="$(find_python)"

  ensure_venv "$py_bin"
  install_requirements

  exec "$VENV_DIR/bin/python" "$PY_SCRIPT" "$@"
}

main "$@"

