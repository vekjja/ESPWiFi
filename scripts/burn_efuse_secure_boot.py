#!/usr/bin/env python3
"""
Burn secure boot eFuse on ESP32 device (IRREVERSIBLE).

This script verifies signatures, checks device state, and burns the secure boot
eFuse. Once burned, the device will only accept firmware signed with the
matching key.
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


def burn_secure_boot(
    port: str | None = None, key_path: Path | None = None
) -> None:
    """Verify signatures then burn the secure boot eFuse. This is IRREVERSIBLE!"""
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
    secure_boot_enabled, _, _ = show_device_info(port)
    if secure_boot_enabled is True:
        print("🔐 Secure boot already enabled; no burn needed.")
        sys.exit(0)
    if secure_boot_enabled is None:
        print(
            "⚠️  Unable to determine secure boot state; proceeding with caution."
        )

    # Final warnings and confirmation
    print_irreversible_warning(
        [
            "⚠️  WARNING: This will PERMANENTLY enable secure boot on the device!",
            "⚠️  This operation is IRREVERSIBLE!",
            "⚠️  The device will ONLY accept signed firmware!",
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
        "SECURE_BOOT_EN",
    ]

    print(f"\n🔥 Burning secure boot eFuse on {port}...\n")
    try:
        subprocess.check_call(cmd, timeout=30)
        print("\n✅ Secure boot eFuse burned successfully!")
        print("⚠️  The device will now only accept signed firmware.")
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
        description="Burn secure boot eFuse (IRREVERSIBLE!)",
        prog="espwifiSecure burn_efuse --sb",
    )
    parser.add_argument(
        "--port",
        default=None,
        help="Serial port (e.g., /dev/ttyUSB0 or /dev/cu.usbserial-0001). Auto-detected if not specified.",
    )
    parser.add_argument(
        "--key",
        default=None,
        help="Path to secure boot signing key (PEM). Defaults to esp32_secure_boot.pem at repo root.",
    )
    return parser.parse_args(argv or sys.argv[1:])


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    key_arg = Path(args.key) if args.key else None
    burn_secure_boot(args.port, key_arg)


if __name__ == "__main__":
    main()
