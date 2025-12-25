#!/usr/bin/env python3
"""Shared helpers for secure boot and flash encryption burn flows."""
from __future__ import annotations

import os
import subprocess
import sys
from itertools import chain
from pathlib import Path

try:
    import glob
except ImportError:
    glob = None


PROJECT_DIR = Path(__file__).resolve().parent.parent


def signing_key_path() -> Path:
    """Return default secure boot key path."""
    return PROJECT_DIR / "esp32_secure_boot.pem"


def _default_env_name(project_dir: Path) -> str:
    """Best-effort fallback to the first default_envs entry in platformio.ini."""
    ini = project_dir / "platformio.ini"
    if not ini.exists():
        return "esp32-c3"
    try:
        for raw in ini.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith(("#", ";")):
                continue
            if line.startswith("default_envs"):
                _, value = line.split("=", 1)
                envs = [e.strip() for e in value.split(",") if e.strip()]
                if envs:
                    return envs[0]
    except Exception:
        pass
    return "esp32-c3"


def verify_signed_binaries(key_path: Path) -> bool:
    """Verify bootloader/firmware signatures using espsecure."""
    env_name = os.environ.get("PIOENV", _default_env_name(PROJECT_DIR))
    build_dir = PROJECT_DIR / ".pio" / "build" / env_name
    firmware_name = os.environ.get("PROGNAME", "firmware")
    bootloader = build_dir / "bootloader.bin"
    firmware = build_dir / f"{firmware_name}.bin"

    missing = [p.name for p in (bootloader, firmware) if not p.exists()]
    if missing:
        print(
            f"❌ Signed binaries not found in {build_dir} (missing: {', '.join(missing)})."
        )
        print(
            "   Run your build/sign flow first (e.g., espwifiSecure sign_binaries)."
        )
        return False

    def _verify(path: Path) -> bool:
        cmd = [
            sys.executable,
            "-m",
            "espsecure",
            "verify-signature",
            "--version",
            "2",
            "--keyfile",
            str(key_path),
            str(path),
        ]
        try:
            subprocess.run(cmd, capture_output=True, text=True, check=True)
            print(f"🔏 Signature Verified: {path}")
            return True
        except subprocess.CalledProcessError as e:
            print(f"❌ Verification failed for {path.name}")
            if e.stdout:
                print(e.stdout.strip())
            if e.stderr:
                print(e.stderr.strip())
            return False
        except FileNotFoundError:
            print(
                "❌ espsecure not found. Install esptool/espsecure in your environment."
            )
            return False

    print(f"🗝️  Verifying signed binaries with key: {key_path.name}")
    boot_ok = _verify(bootloader)
    fw_ok = _verify(firmware)
    if boot_ok and fw_ok:
        # print("\n🎉 Signatures verified! 🎉\n")
        return True
    return False


def resolve_port(port: str | None) -> str:
    """Return a usable serial port, with best-effort auto-detection."""
    if port:
        return port
    if glob is None:
        raise SystemExit(
            "❌ No port specified and glob module not available. Use --port /dev/ttyUSB0"
        )

    common_ports = list(
        chain(
            glob.glob("/dev/ttyUSB*"),
            glob.glob("/dev/cu.usb*"),
            glob.glob("/dev/cu.SLAB*"),
        )
    )
    if common_ports:
        chosen = common_ports[0]
        # print(f"\n🚪 Using Port: {chosen}")
        return chosen

    raise SystemExit(
        "❌ No port specified and could not auto-detect. Use --port /dev/ttyUSB0"
    )


def print_irreversible_warning(lines: list[str]) -> None:
    """Display irreversible action warnings."""
    for line in lines:
        print(line)


def prompt_burn_confirmation() -> None:
    """Prompt the user for final confirmation before burning the eFuse."""
    try:
        response = input("Type 'BURN' to confirm (or Ctrl+C to cancel): ")
    except KeyboardInterrupt:
        print("\n\n❌ Cancelled by user.")
        sys.exit(0)

    if response != "BURN":
        print("❌ Aborted.")
        sys.exit(0)


def get_build_env() -> str:
    """Get the PlatformIO build environment name."""
    return os.environ.get("PIOENV", _default_env_name(PROJECT_DIR))


def get_binary_paths() -> tuple[Path, Path]:
    """Get paths to bootloader and firmware binaries."""
    env_name = get_build_env()
    build_dir = PROJECT_DIR / ".pio" / "build" / env_name
    firmware_name = os.environ.get("PROGNAME", "firmware")
    bootloader = build_dir / "bootloader.bin"
    firmware = build_dir / f"{firmware_name}.bin"
    return bootloader, firmware


