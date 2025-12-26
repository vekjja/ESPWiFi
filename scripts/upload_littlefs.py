#!/usr/bin/env python3
"""
Script to build and upload LittleFS filesystem for ESP32 with ESP-IDF framework
"""
import subprocess
import sys
import os
import glob
from pathlib import Path

# Get project root directory
PROJECT_DIR = Path(__file__).parent.parent.absolute()
DATA_DIR = PROJECT_DIR / "data"
BUILD_DIR = PROJECT_DIR / ".pio" / "build" / "esp32-c3"
LITTLEFS_BIN = BUILD_DIR / "littlefs.bin"

# Partition info (from partitions.csv)
LITTLEFS_OFFSET = "0x210000"  # From partitions.csv
LITTLEFS_SIZE = "0x1e0000"  # 1.88 MB (1966080 bytes)


def run_command(cmd, description):
    """Run a command and handle errors"""
    print(f"\n{description}...")
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr}")
        sys.exit(1)
    if result.stdout:
        print(result.stdout)
    return result


def find_upload_port():
    """Find the USB serial port for the ESP32 device"""
    # Check environment variable first
    env_port = os.environ.get("UPLOAD_PORT")
    if env_port and not "*" in env_port:
        if os.path.exists(env_port):
            return env_port

    # Try common patterns
    patterns = [
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
    ]

    for pattern in patterns:
        matches = glob.glob(pattern)
        if matches:
            # Return the first match
            return matches[0]

    return None


def find_mklittlefs():
    """Find the mklittlefs tool in PlatformIO packages"""
    # Try to find mklittlefs in common locations
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
        BUILD_DIR.parent.parent
        / "packages"
        / "tool-mklittlefs"
        / "mklittlefs",
    ]

    for path in possible_paths:
        if path.exists():
            return str(path)

    # Try to find it via pio command
    try:
        result = subprocess.run(
            ["pio", "pkg", "list", "--only-packages", "-e", "esp32-c3"],
            capture_output=True,
            text=True,
        )
        if "tool-mklittlefs" in result.stdout:
            # Found it, try default location
            return "mklittlefs"
    except:
        pass

    return None


def main():
    print("=" * 60)
    print("ESP32 LittleFS Filesystem Upload")
    print("=" * 60)

    # Check if data directory exists
    if not DATA_DIR.exists():
        print(f"Error: Data directory not found: {DATA_DIR}")
        sys.exit(1)

    print(f"Data directory: {DATA_DIR}")
    print(f"Files to upload:")
    for item in DATA_DIR.rglob("*"):
        if item.is_file():
            print(f"  - {item.relative_to(DATA_DIR)}")

    # Find mklittlefs tool
    mklittlefs = find_mklittlefs()
    if not mklittlefs:
        print("\nWarning: mklittlefs tool not found!")
        print("Attempting to install...")
        run_command(
            [
                "pio",
                "pkg",
                "install",
                "--tool",
                "tool-mklittlefs",
                "-e",
                "esp32-c3",
            ],
            "Installing mklittlefs tool",
        )
        mklittlefs = find_mklittlefs()
        if not mklittlefs:
            print("Error: Could not find or install mklittlefs tool")
            sys.exit(1)

    print(f"Using mklittlefs: {mklittlefs}")

    # Create build directory if it doesn't exist
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    # Build LittleFS image
    run_command(
        [
            mklittlefs,
            "-c",
            str(DATA_DIR),
            "-s",
            str(int(LITTLEFS_SIZE, 16)),
            "-b",
            "4096",
            "-p",
            "256",
            str(LITTLEFS_BIN),
        ],
        "Building LittleFS image",
    )

    print(f"\nLittleFS image created: {LITTLEFS_BIN}")
    print(f"Size: {LITTLEFS_BIN.stat().st_size} bytes")

    # Find upload port
    upload_port = find_upload_port()
    if not upload_port:
        print("\nError: Could not find ESP32 device!")
        print("Please ensure the device is connected via USB.")
        print("You can also set the UPLOAD_PORT environment variable.")
        sys.exit(1)

    print(f"Using port: {upload_port}")
    print(
        "\n⚠️  IMPORTANT: Close any serial monitors (pio device monitor) before continuing!"
    )
    print("Press Enter to continue or Ctrl+C to cancel...")
    try:
        input()
    except KeyboardInterrupt:
        print("\nUpload cancelled.")
        sys.exit(0)

    # Upload filesystem using esptool
    # Using --no-stub for compatibility with secure boot (matches upload_secure.py)
    cmd = [
        "esptool",
        "--chip",
        "esp32c3",
        "--port",
        upload_port,
        "--baud",
        "460800",
        "--no-stub",
        "write-flash",  # Using hyphen (not underscore) to match upload_secure.py
        LITTLEFS_OFFSET,
        str(LITTLEFS_BIN),
    ]

    print("\nUploading LittleFS to device...")
    print(f"Command: {' '.join(cmd)}\n")

    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("\n❌ Upload failed!")
        print("\nTroubleshooting:")
        print(
            "1. Make sure no serial monitor is running (check: ps aux | grep monitor)"
        )
        print("2. Try unplugging and replugging the USB cable")
        print("3. Try manually entering bootloader mode:")
        print("   - Hold BOOT button")
        print("   - Press and release RESET button")
        print("   - Release BOOT button")
        print("   - Run this script again immediately")
        sys.exit(1)

    print("\n" + "=" * 60)
    print("✅ LittleFS filesystem uploaded successfully!")
    print("=" * 60)


if __name__ == "__main__":
    main()
