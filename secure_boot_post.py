# secure_boot_post.py
# Post-build script: manually sign binaries for secure boot
# Compatible with Arduino framework

import os
import shutil
from pathlib import Path

Import("env")  # pyright: ignore[reportUndefinedVariable]

project_dir = Path(env["PROJECT_DIR"])
keys_dir = project_dir / "secure_boot"
priv_key = keys_dir / "ecdsa_signing_key.pem"
pub_key = keys_dir / "ecdsa_signing_pub_key.pem"


def sign_binaries(source, target, env):
    """Called after firmware.bin is built"""

    build_dir = Path(env.subst("$BUILD_DIR"))
    firmware_bin = Path(str(target[0]))  # The firmware.bin that was just built
    bootloader_bin = build_dir / "bootloader.bin"

    print(f"\n[secure-boot post] ═══════════════════════════════════════")
    print(f"[secure-boot post] Signing firmware...")
    print(f"[secure-boot post] Found firmware: {firmware_bin}")

    if bootloader_bin.exists():
        print(f"[secure-boot post] Found bootloader: {bootloader_bin}")
    else:
        print(f"[secure-boot post] No bootloader found (Arduino framework)")

    # Resolve espsecure path
    espsecure = shutil.which("espsecure") or shutil.which("espsecure.py")
    if not espsecure:
        print(
            "[secure-boot post] ERROR: espsecure not found; cannot sign binaries"
        )
        return

    # Create signed output directory
    signed_dir = project_dir / "release"
    signed_dir.mkdir(parents=True, exist_ok=True)

    # Sign firmware
    print(f"[secure-boot post] Signing firmware with ECDSA key...")
    firmware_signed = signed_dir / "firmware_signed.bin"
    cmd_sign_app = f'"{espsecure}" sign_data --version 2 --keyfile "{priv_key}" --output "{firmware_signed}" "{firmware_bin}"'
    if os.system(cmd_sign_app) != 0:
        print("[secure-boot post] ERROR: Failed to sign firmware")
        return
    print(f"[secure-boot post] ✓ Signed firmware: {firmware_signed}")

    # Sign bootloader if it exists
    if bootloader_bin.exists():
        print(f"[secure-boot post] Signing bootloader with ECDSA key...")
        bootloader_signed = signed_dir / "bootloader_signed.bin"
        cmd_sign_boot = f'"{espsecure}" sign_data --version 2 --keyfile "{priv_key}" --output "{bootloader_signed}" "{bootloader_bin}"'
        if os.system(cmd_sign_boot) != 0:
            print("[secure-boot post] WARNING: Failed to sign bootloader")
        else:
            print(
                f"[secure-boot post] ✓ Signed bootloader: {bootloader_signed}"
            )

    # Verify signature
    print(f"[secure-boot post] Verifying firmware signature...")
    if (
        os.system(
            f'"{espsecure}" verify_signature --version 2 --keyfile "{pub_key}" "{firmware_signed}"'
        )
        == 0
    ):
        print(f"[secure-boot post] ✓ Firmware signature verified")
    else:
        print(f"[secure-boot post] ✗ Firmware signature verification failed")

    # Copy original binaries
    shutil.copy2(firmware_bin, signed_dir / "firmware_unsigned.bin")
    if bootloader_bin.exists():
        shutil.copy2(bootloader_bin, signed_dir / "bootloader_unsigned.bin")

    print(f"[secure-boot post] ═══════════════════════════════════════")
    print(f"[secure-boot post] Done! Signed artifacts in: {signed_dir}")
    print(f"[secure-boot post] To upload signed firmware:")
    print(
        f"[secure-boot post]   esptool.py write_flash 0x10000 {firmware_signed}"
    )
    print(f"[secure-boot post] ═══════════════════════════════════════\n")


# Register post-action to run after firmware.bin is built
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", sign_binaries)

print("[secure-boot post] Post-build signing hook registered")
