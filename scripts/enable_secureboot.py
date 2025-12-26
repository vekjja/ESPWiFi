#!/usr/bin/env python3
"""
Complete secure boot enablement workflow.

This script orchestrates the entire secure boot process:
1. Validates environment and key
2. Builds firmware
3. Signs binaries
4. Validates signed binaries
5. Uploads signed binaries to device
6. Validates device state
7. Burns secure boot efuse (with confirmation)
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Add scripts to path for imports
PROJECT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_DIR / "scripts"))

from burn_utils import (
    detect_chip_type,
    get_binary_paths,
    get_build_env,
    print_irreversible_warning,
    prompt_burn_confirmation,
    resolve_port,
    show_device_info,
    signing_key_path,
    validate_key_file,
    verify_binaries_exist,
    verify_device_accessible,
    verify_signed_binaries,
    verify_binary_timestamps,
)

PROJECT_DIR = Path(__file__).resolve().parent.parent


def check_dependencies() -> None:
    """Check that required tools are available."""
    print("🔍 Checking dependencies...")
    # Check PlatformIO
    try:
        subprocess.run(
            ["pio", "--version"], capture_output=True, check=True, timeout=5
        )
    except (
        subprocess.CalledProcessError,
        FileNotFoundError,
        subprocess.TimeoutExpired,
    ):
        raise SystemExit(
            "❌ PlatformIO (pio) not found. Install PlatformIO first."
        )

    # Python is available if we're running this script
    print("✅ All dependencies available\n")


def build_firmware(env_name: str) -> None:
    """Build the firmware using PlatformIO."""
    print(f"🔨 Building firmware (environment: {env_name})...")
    env = os.environ.copy()
    env["PIOENV"] = env_name
    try:
        subprocess.check_call(
            ["pio", "run", "-e", env_name],
            cwd=PROJECT_DIR,
            env=env,
            timeout=300,  # 5 minute timeout
        )
        print("✅ Firmware built successfully\n")
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"❌ Build failed: {e}")
    except subprocess.TimeoutExpired:
        raise SystemExit("❌ Build timeout (exceeded 5 minutes)")


def sign_binaries(key_path: Path, env_name: str) -> None:
    """Sign the bootloader and firmware binaries."""
    print("✍️  Signing binaries...")
    sign_script = PROJECT_DIR / "scripts" / "sign_binaries.py"
    env = os.environ.copy()
    env["PIOENV"] = env_name
    try:
        subprocess.check_call(
            [sys.executable, str(sign_script)],
            cwd=PROJECT_DIR,
            env=env,
            timeout=30,
        )
        print("✅ Binaries signed successfully\n")
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"❌ Signing failed: {e}")
    except subprocess.TimeoutExpired:
        raise SystemExit("❌ Signing timeout")


def validate_binaries(key_path: Path) -> None:
    """Validate that binaries exist, are signed correctly, and from same build."""
    print("🔍 Validating binaries...\n")

    # Check binaries exist
    exist, missing = verify_binaries_exist()
    if not exist:
        raise SystemExit(f"❌ Missing binaries: {', '.join(missing)}")

    # Verify signatures
    if not verify_signed_binaries(key_path):
        raise SystemExit("❌ Binary signature verification failed")

    # Check timestamps
    if not verify_binary_timestamps():
        raise SystemExit("❌ Binary timestamp check failed")

    print("✅ All binary validations passed\n")


def upload_binaries(port: str, env_name: str) -> None:
    """Upload signed binaries to the device."""
    print(f"📤 Uploading signed binaries to {port}...")
    env = os.environ.copy()
    env["PIOENV"] = env_name
    try:
        subprocess.check_call(
            ["pio", "run", "-t", "upload", "-e", env_name],
            cwd=PROJECT_DIR,
            env=env,
            timeout=60,
        )
        print("✅ Binaries uploaded successfully\n")
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"❌ Upload failed: {e}")
    except subprocess.TimeoutExpired:
        raise SystemExit("❌ Upload timeout")


def burn_secure_boot_efuse(port: str, chip_type: str, key_path: Path) -> None:
    """Burn the secure boot efuse (irreversible operation)."""
    print(f"\n{'=' * 60}")
    print("🔐 FINAL SECURE BOOT EFUSE BURN")
    print(f"{'=' * 60}\n")

    # Show device info one more time
    secure_boot_enabled, flash_encryption, _ = show_device_info(port)

    if secure_boot_enabled is True:
        print("✅ Secure boot already enabled. Nothing to do.")
        return

    # Final warnings
    print_irreversible_warning(
        [
            "⚠️  WARNING: This will PERMANENTLY enable secure boot!",
            "⚠️  This operation is IRREVERSIBLE!",
            "⚠️  The device will ONLY accept firmware signed with this key!",
            "⚠️  Make sure you have uploaded the signed firmware FIRST!",
        ]
    )

    prompt_burn_confirmation()

    # Import and call the burn function
    from burn_efuse_secure_boot import burn_secure_boot

    # Call the burn function
    burn_secure_boot(port=port, key_path=key_path)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Complete secure boot enablement workflow"
    )
    parser.add_argument(
        "--port",
        help="Serial port (auto-detected if not provided)",
    )
    parser.add_argument(
        "--key",
        type=Path,
        help="Path to signing key (default: from sdkconfig)",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip building firmware (use existing binaries)",
    )
    parser.add_argument(
        "--skip-upload",
        action="store_true",
        help="Skip uploading firmware (already uploaded)",
    )
    args = parser.parse_args()

    print(f"\n{'=' * 60}")
    print("🔐 SECURE BOOT ENABLEMENT WORKFLOW")
    print(f"{'=' * 60}\n")

    # Step 1: Check dependencies
    check_dependencies()

    # Step 2: Get signing key
    key_path = args.key or signing_key_path()
    if not validate_key_file(key_path):
        raise SystemExit(1)
    print(f"🔑 Using signing key: {key_path}\n")

    # Step 3: Resolve and verify port
    port = resolve_port(args.port)
    if not verify_device_accessible(port):
        raise SystemExit(f"❌ Device not accessible on {port}")
    print(f"📱 Device port: {port}\n")

    # Step 4: Detect chip type
    chip_type = detect_chip_type(port)
    if not chip_type:
        raise SystemExit("❌ Could not detect chip type")
    print(f"🔧 Chip type: {chip_type}\n")

    # Step 5: Get PlatformIO environment
    env_name = get_build_env()
    print(f"📦 PlatformIO environment: {env_name}\n")

    # Step 6: Build firmware (if not skipped)
    if not args.skip_build:
        build_firmware(env_name)
    else:
        print("⏭️  Skipping build (--skip-build)\n")

    # Step 7: Sign binaries
    sign_binaries(key_path, env_name)

    # Step 8: Validate binaries
    validate_binaries(key_path)

    # Step 9: Upload binaries (if not skipped)
    if not args.skip_upload:
        upload_binaries(port, env_name)
    else:
        print("⏭️  Skipping upload (--skip-upload)\n")

    # Step 10: Final validation before burn
    print(f"\n{'=' * 60}")
    print("🔍 FINAL PRE-BURN VALIDATION")
    print(f"{'=' * 60}\n")

    # Validate binaries one more time
    validate_binaries(key_path)

    # Show device info
    show_device_info(port)

    # Step 11: Burn secure boot efuse
    burn_secure_boot_efuse(port, chip_type, key_path)

    print(f"\n{'=' * 60}")
    print("✅ SECURE BOOT ENABLEMENT COMPLETE!")
    print(f"{'=' * 60}\n")
    print("⚠️  The device will now only accept firmware signed with this key.")
    print("⚠️  Keep your signing key safe and secure!")


if __name__ == "__main__":
    main()
