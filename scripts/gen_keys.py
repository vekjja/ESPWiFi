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


def extract_public_key(
    private_key_path: Path, public_key_path: Path | None = None
) -> Path:
    """Extract public key from private key PEM file."""
    if public_key_path is None:
        public_key_path = (
            private_key_path.parent / f"{private_key_path.stem}.pub.pem"
        )

    if not private_key_path.exists():
        raise SystemExit(f"❌ Private key not found: {private_key_path}")

    try:
        # Extract public key using openssl
        result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(private_key_path),
                "-pubout",
                "-out",
                str(public_key_path),
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        print(f"✅ Public key extracted: {public_key_path}")
        return public_key_path
    except subprocess.CalledProcessError as e:
        raise SystemExit(
            f"❌ Failed to extract public key: {e.stderr if e.stderr else e}"
        )
    except FileNotFoundError:
        raise SystemExit(
            "❌ openssl not found. Install openssl to extract public key."
        )


def generate_key(
    out_path: Path, version: int = 2, extract_pub: bool = True
) -> None:
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

    # Extract public key for inclusion in releases
    if extract_pub:
        try:
            extract_public_key(out_path)
        except SystemExit as e:
            print(f"⚠️  {e}")
            print(
                "   You can extract it later with: openssl pkey -in <key> -pubout -out <key>.pub.pem"
            )

    print(
        "🎉 Done. Keep this key safe and backed up; secure boot fuses are irreversible❗️\n"
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate ESP32 secure boot v2 signing key",
        prog="espwifiSecure gen_key",
    )
    parser.add_argument(
        "--out",
        default="esp32_secure_boot.pem",
        help="Output key path (default: esp32_secure_boot.pem)",
    )
    parser.add_argument(
        "--extract-pub",
        action="store_true",
        default=True,
        help="Extract public key after generation (default: True)",
    )
    parser.add_argument(
        "--no-extract-pub",
        dest="extract_pub",
        action="store_false",
        help="Don't extract public key",
    )
    parser.add_argument(
        "--extract-only",
        metavar="PRIVATE_KEY",
        help="Extract public key from existing private key (do not generate new key)",
    )
    return parser.parse_args(argv or sys.argv[1:])


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)

    if args.extract_only:
        # Extract public key from existing private key
        private_key_path = Path(args.extract_only).expanduser().resolve()
        extract_public_key(private_key_path)
    else:
        # Generate new key
        out_path = Path(args.out).expanduser().resolve()
        generate_key(out_path, extract_pub=args.extract_pub)


if __name__ == "__main__":
    main()
