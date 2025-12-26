#!/usr/bin/env python3
"""
Custom upload script for ESP32 with Secure Boot enabled.
Uses --no-stub flag to work with Secure Download Mode.
"""
import subprocess
import sys
import glob
import os

# Get project root
project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
build_dir = os.path.join(project_dir, ".pio", "build", "esp32-c3")

# Auto-detect port
upload_port_pattern = "/dev/cu.usbmodem*"
ports = glob.glob(upload_port_pattern)

if not ports:
    print(f"Error: No port matching {upload_port_pattern} found")
    print("Please connect your ESP32 device")
    sys.exit(1)

upload_port = ports[0]
upload_speed = "460800"

print(f"Auto-detected port: {upload_port}")

# Check if signed binaries exist
bootloader = os.path.join(build_dir, "bootloader-signed.bin")
partitions = os.path.join(build_dir, "partitions-signed.bin")
firmware = os.path.join(build_dir, "firmware-signed.bin")

for binary in [bootloader, firmware]:
    if not os.path.exists(binary):
        print(f"Error: Signed binary not found: {binary}")
        print("Please run 'pio run -e esp32-c3' first to build the project")
        sys.exit(1)

# Build the esptool command
cmd = [
    "esptool",
    "--chip",
    "esp32c3",
    "--port",
    upload_port,
    "--baud",
    upload_speed,
    "--no-stub",
    "--after",
    "hard-reset",
    "write-flash",
    "0x0",
    bootloader,
    "0x10000",
    firmware,
]

print(f"Uploading with secure boot support...")
print(f"Command: {' '.join(cmd)}\n")

# Execute the command
result = subprocess.run(cmd)
sys.exit(result.returncode)
