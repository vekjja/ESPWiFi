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


def _get_sdkconfig_path(env_name: str) -> Path | None:
    """Get the SDK config file path for the given environment from platformio.ini."""
    ini = PROJECT_DIR / "platformio.ini"
    if not ini.exists():
        return None

    try:
        content = ini.read_text()
        lines = content.splitlines()
        in_target_env = False
        i = 0

        while i < len(lines):
            line = lines[i]
            stripped = line.strip()
            # Check if we're in the target environment section
            if stripped == f"[env:{env_name}]":
                in_target_env = True
                i += 1
                continue
            # Stop if we hit another section
            if stripped.startswith("[") and in_target_env:
                break
            # Look for board_build.sdkconfig in the target environment
            if in_target_env and "board_build.sdkconfig" in stripped.lower():
                # Handle both inline values and multi-line values
                if "=" in stripped:
                    parts = stripped.split("=", 1)
                    value = parts[1].strip() if len(parts) > 1 else ""
                    # If value is empty, check next line (multi-line value)
                    if not value and i + 1 < len(lines):
                        value = lines[i + 1].strip()
                        i += 1
                    # Remove quotes if present
                    value = value.strip("\"'")
                    if value:
                        sdkconfig_path = PROJECT_DIR / value
                        if sdkconfig_path.exists():
                            return sdkconfig_path
            i += 1
    except Exception:
        pass
    return None


def _parse_sdkconfig_key_path(sdkconfig_path: Path) -> Path | None:
    """Parse CONFIG_SECURE_BOOT_SIGNING_KEY from SDK config file."""
    try:
        content = sdkconfig_path.read_text()
        for line in content.splitlines():
            stripped = line.strip()
            # Skip comments and empty lines
            if not stripped or stripped.startswith("#"):
                continue
            # Look for the signing key config
            if stripped.startswith("CONFIG_SECURE_BOOT_SIGNING_KEY="):
                # Extract the value (handle quotes)
                value = stripped.split("=", 1)[1].strip().strip("\"'")
                if value:
                    # If absolute path, use it directly; otherwise relative to PROJECT_DIR
                    key_path = Path(value)
                    if not key_path.is_absolute():
                        key_path = PROJECT_DIR / key_path
                    return key_path
    except Exception:
        pass
    return None


def signing_key_path() -> Path:
    """Return signing key path from SDK config, or default if not found."""
    env_name = os.environ.get("PIOENV", _default_env_name(PROJECT_DIR))
    sdkconfig_path = _get_sdkconfig_path(env_name)

    if sdkconfig_path:
        key_path = _parse_sdkconfig_key_path(sdkconfig_path)
        if key_path:
            return key_path

    # Fall back to default
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


def validate_key_file(key_path: Path) -> bool:
    """Validate that the key file exists, is readable, and appears to be a valid PEM file."""
    if not key_path.exists():
        print(f"❌ Signing key not found: {key_path}")
        return False

    if not os.access(key_path, os.R_OK):
        print(f"❌ Signing key file is not readable: {key_path}")
        return False

    # Check if it looks like a PEM file
    try:
        content = key_path.read_text()
        if "-----BEGIN" not in content or "-----END" not in content:
            print(
                f"❌ Key file does not appear to be a valid PEM file: {key_path}"
            )
            return False
    except Exception as e:
        print(f"❌ Failed to read key file: {key_path} ({e})")
        return False

    return True


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


def verify_binaries_exist() -> tuple[bool, list[str]]:
    """Check that required binaries exist. Returns (success, list of missing files)."""
    bootloader, firmware = get_binary_paths()
    missing = []
    if not bootloader.exists():
        missing.append(bootloader.name)
    if not firmware.exists():
        missing.append(firmware.name)
    return len(missing) == 0, missing


def verify_signed_binaries(key_path: Path) -> bool:
    """Verify bootloader/firmware signatures using espsecure."""
    bootloader, firmware = get_binary_paths()

    # Check binaries exist first
    exist, missing = verify_binaries_exist()
    if not exist:
        print(f"❌ Signed binaries not found (missing: {', '.join(missing)}).")
        print(f"   Expected location: {bootloader.parent}")
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
            print(f"🔏 Signature Verified: {path.name}")
            return True
        except subprocess.CalledProcessError as e:
            print(f"❌ Verification failed for {path.name}")
            if e.stdout:
                print(f"   {e.stdout.strip()}")
            if e.stderr:
                print(f"   {e.stderr.strip()}")
            return False
        except FileNotFoundError:
            print(
                "❌ espsecure not found. Install esptool/espsecure in your environment."
            )
            return False

    print(f"🗝️  Verifying signed binaries with key: {key_path.name}")
    boot_ok = _verify(bootloader)
    fw_ok = _verify(firmware)
    return boot_ok and fw_ok


