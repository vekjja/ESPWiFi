#!/usr/bin/env python3
import os
import subprocess
import sys
import shutil
import tempfile
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


def get_signing_key_path(project_dir: Path, pioenv: str) -> Path:
    """Get the signing key path from sdkconfig file."""
    # Try to find sdkconfig in various locations
    sdkconfig_paths = [
        project_dir / "sdkconfig",
        project_dir / ".pio" / "build" / pioenv / "sdkconfig",
    ]

    # Also check board_build.sdkconfig from platformio.ini
    try:
        if HAS_SCONS:
            board_sdkconfig = env.BoardConfig().get("build.sdkconfig", "")
            if board_sdkconfig:
                sdkconfig_paths.insert(0, project_dir / board_sdkconfig)
    except Exception:
        pass

    # Try to read from sdkconfig files
    for sdkconfig_path in sdkconfig_paths:
        if not sdkconfig_path.exists():
            continue

        try:
            with open(sdkconfig_path, "r") as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("CONFIG_SECURE_BOOT_SIGNING_KEY="):
                        # Extract the value (remove quotes if present)
                        key_path = (
                            line.split("=", 1)[1].strip().strip('"').strip("'")
                        )
                        # Resolve relative paths from project directory
                        if os.path.isabs(key_path):
                            return Path(key_path)
                        else:
                            return project_dir / key_path
        except Exception:
            continue

    # Fallback to default if not found in sdkconfig
    default_key = project_dir / "esp32_secure_boot.pem"
    print(
        f"⚠️  Could not find CONFIG_SECURE_BOOT_SIGNING_KEY in sdkconfig, using default: {default_key}"
    )
    return default_key


def get_signing_key() -> Path:
    """Get and validate the signing key path."""
    key_path = get_signing_key_path(PROJECT_DIR, PIOENV)

    # Validate the key exists and is a valid PEM file
    if not key_path.exists():
        print(f"❌ Signing key not found: {key_path}")
        print(
            "   Please generate a key or update CONFIG_SECURE_BOOT_SIGNING_KEY in sdkconfig"
        )
        return key_path  # Return anyway so error message is clear

    # Validate it's a PEM file
    try:
        with open(key_path, "r") as f:
            content = f.read()
            if (
                "BEGIN RSA PRIVATE KEY" not in content
                and "BEGIN PRIVATE KEY" not in content
            ):
                print(
                    f"⚠️  Warning: {key_path} does not appear to be a valid private key PEM file"
                )
    except Exception as e:
        print(f"⚠️  Warning: Could not read key file: {e}")

    return key_path


SIGNING_KEY = get_signing_key()


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


def verify_signature(binary_path: Path, key_path: Path) -> bool:
    """Verify that a binary was signed with the given key."""
    if not binary_path.exists():
        print(f"⚠️  Binary not found: {binary_path}")
        return False

    if not key_path.exists():
        print(f"❌ Signing key not found: {key_path}")
        return False

    if ESPSECURE is None:
        print("❌ espsecure.py not found, cannot verify signature")
        return False

    key_file = str(key_path.resolve())
    binary_file = str(binary_path.resolve())

    # Extract public key from private key for verification
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".pem", delete=False
    ) as tmp_pubkey:
        tmp_pubkey_path = tmp_pubkey.name

    try:
        # Extract public key (output file is a positional argument)
        extract_cmd = [
            str(PYTHON_EXE),
            str(ESPSECURE),
            "extract_public_key",
            "--version",
            "2",
            "--keyfile",
            key_file,
            tmp_pubkey_path,  # Output file as positional argument
        ]

        try:
            subprocess.run(
                extract_cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print(f"⚠️  Could not extract public key for verification: {e}")
            if e.stderr:
                print(f"   stderr: {e.stderr}")
            return False

        # Verify signature with public key
        verify_cmd = [
            str(PYTHON_EXE),
            str(ESPSECURE),
            "verify_signature",
            "--version",
            "2",
            "--keyfile",
            tmp_pubkey_path,
            binary_file,
        ]

        try:
            result = subprocess.run(
                verify_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
            print(f"✓ Verified signature: {binary_path.name}")
            return True
        except subprocess.CalledProcessError as e:
            print(f"❌ Signature verification failed for {binary_path.name}")
            if e.stdout:
                print(f"   stdout: {e.stdout}")
            if e.stderr:
                print(f"   stderr: {e.stderr}")
            return False
    finally:
        # Clean up temporary public key file
        try:
            os.unlink(tmp_pubkey_path)
        except Exception:
            pass


def sign_binary(binary_path: Path, key_path: Path) -> bool:
    """Sign a binary file using espsecure.py and verify the signature."""
    if not binary_path.exists():
        print(f"⚠️  Binary not found: {binary_path}")
        return False

    if not key_path.exists():
        print(f"❌ Signing key not found: {key_path}")
        return False

    if ESPSECURE is None:
        print("❌ espsecure.py not found")
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
            stdout=subprocess.DEVNULL,  # suppress stdout
            stderr=subprocess.PIPE,  # capture only stderr
            text=True,
            check=True,
        )
        if result.stderr:
            print(result.stderr)
        print(f"🔐 Signed: {binary_path}")

        # Verify the signature was applied correctly
        if verify_signature(binary_path, key_path):
            print()  # Add blank line after verification
        else:
            print("⚠️  Warning: Signature verification failed\n")
            return False

        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Failed to sign {binary_path.name}: {e}")
        if e.stderr:
            print("   stderr:\n", e.stderr)
        return False
    except FileNotFoundError:
        print("❌ espsecure.py not found")
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
