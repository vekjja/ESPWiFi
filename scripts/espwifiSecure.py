#!/usr/bin/env python3
"""
ESPWiFi Secure Boot Management Tool

Commands:
  gen_key [--out /filepath] - Generate a secure boot signing key
  burn_efuse [--port /dev/ttyUSB0] - Burn secure boot eFuse (IRREVERSIBLE!)

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


def show_device_info(port: str) -> None:
    """Display human-readable device information."""
    base_cmd = [sys.executable, "-m", "espefuse", "--port", port]

    print(f"\n{'=' * 60}")
    print("📱 Device Information")
    print(f"{'=' * 60}")
    print(f"USB Port: {port}\n")

    try:
        # Get chip info
        chip_cmd = base_cmd + ["summary"]
        result = subprocess.run(
            chip_cmd, capture_output=True, text=True, check=True
        )
        output = result.stdout

        # Extract chip type and revision
        chip_type = "Unknown"
        chip_revision = "Unknown"
        secure_boot_enabled = False
        flash_encryption = False
        mac_address = "Unknown"

        for line in output.split("\n"):
            line_lower = line.lower()
            # Parse "Detecting chip type... ESP32-S3"
            if "detecting chip type" in line_lower:
                parts = line.split("...")
                if len(parts) > 1:
                    chip_type = parts[1].strip()
            # Parse "SECURE_BOOT_EN (BLOCK0) ... = False"
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
            # Parse "SPI_BOOT_CRYPT_CNT ... = Disable" or "= Enable"
            elif "spi_boot_crypt_cnt" in line_lower:
                if "= disable" in line_lower or "= 0" in line_lower:
                    flash_encryption = False
                elif (
                    "= enable" in line_lower
                    or "= 1" in line_lower
                    or "= 3" in line_lower
                ):
                    flash_encryption = True
            # Parse MAC address
            elif "mac (block1)" in line_lower and "=" in line_lower:
                # Format: "MAC (BLOCK1) ... = 8c:bf:ea:8f:5a:90 (OK)"
                parts = line.split("=")
                if len(parts) > 1:
                    mac_part = parts[1].strip().split()[0]
                    if ":" in mac_part:
                        mac_address = mac_part

        # Display formatted info
        print(f"Board/Chip Type: {chip_type}")
        if chip_revision != "Unknown":
            print(f"Chip Revision:   {chip_revision}")
        if mac_address != "Unknown":
            print(f"MAC Address:      {mac_address}")
        print()
        print("eFuse Status:")
        print(
            f"  Secure Boot:   {'✅ ENABLED' if secure_boot_enabled else '❌ Disabled'}"
        )
        print(
            f"  Flash Encryption: {'✅ ENABLED' if flash_encryption else '❌ Disabled'}"
        )
        print(f"{'=' * 60}\n")

    except subprocess.CalledProcessError as e:
        print("⚠️  Warning: Could not read device info")
        print(f"   Error: {e}")
        print("   Make sure the device is connected and in download mode")
        print("   Continuing anyway...\n")


def burn_efuse(port: str | None = None) -> None:
    """Burn the secure boot eFuse. This is IRREVERSIBLE!"""
    if port is None:
        # Try to detect port from PlatformIO or common locations
        import glob
        from itertools import chain

        common_ports = list(
            chain(
                glob.glob("/dev/ttyUSB*"),
                glob.glob("/dev/cu.usb*"),
                glob.glob("/dev/cu.SLAB*"),
            )
        )
        if common_ports:
            port = common_ports[0]
            print(f"⚠️  No port specified, using: {port}")
        else:
            raise SystemExit(
                "❌ No port specified and could not auto-detect. Use --port /dev/ttyUSB0"
            )

    # Show device info before burning
    show_device_info(port)

    print(
        "\n⚠️  WARNING: This will PERMANENTLY enable secure boot on the device!"
    )
    print("⚠️  This operation is IRREVERSIBLE!")
    print("⚠️  The device will ONLY accept firmware signed with your key!")

    try:
        response = input("Type 'BURN' to confirm (or Ctrl+C to cancel): ")
        if response != "BURN":
            print("❌ Aborted.")
            sys.exit(0)
    except KeyboardInterrupt:
        print("\n\n❌ Cancelled by user.")
        sys.exit(0)

    base_cmd = [sys.executable, "-m", "espefuse"]
    cmd = base_cmd + [
        "--port",
        port,
        "burn_efuse",
        "SECURE_BOOT_EN",
    ]

    print(f"\n🔥 Burning secure boot eFuse on {port}...\n")
    try:
        subprocess.check_call(cmd)
        print("\n✅ Secure boot eFuse burned successfully!")
        print("⚠️  The device will now only accept signed firmware.")
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"❌ Failed to burn eFuse: {e}")


def parse_gen_key_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate ESP32 secure boot v2 signing key",
        prog="espwifiSecure.sh gen_key",
    )
    parser.add_argument(
        "--out",
        default="esp32_secure_boot.pem",
        help="Output key path (default: esp32_secure_boot.pem)",
    )
    return parser.parse_args(argv)


def parse_burn_efuse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Burn secure boot eFuse (IRREVERSIBLE!)",
        prog="espwifiSecure.sh burn_efuse",
    )
    parser.add_argument(
        "--port",
        default=None,
        help="Serial port (e.g., /dev/ttyUSB0 or /dev/cu.usbserial-0001). Auto-detected if not specified.",
    )
    return parser.parse_args(argv)


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: espwifiSecure.sh <command> [options]")
        print("Commands: gen_key, burn_efuse")
        sys.exit(1)

    command = sys.argv[1]
    args_rest = sys.argv[2:]

    if command == "gen_key":
        args = parse_gen_key_args(args_rest)
        out_path = Path(args.out).expanduser().resolve()
        generate_key(out_path)
    elif command == "burn_efuse":
        args = parse_burn_efuse_args(args_rest)
        burn_efuse(args.port)
    else:
        print(f"❌ Unknown command: {command}")
        print("Commands: gen_key, burn_efuse")
        sys.exit(1)


if __name__ == "__main__":
    main()
