#!/usr/bin/env python3
"""
Burn secure boot eFuse on ESP32 device (IRREVERSIBLE).

This script verifies signatures, checks device state, and burns the secure boot
eFuse. Once burned, the device will only accept firmware signed with the
matching key.
"""
from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

from burn_utils import (
    detect_chip_type,
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

    # Detect chip type
    chip_type = detect_chip_type(port)
    if not chip_type:
        raise SystemExit(
            f"❌ Could not detect chip type from device on {port}. "
            "Please ensure device is connected and accessible."
        )

    # Run all pre-burn validation checks
    if not validate_pre_burn_checks(key):
        raise SystemExit("❌ Pre-burn validation checks failed. Aborting.")

    # Check device state
    secure_boot_enabled, _, _ = show_device_info(port)

    # Check if key is already burned
    try:
        check_key_cmd = [
            sys.executable,
            "-m",
            "espefuse",
            "--port",
            port,
            "--chip",
            chip_type,
            "dump_blocks",
            "BLOCK_KEY0",
        ]
        key_result = subprocess.run(
            check_key_cmd, capture_output=True, text=True, timeout=10
        )
        # Look for non-zero hex words in BLOCK_KEY0 output
        # Extract all 8-character hex words (32-bit values) from the output
        import re

        hex_words = re.findall(r"\b([0-9a-f]{8})\b", key_result.stdout.lower())
        # Check if any hex word is non-zero (not all zeros)
        has_key = False
        if hex_words:
            # For secure boot v2, we need at least one non-zero word
            # First 8 words (32 bytes) should contain the SHA256 digest
            for word in hex_words[:8]:  # Check first 8 words (32 bytes)
                if word != "00000000":
                    has_key = True
                    break
    except Exception:
        has_key = None  # Can't determine, proceed with caution

    if secure_boot_enabled is True and has_key is True:
        print("🔐 Secure boot already fully enabled with key; no burn needed.")
        sys.exit(0)
    elif secure_boot_enabled is True and has_key is False:
        print("⚠️  WARNING: SECURE_BOOT_EN is enabled but key block is empty!")
        print("   This is an invalid state. We will burn the key now.")
    elif secure_boot_enabled is None:
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

    # For ESP32-C3 secure boot v2, we need to:
    # 1. Burn the public key hash into BLOCK_KEY0
    # 2. Set KEY_PURPOSE_0 to SECURE_BOOT_DIGEST (if not already set)
    # 3. Burn SECURE_BOOT_EN

    print(f"\n🔥 Burning secure boot key and eFuses on {port}...\n")

    # Step 1: Extract public key digest from PEM file
    # For ESP32-C3 secure boot v2, we need to extract the SHA256 digest
    # of the public key (32 bytes) and burn it as raw binary
    print("📝 Step 1: Extracting public key digest...")
    try:
        # Extract public key in DER format
        pubkey_result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(key),
                "-pubout",
                "-outform",
                "DER",
            ],
            capture_output=True,
            check=True,
        )
        # Calculate SHA256 digest (32 bytes) - this is what ESP32 stores
        digest_bytes = hashlib.sha256(pubkey_result.stdout).digest()
        print("✅ Public key digest extracted")
    except subprocess.CalledProcessError as e:
        raise SystemExit(
            f"❌ Failed to extract public key digest: {e.stderr.decode('utf-8', errors='ignore') if e.stderr else e}"
        )
    except FileNotFoundError:
        raise SystemExit(
            "❌ openssl not found. Install openssl to extract public key digest."
        )

    # Step 2: Burn the digest into BLOCK_KEY0
    # Write digest to temporary file for burning
    print("📝 Step 2: Burning public key digest into BLOCK_KEY0...")
    with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as tmp_file:
        tmp_path = Path(tmp_file.name)
        tmp_file.write(digest_bytes)

    try:
        burn_key_cmd = [
            sys.executable,
            "-m",
            "espefuse",
            "--port",
            port,
            "--chip",
            chip_type,
            "burn-key",
            "BLOCK_KEY0",
            str(tmp_path),
            "SECURE_BOOT_DIGEST0",
        ]
        # Automatically confirm the espefuse prompt since we already confirmed in our script
        subprocess.run(
            burn_key_cmd,
            input="BURN\n",
            text=True,
            timeout=30,
            check=True,
        )
        print("🔑 Public key digest burned successfully")
    except subprocess.CalledProcessError as e:
        error_output = (
            e.stderr.decode("utf-8", errors="ignore")
            if hasattr(e, "stderr") and e.stderr
            else str(e)
        )
        # Check if key is already burned or block is write-protected
        if (
            "already" in error_output.lower()
            or "protected" in error_output.lower()
        ):
            print("⚠️  Key block may already be burned or protected")
            print(f"   Error: {error_output}")
            raise SystemExit(
                "❌ Cannot burn key - block already written or protected"
            )
        else:
            raise SystemExit(
                f"❌ Failed to burn public key digest: {error_output}"
            )
    finally:
        # Clean up temp file
        if tmp_path.exists():
            tmp_path.unlink()

    # Step 3: Burn SECURE_BOOT_EN (if not already enabled)
    print("\n📝 Step 3: Enabling secure boot...")
    burn_sb_cmd = [
        sys.executable,
        "-m",
        "espefuse",
        "--port",
        port,
        "--chip",
        chip_type,
        "burn_efuse",
        "SECURE_BOOT_EN",
    ]
    try:
        subprocess.check_call(burn_sb_cmd, timeout=30)
        print("🔐 Secure boot enabled successfully")
    except subprocess.CalledProcessError as e:
        # If already enabled, that's okay
        error_msg = (
            e.stderr.decode("utf-8", errors="ignore")
            if hasattr(e, "stderr") and e.stderr
            else str(e)
        )
        if "already" in str(e).lower() or "already" in error_msg.lower():
            print("🔐 Secure boot already enabled")
        else:
            raise SystemExit(f"❌ Failed to enable secure boot: {e}")

    print("\n🎉 Secure boot setup complete! 🎉")
    print("⚠️  The device will now only accept firmware signed with this key.")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Burn secure boot eFuse (IRREVERSIBLE!)",
        prog="espwifiSecure burn_efuse --sb",
    )
    parser.add_argument(
        "--port",
        default=None,
        help=(
            "Serial port (e.g., /dev/ttyUSB0 or /dev/cu.usbserial-0001). "
            "Auto-detected if not specified."
        ),
    )
    parser.add_argument(
        "--key",
        default=None,
        help=(
            "Path to secure boot signing key (PEM). "
            "Defaults to value from SDK config (CONFIG_SECURE_BOOT_SIGNING_KEY) "
            "or esp32_secure_boot.pem."
        ),
    )
    return parser.parse_args(argv or sys.argv[1:])


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    key_arg = Path(args.key) if args.key else None
    burn_secure_boot(args.port, key_arg)


if __name__ == "__main__":
    main()
