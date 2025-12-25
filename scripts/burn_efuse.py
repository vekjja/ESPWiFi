#!/usr/bin/env python3
"""
Burn secure boot eFuse on ESP32 device (IRREVERSIBLE!).

Usage:
  python burn_efuse.py [--port /dev/ttyUSB0]

Dependencies:
  pip install -r scripts/requirements.txt
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from itertools import chain

try:
    import glob
except ImportError:
    glob = None


def show_device_info(port: str) -> tuple[bool | None, bool | None]:
    """Display human-readable device information and return security state."""
    base_cmd = [sys.executable, "-m", "espefuse", "--port", port]
    secure_boot_enabled: bool | None = None
    flash_encryption: bool | None = None

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
            f"  Secure Boot:   {'🔐 ENABLED' if secure_boot_enabled else '🔓 Disabled'}"
        )
        print(
            f"  Flash Encryption: {'🔐 ENABLED' if flash_encryption else '🔓 Disabled'}"
        )
        print(f"{'=' * 60}\n")
        return secure_boot_enabled, flash_encryption

    except subprocess.CalledProcessError as e:
        print("❌ Device Could Not Be Queried:")
        print(f"   Error: {e}")
        raise SystemExit(1)


def burn_efuse(port: str | None = None) -> None:
    """Burn the secure boot eFuse. This is IRREVERSIBLE!"""
    if port is None:
        # Try to detect port from PlatformIO or common locations
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
            port = common_ports[0]
            print(f"⚠️  No port specified, using: {port}")
        else:
            raise SystemExit(
                "❌ No port specified and could not auto-detect. Use --port /dev/ttyUSB0"
            )

    # Show device info before burning
    secure_boot_enabled, flash_encryption = show_device_info(port)
    if secure_boot_enabled is False:
        print(
            "❌ Secure boot is disabled on this device; aborting eFuse burn."
        )
        print("Enable secure boot in firmware.")
        sys.exit(1)

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


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Burn secure boot eFuse (IRREVERSIBLE!)",
        prog="espwifiSecure.sh burn_efuse",
    )
    parser.add_argument(
        "--port",
        default=None,
        help="Serial port (e.g., /dev/ttyUSB0 or /dev/cu.usbserial-0001). Auto-detected if not specified.",
    )
    return parser.parse_args(argv or sys.argv[1:])


def main(argv: list[str] | None = None) -> None:
    args = parse_args(argv)
    burn_efuse(args.port)


if __name__ == "__main__":
    main()
