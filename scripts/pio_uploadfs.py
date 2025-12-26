Import("env")
import subprocess
import sys
import glob
from pathlib import Path


def upload_filesystem(*args, **kwargs):
    """Custom uploadfs target for LittleFS with ESP-IDF"""

    # Get project paths
    project_dir = Path(env.subst("$PROJECT_DIR"))
    build_dir = Path(env.subst("$BUILD_DIR"))
    data_dir = project_dir / "data"
    littlefs_bin = build_dir / "littlefs.bin"

    # LittleFS partition info (from partitions.csv)
    littlefs_offset = "0x210000"
    littlefs_size = 0x1E0000  # 1.88 MB

    # Get upload port - try auto-detect if not set
    upload_port = env.subst("$UPLOAD_PORT")
    if not upload_port or "*" in str(upload_port):
        # Auto-detect port
        patterns = [
            "/dev/cu.usbmodem*",
            "/dev/cu.usbserial*",
            "/dev/ttyUSB*",
            "/dev/ttyACM*",
        ]
        for pattern in patterns:
            matches = glob.glob(pattern)
            if matches:
                upload_port = matches[0]
                break

    if not upload_port:
        print("Error: No upload port found!")
        print("Please connect your ESP32-C3 device")
        sys.exit(1)

    print(f"Auto-detected port: {upload_port}")

    # Check if data directory exists
    if not data_dir.exists():
        print(f"Error: Data directory not found: {data_dir}")
        sys.exit(1)

    # Create build directory if it doesn't exist
    build_dir.mkdir(parents=True, exist_ok=True)

    print(f"Building LittleFS image from {data_dir}...")
    print(f"Output: {littlefs_bin}")

    # Find mklittlefs tool
    mklittlefs = None
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
    ]

    for path in possible_paths:
        if path.exists():
            mklittlefs = str(path)
            break

    if not mklittlefs:
        print("Error: mklittlefs tool not found!")
        print("Install with: pio pkg install --tool tool-mklittlefs")
        sys.exit(1)

    # Build LittleFS image
    build_cmd = [
        mklittlefs,
        "-c",
        str(data_dir),
        "-s",
        str(littlefs_size),
        "-b",
        "4096",
        "-p",
        "256",
        str(littlefs_bin),
    ]

    print(f"Running: {' '.join(build_cmd)}")
    result = subprocess.run(build_cmd)
    if result.returncode != 0:
        print("Error building LittleFS image!")
        sys.exit(1)

    print(f"\nLittleFS image created: {littlefs_bin}")
    print(f"Size: {littlefs_bin.stat().st_size} bytes")

    # Upload using esptool
    upload_cmd = [
        "esptool",
        "--chip",
        "esp32c3",
        "--port",
        upload_port,
        "--baud",
        "460800",
        "--no-stub",
        "--after",
        "hard-reset",
        "write-flash",
        littlefs_offset,
        str(littlefs_bin),
    ]

    print(f"\nUploading LittleFS to {upload_port}...")
    print(f"Command: {' '.join(upload_cmd)}\n")

    result = subprocess.run(upload_cmd)
    if result.returncode != 0:
        print("\n❌ Upload failed!")
        sys.exit(1)

    print("\n✅ LittleFS filesystem uploaded successfully!")


# Register custom uploadlittlefs target
env.AddCustomTarget(
    name="uploadlittlefs",
    dependencies=None,
    actions=upload_filesystem,
    title="Upload LittleFS",
    description="Build and upload LittleFS filesystem",
)