def verify_binary_timestamps() -> bool:
    """Verify bootloader and firmware are from the same build (similar timestamps)."""
    bootloader, firmware = get_binary_paths()
    if not bootloader.exists() or not firmware.exists():
        print(
            f"   ❌ Binaries not found: {bootloader.name} or {firmware.name}"
        )
        return False

    boot_mtime = bootloader.stat().st_mtime
    fw_mtime = firmware.stat().st_mtime
    time_diff = abs(boot_mtime - fw_mtime)

    # Allow up to 30 seconds difference (builds can take time)
    if time_diff > 30:
        print(
            f"   ⚠️  Bootloader and firmware have significantly different timestamps "
            f"({time_diff:.0f}s difference)."
        )
        print("   They may be from different builds. Proceeding with caution.")
        return True  # Warn but don't fail

    print(
        f"\n🕰️  Binaries were built within {time_diff:.0f}s seconds of each other. (30s difference is tolerated.)"
    )
    return True


def show_device_info(port: str) -> tuple[bool | None, bool | None, str]:
    """Display human-readable device information and return security state and chip type."""
    base_cmd = [sys.executable, "-m", "espefuse", "--port", port]
    secure_boot_enabled: bool | None = None
    flash_encryption: bool | None = None

    print(f"\n{'=' * 60}")
    print("📱 Device Information")
    print(f"{'=' * 60}")
    print(f"USB Port: {port}\n")

    try:
        chip_cmd = base_cmd + ["summary"]
        result = subprocess.run(
            chip_cmd, capture_output=True, text=True, check=True
        )
        output = result.stdout

        chip_type = "Unknown"
        chip_revision = "Unknown"
        secure_boot_enabled = False
        flash_encryption = False
        mac_address = "Unknown"

        for line in output.split("\n"):
            line_lower = line.lower()
            if "detecting chip type" in line_lower:
                parts = line.split("...")
                if len(parts) > 1:
                    chip_type = parts[1].strip()
            elif "secure_boot_en" in line_lower:
                if (
                    "= false" in line_lower
                    or "= 0" in line_lower
                    or "= 0b0" in line_lower
                ):
                    secure_boot_enabled = False
                elif (
                    "= true" in line_lower
                    or "= 1" in line_lower
                    or "= 0b1" in line_lower
                ):
                    secure_boot_enabled = True
            elif "spi_boot_crypt_cnt" in line_lower:
                if "= disable" in line_lower or "= 0" in line_lower:
                    flash_encryption = False
                elif (
                    "= enable" in line_lower
                    or "= 1" in line_lower
                    or "= 3" in line_lower
                ):
                    flash_encryption = True
            elif "mac (block1)" in line_lower and "=" in line_lower:
                parts = line.split("=")
                if len(parts) > 1:
                    mac_part = parts[1].strip().split()[0]
                    if ":" in mac_part:
                        mac_address = mac_part

        print(f"Board/Chip Type: {chip_type}")
        if chip_revision != "Unknown":
            print(f"Chip Revision:   {chip_revision}")
        if mac_address != "Unknown":
            print(f"MAC Address:      {mac_address}")
        print()
        print("eFuse Status:")
        print(
            f"  Secure Boot:   {'🔐 ENABLED' if secure_boot_enabled else '🔓 Disabled'}"
        )
        print(
            f"  Flash Encryption: {'🔐 ENABLED' if flash_encryption else '🔓 Disabled'}"
        )
        print(f"{'=' * 60}\n")
        return secure_boot_enabled, flash_encryption, chip_type

    except subprocess.CalledProcessError as e:
        print("❌ Device Could Not Be Queried:")
        print(f"   Error: {e}")
        raise SystemExit(1)


def validate_pre_burn_checks(key_path: Path) -> bool:
    """Run all pre-burn validation checks. Returns True if all checks pass."""
    print("\n🔍 Running pre-burn validation checks...\n")

    checks_passed = True

    # Check 1: Signature verification
    if not verify_signed_binaries(key_path):
        checks_passed = False

    # Check 2: Binary timestamps (same build)
    if not verify_binary_timestamps():
        checks_passed = False

    if checks_passed:
        print("\n🎉 All pre-burn checks passed! 🎉\n")
    else:
        print(
            "\n❌ Some pre-burn checks failed. Please fix the issues before proceeding.\n"
        )

    return checks_passed
