#!/usr/bin/env python3
import os
import subprocess
import sys
import shutil
from pathlib import Path

# Try to import PlatformIO's SCons env when running as an extra_script.
try:
    from SCons.Script import Import  # type: ignore

    Import("env")  # provided by PlatformIO
    HAS_SCONS = True
except ModuleNotFoundError:
    env = None
    HAS_SCONS = False


def _default_env_name(project_dir: Path) -> str:
    """Best-effort fallback to the first default_envs entry in platformio.ini."""
    ini = project_dir / "platformio.ini"
    if not ini.exists():
        return "esp32-s3"
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
    return "esp32-s3"


PROJECT_DIR = (
    Path(env["PROJECT_DIR"])
    if HAS_SCONS
    else Path(__file__).resolve().parent.parent
)
PIOENV = (
    env["PIOENV"]
    if HAS_SCONS
    else os.environ.get("PIOENV", _default_env_name(PROJECT_DIR))
)

SIGNING_KEY = PROJECT_DIR / "esp32_secure_boot.pem"


def find_espsecure_script() -> Path | None:
    """Locate espsecure.py path (PlatformIO package preferred, else python package)."""
    if HAS_SCONS:
        try:
            esptool_dir = Path(
                env.PioPlatform().get_package_dir("tool-esptoolpy")
            )
            script = esptool_dir / "espsecure.py"
            if script.exists():
                return script
        except Exception:
            pass

    try:
        import espsecure  # type: ignore

        return Path(espsecure.__file__).resolve()
    except Exception:
        pass

    which = shutil.which("espsecure.py") or shutil.which("espsecure")
    if which:
        return Path(which)
    return None


ESPSECURE = find_espsecure_script()
PYTHON_EXE = sys.executable


def sign_binary(binary_path: Path, key_path: Path) -> bool:
    """Sign a binary file using espsecure.py"""
    if not binary_path.exists():
        print(f"⚠️  Binary not found: {binary_path}")
        return False

    if not key_path.exists():
        print(f"❌ Signing key not found: {key_path}")
        return False

    key_file = str(key_path.resolve())
    binary_file = str(binary_path.resolve())

    cmd = [
        str(PYTHON_EXE),
        str(ESPSECURE),
        "sign_data",
        "--version",
        "2",
        "--keyfile",
        key_file,
        "--",
        binary_file,
    ]

    # print("Running:", " ".join(cmd))

    try:
        result = subprocess.run(
            cmd,
            stdout=subprocess.DEVNULL,  # suppress normal output
            stderr=subprocess.PIPE,  # capture only errors
            text=True,
            check=True,
        )
        if result.stderr:
            print(result.stderr)
        print(f"🔐 Signed: {binary_path}\n")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to sign {binary_path.name}: {e}")
        if e.stderr:
            print("   stderr:\n", e.stderr)
        return False
    except FileNotFoundError:
        print(f"❌ espsecure.py not found at {ESPSECURE}")
        return False


def _standalone_sign():
    """Allow running this script directly (outside SCons) via espwifiSecure.sh."""
    if ESPSECURE is None:
        print(
            "❌ espsecure.py not found. Install esptool (espsecure) or run via PlatformIO."
        )
        sys.exit(1)

    build_dir = PROJECT_DIR / ".pio" / "build" / PIOENV
    firmware_name = os.environ.get("PROGNAME", "firmware")

    bootloader = build_dir / "bootloader.bin"
    firmware = build_dir / f"{firmware_name}.bin"

    if not bootloader.exists() and not firmware.exists():
        print(
            f"⚠️  No build artifacts found in {build_dir}. Run 'pio run' first."
        )
        sys.exit(1)

    if bootloader.exists():
        print("\n🔏 Signing Bootloader")
        sign_binary(bootloader, SIGNING_KEY)

    if firmware.exists():
        print("\n🔏 Signing Firmware")
        if not sign_binary(firmware, SIGNING_KEY):
            print("❌ Secure boot signing failed\n")
            sys.exit(1)
        print("\n🎉 Secure Boot Signing Complete 🎉\n")


def post_sign_bootloader(target, source, env):
    bootloader = Path(str(target[0]))
    print("\n🔏 Signing Bootloader")
    if SIGNING_KEY.exists():
        sign_binary(bootloader, SIGNING_KEY)
    else:
        print(f"⚠️  Signing key not found: {SIGNING_KEY}")
        print("   Skipping secure boot signing.")


def post_sign_firmware(target, source, env):
    firmware = Path(str(target[0]))
    print("\n🔏 Signing Firmware")
    if SIGNING_KEY.exists():
        if sign_binary(firmware, SIGNING_KEY):
            print("🎉 Secure Boot Signing Complete 🎉\n")
        else:
            print("❌ Secure boot signing failed\n")
            sys.exit(1)
    else:
        print(f"⚠️  Signing key not found: {SIGNING_KEY}")
        print("   Skipping secure boot signing.")


def post_sign_partitions(target, source, env):
    partitions = Path(str(source[0]))
    print(f"\n🔐 (optional) Signing partitions: {partitions.name}")
    # If you ever want to sign partitions, call sign_binary here
    pass


if HAS_SCONS:
    env.AddPostAction("$BUILD_DIR/bootloader.bin", post_sign_bootloader)
    firmware_name = env.get("PROGNAME", "firmware")
    env.AddPostAction(f"$BUILD_DIR/{firmware_name}.bin", post_sign_firmware)
    print("🔐🪝 ESPWiFi Secure Boot Signing Hooks Registered")
else:
    if __name__ == "__main__":
        _standalone_sign()
    else:
        # Allow import without executing when not under SCons
        pass
