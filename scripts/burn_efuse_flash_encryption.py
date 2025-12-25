#!/usr/bin/env python3
"""
Burn flash encryption eFuse on ESP32 device (IRREVERSIBLE).

This script verifies signatures, checks device state, and burns the flash
encryption eFuse. Once burned, the device will only accept encrypted firmware.
If secure boot is enabled, firmware must be both signed and encrypted.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from burn_utils import (
    print_irreversible_warning,
    prompt_burn_confirmation,
    resolve_port,
    show_device_info,
    signing_key_path,
    validate_key_file,
    validate_pre_burn_checks,
    verify_device_accessible,
)


def burn_flash_encryption(
    port: str | None = None, key_path: Path | None = None
) -> None:
    """Verify signatures then burn flash encryption eFuse. This is IRREVERSIBLE!"""
    key = key_path or signing_key_path()

    # Validate key file early
    if not validate_key_file(key):
        raise SystemExit(1)

    # Resolve and validate port
    port = resolve_port(port)
    if not verify_device_accessible(port):
        raise SystemExit(
            f"❌ Device not accessible on {port}. Check connection."
        )

    # Run all pre-burn validation checks
    if not validate_pre_burn_checks(key):
        raise SystemExit("❌ Pre-burn validation checks failed. Aborting.")

    # Check device state
    _, flash_encryption, _ = show_device_info(port)
    if flash_encryption is True:
        print("🔐 Flash encryption already enabled; no burn needed.")
        sys.exit(0)
    if flash_encryption is None:
        print(
            "⚠️  Unable to determine flash encryption state; proceeding with caution."
        )

    # Final warnings and confirmation
    print_irreversible_warning(
        [
            "⚠️  WARNING: This will PERMANENTLY enable flash encryption on the device!",
            "⚠️  This operation is IRREVERSIBLE!",
            "⚠️  The device will ONLY accept encrypted (and signed, if secure boot) firmware!",
        ]
    )
    prompt_burn_confirmation()

    # Burn the eFuse
    cmd = [
        sys.executable,
        "-m",
        "espefuse",
        "--port",
        port,
        "burn_efuse",
        "FLASH_CRYPT_CNT",
    ]

    print(f"\n🔥 Burning flash encryption eFuse on {port}...\n")
    try:
        subprocess.check_call(cmd, timeout=30)
        print("\n✅ Flash encryption eFuse burned successfully!")
        print("⚠️  The device will now only accept encrypted firmware.")
    except subprocess.TimeoutExpired:
        raise SystemExit(
            "❌ Timeout while burning eFuse. Device may be in an uncertain state."
        )
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"❌ Failed to burn eFuse: {e}")
    except KeyboardInterrupt:
        print("\n\n❌ Interrupted during burn. Device state is uncertain.")
        raise SystemExit(1)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Burn flash encryption eFuse (IRREVERSIBLE!)",
        prog="espwifiSecure burn_efuse --fe",
    )
    parser.add_argument(
        "--port",
        default=None,
        help="Serial port (e.g., /dev/ttyUSB0 or /dev/cu.usbserial-0001). Auto-detected if not specified.",
    )
    parser.add_argument(
        "--key",
        default=None,
        help="Path to secure boot signing key (PEM). Defaults to value from SDK config (CONFIG_SECURE_BOOT_SIGNING_KEY) or esp32_secure_boot.pem.",
    )
    return parser.parse_args(argv or sys.argv[1:])


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    key_arg = Path(args.key) if args.key else None
    burn_flash_encryption(args.port, key_arg)


if __name__ == "__main__":
    main()
