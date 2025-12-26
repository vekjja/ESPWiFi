#!/usr/bin/env python3
"""
Custom upload script for ESP32-C3 with Secure Boot enabled.
Uses --no-stub flag to work with Secure Download Mode.
Also builds and uploads LittleFS partition from data directory.
"""
import subprocess
import sys
import glob
import os
from pathlib import Path

# Get project root
project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
build_dir = os.path.join(project_dir, ".pio", "build", "esp32-c3")
data_dir = os.path.join(project_dir, "data")
littlefs_bin = os.path.join(build_dir, "littlefs.bin")

# Partition info (from partitions.csv)
LITTLEFS_OFFSET = "0x210000"
LITTLEFS_SIZE = "0x1e0000"  # 1.88 MB


def find_mklittlefs():
    """Find the mklittlefs tool in PlatformIO packages"""
    possible_paths = [
        Path.home()
        / ".platformio"
        / "packages"
        / "tool-mklittlefs"
        / "mklittlefs",
        Path.home()
        / ".platformio"
        / "packages"
        / "tool-mklittlefs-riscv32-esp"
        / "mklittlefs",
        Path(build_dir).parent.parent
        / "packages"
        / "tool-mklittlefs"
        / "mklittlefs",
    ]

    for path in possible_paths:
        if path.exists():
            return str(path)

    return None


def build_littlefs():
    """Build LittleFS image from data directory"""
    if not os.path.exists(data_dir):
        print(f"Warning: Data directory not found: {data_dir}")
        print("Skipping LittleFS build")
        return False

    print(f"\nBuilding LittleFS image from: {data_dir}")

    # Find mklittlefs tool
    mklittlefs = find_mklittlefs()
    if not mklittlefs:
        print("Warning: mklittlefs tool not found!")
        print("Attempting to install...")
        result = subprocess.run(
            [
                "pio",
                "pkg",
                "install",
                "--tool",
                "tool-mklittlefs",
                "-e",
                "esp32-c3",
            ]
        )
        if result.returncode != 0:
            print("Error: Could not install mklittlefs tool")
            return False

        mklittlefs = find_mklittlefs()
        if not mklittlefs:
            print("Error: Still could not find mklittlefs tool")
            return False

    print(f"Using mklittlefs: {mklittlefs}")

    # Create build directory if it doesn't exist
    os.makedirs(build_dir, exist_ok=True)

    # Build LittleFS image
    cmd = [
        mklittlefs,
        "-c",
        data_dir,
        "-s",
        str(int(LITTLEFS_SIZE, 16)),
        "-b",
        "4096",
        "-p",
        "256",
        littlefs_bin,
    ]

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd)

    if result.returncode != 0:
        print("Error: Failed to build LittleFS image")
        return False

    print(f"LittleFS image created: {littlefs_bin}")
    print(f"Size: {os.path.getsize(littlefs_bin)} bytes")
    return True


# Auto-detect port
upload_port_pattern = "/dev/cu.usbmodem*"
ports = glob.glob(upload_port_pattern)

if not ports:
    print(f"Error: No port matching {upload_port_pattern} found")
    print("Please connect your ESP32-C3 device")
    sys.exit(1)

upload_port = ports[0]
upload_speed = "460800"

print(f"Auto-detected port: {upload_port}")

# Check if binaries exist
bootloader = os.path.join(build_dir, "bootloader.bin")
partitions = os.path.join(build_dir, "partitions.bin")
firmware = os.path.join(build_dir, "firmware.bin")

for binary in [bootloader, partitions, firmware]:
    if not os.path.exists(binary):
        print(f"Error: Binary not found: {binary}")
        print("Please run 'pio run -e esp32-c3' first to build the project")
        sys.exit(1)

# Build LittleFS image
littlefs_available = build_littlefs()

# Build the esptool command
cmd = [
    "esptool",
    "--chip",
    "esp32c3",
    "--port",
    upload_port,
    "--baud",
    upload_speed,
    "--before",
    "default_reset",
    "--after",
    "no_reset",
    "--no-stub",
    "write-flash",
    "--flash_mode",
    "dio",
    "--flash_freq",
    "80m",
    "--flash_size",
    "2MB",
    "0x0",
    bootloader,
    "0x8000",
    partitions,
    "0x10000",
    firmware,
]

# Add LittleFS partition if available
if littlefs_available and os.path.exists(littlefs_bin):
    cmd.extend([LITTLEFS_OFFSET, littlefs_bin])
    print(f"Uploading with secure boot support and LittleFS partition...")
else:
    print(f"Uploading with secure boot support (no LittleFS)...")

print(f"Command: {' '.join(cmd)}\n")

# Execute the command
result = subprocess.run(cmd)

# Manual reset after successful upload
if result.returncode == 0:
    print("\n✅ Upload successful!")
    print(
        "Please manually reset your ESP32-C3 device to run the new firmware."
    )
else:
    print("\n❌ Upload failed!")

sys.exit(result.returncode)
