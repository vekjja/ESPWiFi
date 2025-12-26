# secure_boot_pre.py
# Pre-build script: ensures ECDSA signing keys exist for manual signing
# Compatible with Arduino framework

import os
import shutil
from pathlib import Path

Import("env")  # pyright: ignore[reportUndefinedVariable]

project_dir = Path(env["PROJECT_DIR"])
keys_dir = project_dir / "secure_boot"
priv_key = keys_dir / "ecdsa_signing_key.pem"
pub_key = keys_dir / "ecdsa_signing_pub_key.pem"


def ensure_keys():
    keys_dir.mkdir(parents=True, exist_ok=True)

    # Use espsecure (modern CLI); fall back only if necessary
    espsecure = shutil.which("espsecure") or shutil.which("espsecure.py")
    if not espsecure:
        print(
            "[secure-boot pre] ERROR: espsecure not found in PATH. Ensure ESP-IDF tools are installed."
        )
        env.Exit(1)  # pyright: ignore[reportUndefinedVariable]
    print(f"[secure-boot pre] Using espsecure at: {espsecure}")

    # Generate private key only if it doesn't exist
    if not priv_key.exists():
        print("[secure-boot pre] Generating ECDSA signing key...")
        cmd_gen = f'"{espsecure}" generate-signing-key "{priv_key}" --version 2 --scheme ecdsa256'
        if os.system(cmd_gen) != 0:
            print(
                "[secure-boot pre] ERROR: Failed to generate ECDSA signing key"
            )
            env.Exit(1)  # pyright: ignore[reportUndefinedVariable]

    # Extract public key only if it doesn't exist (but private key must exist)
    if not pub_key.exists():
        if not priv_key.exists():
            print(
                "[secure-boot pre] ERROR: Cannot extract public key: private key missing"
            )
            env.Exit(1)  # pyright: ignore[reportUndefinedVariable]
        print("[secure-boot pre] Extracting public key from private key...")
        cmd_pub = f'"{espsecure}" extract-public-key --keyfile "{priv_key}" "{pub_key}"'
        if os.system(cmd_pub) != 0:
            # Extraction failed - likely wrong key type (RSA instead of ECDSA)
            print(
                "[secure-boot pre] WARNING: Failed to extract public key - key may be wrong type"
            )
            print(
                "[secure-boot pre] Deleting old key and regenerating as ECDSA..."
            )
            if priv_key.exists():
                priv_key.unlink()
            if pub_key.exists():
                pub_key.unlink()
            # Regenerate as ECDSA
            print("[secure-boot pre] Generating ECDSA signing key...")
            cmd_gen = f'"{espsecure}" generate-signing-key "{priv_key}" --version 2 --scheme ecdsa256'
            if os.system(cmd_gen) != 0:
                print(
                    "[secure-boot pre] ERROR: Failed to generate ECDSA signing key"
                )
                env.Exit(1)  # pyright: ignore[reportUndefinedVariable]
            # Now extract the public key from the new ECDSA key
            cmd_pub = f'"{espsecure}" extract-public-key --keyfile "{priv_key}" "{pub_key}"'
            if os.system(cmd_pub) != 0:
                print(
                    "[secure-boot pre] ERROR: Failed to extract public key after regeneration"
                )
                env.Exit(1)  # pyright: ignore[reportUndefinedVariable]

    if priv_key.exists() and pub_key.exists():
        print(f"[secure-boot pre] Keys ready: {priv_key} and {pub_key}")


# Make sure keys exist before build
ensure_keys()

# For Arduino framework, we'll sign manually in post-build
signing_key_abs = str(priv_key.resolve())
print(f"[secure-boot pre] Signing key ready: {signing_key_abs}")
print("[secure-boot pre] Binaries will be signed after build")

# Hardening: fail build if key missing
if not priv_key.exists():
    print("[secure-boot pre] ERROR: Signing key missing after generation.")
    env.Exit(1)  # pyright: ignore[reportUndefinedVariable]