def verify_binary_timestamps() -> bool:
    """Verify bootloader and firmware are from the same build (similar timestamps)."""
    bootloader, firmware = get_binary_paths()

    exist, missing = verify_binaries_exist()
    if not exist:
        print(f"   ❌ Binaries not found: {', '.join(missing)}")
        return False

    try:
        boot_mtime = bootloader.stat().st_mtime
        fw_mtime = firmware.stat().st_mtime
        time_diff = abs(boot_mtime - fw_mtime)

        # Allow up to 30 seconds difference (builds can take time)
        if time_diff > 30:
            print(
                f"   ⚠️  Bootloader and firmware have significantly different timestamps "
                f"({time_diff:.0f}s difference)."
            )
            print(
                "   They may be from different builds. Proceeding with caution."
            )
            return True  # Warn but don't fail

        print(
            f"🕰️  Binaries built within {time_diff:.0f}s of each other (30s tolerance)."
        )
        return True
    except OSError as e:
        print(f"   ❌ Failed to check binary timestamps: {e}")
        return False


def resolve_port(port: str | None) -> str:
    """Return a usable serial port, with best-effort auto-detection."""
    if port:
        # Validate that the provided port exists/accessible
        port_path = Path(port)
        if not port_path.exists():
            raise SystemExit(f"❌ Specified port does not exist: {port}")
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
        return chosen

    raise SystemExit(
        "❌ No port specified and could not auto-detect. Use --port /dev/ttyUSB0"
    )


def detect_chip_type(port: str) -> str | None:
    """Detect chip type from device and return in espefuse format (e.g., 'esp32c3')."""
    try:
        cmd = [sys.executable, "-m", "espefuse", "--port", port, "summary"]
        result = subprocess.run(
            cmd, capture_output=True, text=True, check=True, timeout=10
        )
        output = result.stdout.lower()

        # Map chip names to espefuse format
        chip_map = {
            "esp32-c3": "esp32c3",
            "esp32c3": "esp32c3",
            "esp32-s3": "esp32s3",
            "esp32s3": "esp32s3",
            "esp32": "esp32",
            "esp32-c6": "esp32c6",
            "esp32c6": "esp32c6",
        }

        # Look for chip type in output
        for line in result.stdout.split("\n"):
            line_lower = line.lower()
            if (
                "detecting chip type" in line_lower
                or "chip type" in line_lower
            ):
                # Extract chip type from line
                for chip_name, espefuse_name in chip_map.items():
                    if chip_name in line_lower:
                        return espefuse_name
                # Try to extract from "Chip type: ..." format
                if ":" in line:
                    chip_part = line.split(":")[-1].strip().lower()
                    for chip_name, espefuse_name in chip_map.items():
                        if chip_name in chip_part:
                            return espefuse_name

        return None
    except Exception:
        return None


def verify_device_accessible(port: str) -> bool:
    """Verify that a device is accessible on the given port."""
    try:
        cmd = [sys.executable, "-m", "espefuse", "--port", port, "summary"]
        result = subprocess.run(
            cmd, capture_output=True, text=True, check=True, timeout=10
        )
        return (
            "Detecting chip type" in result.stdout or "Chip" in result.stdout
        )
    except subprocess.TimeoutExpired:
        print(f"❌ Timeout connecting to device on {port}")
        return False
    except subprocess.CalledProcessError:
        return False
    except FileNotFoundError:
        print("❌ espefuse not found. Install esptool in your environment.")
        return False
    except Exception as e:
        print(f"❌ Error accessing device on {port}: {e}")
        return False


def print_irreversible_warning(lines: list[str]) -> None:
    """Display irreversible action warnings."""
    for line in lines:
        print(line)


def prompt_burn_confirmation() -> None:
    """Prompt the user for final confirmation before burning the eFuse."""
    try:
        response = input("🔥 Type 'BURN' to confirm (or Ctrl+C to cancel): ")
    except KeyboardInterrupt:
        print("\n\n💧 Cancelled by user.")
        sys.exit(0)

    if response != "BURN":
        print(f"💧 Skipping Burn. {response} != BURN 💧")
        sys.exit(1)


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
            chip_cmd, capture_output=True, text=True, check=True, timeout=10
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

    except subprocess.TimeoutExpired:
        print("❌ Timeout connecting to device.")
        raise SystemExit(1)
    except subprocess.CalledProcessError as e:
        print("❌ Device Could Not Be Queried:")
        print(f"   Error: {e}")
        if e.stderr:
            print(f"   stderr: {e.stderr.strip()}")
        raise SystemExit(1)


def validate_pre_burn_checks(key_path: Path) -> bool:
    """Run all pre-burn validation checks. Returns True if all checks pass."""
    print("\n🔍 Running pre-burn validation checks...\n")

    checks_passed = True

    # Check 1: Key file validation
    if not validate_key_file(key_path):
        checks_passed = False

    # Check 2: Signature verification
    if not verify_signed_binaries(key_path):
        checks_passed = False
    print()

    # Check 3: Binary timestamps (same build)
    if not verify_binary_timestamps():
        checks_passed = False
    print()

    if checks_passed:
        print("🎉 All pre-burn checks passed! 🎉\n")
    else:
        print(
            "❌ Some pre-burn checks failed. Please fix the issues before proceeding.\n"
        )

    return checks_passed
