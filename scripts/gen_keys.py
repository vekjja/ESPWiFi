#!/usr/bin/env python3
"""
Generate ESP32 secure boot signing key.

Usage:
  python gen_keys.py [--out /filepath]

Dependencies:
  pip install -r scripts/requirements.txt
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


def generate_key(out_path: Path, version: int = 2) -> None:
    if out_path.exists():
        raise SystemExit(
            f"❌ Key already exists at '{out_path}'. Delete it or choose a new --out path."
        )

    out_path.parent.mkdir(parents=True, exist_ok=True)

    base_cmd = [sys.executable, "-m", "espsecure"]
    cmd_new = base_cmd + [
        "generate-signing-key",
        "--version",
        str(version),
        str(out_path),
    ]
    cmd_old = base_cmd + [
        "generate_signing_key",
        "--version",
        str(version),
        str(out_path),
    ]

    print(f"🚀 Generating secure boot v{version} signing key: '{out_path}'\n")
    try:
        subprocess.check_call(cmd_new, stdout=subprocess.DEVNULL)
    except subprocess.CalledProcessError:
        subprocess.check_call(cmd_old, stdout=subprocess.DEVNULL)
    os.chmod(out_path, 0o600)
    print(
        "🎉 Done. Keep this key safe and backed up; secure boot fuses are irreversible❗️\n"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate ESP32 secure boot v2 signing key",
        prog="espwifiSecure.sh gen_key",
    )
    parser.add_argument(
        "--out",
        default="esp32_secure_boot.pem",
        help="Output key path (default: esp32_secure_boot.pem)",
    )
    return parser.parse_args(argv or sys.argv[1:])


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    out_path = Path(args.out).expanduser().resolve()
    generate_key(out_path)


if __name__ == "__main__":
    main()
