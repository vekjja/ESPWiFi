#!/usr/bin/env python3
"""
Check if the current signing key matches what's burned into the device's secure boot eFuses.
This helps diagnose secure boot verification failures.
"""
from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

# Add scripts to path for imports
PROJECT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_DIR / "scripts"))

from burn_utils import resolve_port, signing_key_path


def extract_public_key_digest(key_path: Path) -> str | None:
    """Extract SHA256 digest of public key (as used by ESP32 secure boot v2)."""
    try:
        # For ESP32 secure boot v2, extract public key in DER format and calculate SHA256
        # This matches how ESP32 stores the digest in eFuses
        pubkey_result = subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(key_path),
                "-pubout",
                "-outform",
                "DER",
            ],
            capture_output=True,
            check=True,
        )

        # Calculate SHA256 hash of the DER-encoded public key
        # This is what ESP32 stores in the eFuses for secure boot v2
        digest = hashlib.sha256(pubkey_result.stdout).hexdigest()
        return digest.lower()
    except subprocess.CalledProcessError as e:
        print(f"⚠️  Error extracting public key: {e.stderr if e.stderr else e}")
        return None
    except FileNotFoundError:
        print(
            "⚠️  openssl not found. Install openssl to extract public key digest."
        )
        return None
    except Exception as e:
        print(f"⚠️  Could not extract public key digest: {e}")
        return None


def get_device_key_digest(port: str) -> str | None:
    """Read secure boot key digest from device eFuses."""
    import re

    try:
        # Try using espefuse dump_blocks to get raw block data
        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "espefuse",
                "--port",
                port,
                "dump_blocks",
                "BLOCK_KEY0",
            ],
            capture_output=True,
            text=True,
            check=True,
        )

        output = result.stdout

        # Extract hex values from the output
        # espefuse dump_blocks shows data in lines like:
        #   00000000: 3b ab e8 ee 22 79 12 b9 ...
        # We need to extract the actual data bytes (after the colon), not addresses
        # Look for hex dump lines and extract the byte values
        lines = output.split("\n")
        hex_bytes = []
        for line in lines:
            # Match lines with format: "  OFFSET: byte byte byte ..."
            if ":" in line:
                # Get everything after the colon
                data_part = line.split(":", 1)[1]
                # Extract 2-character hex values (individual bytes)
                bytes_in_line = re.findall(
                    r"\b([0-9a-f]{2})\b", data_part.lower()
                )
                hex_bytes.extend(bytes_in_line)

        if len(hex_bytes) >= 32:
            # For secure boot v2, first 32 bytes contain SHA256 digest
            # ESP32 stores data in little-endian format (32-bit words)
            # Group bytes into 32-bit words (4 bytes each) and reverse bytes within each word
            # When we burn the digest, it's stored in little-endian, so we need to reverse it back
            digest_chars = []
            for word_idx in range(8):  # 8 words * 4 bytes = 32 bytes
                word_bytes = hex_bytes[word_idx * 4 : (word_idx + 1) * 4]
                # Reverse bytes in each word (little-endian to big-endian)
                reversed_bytes = list(reversed(word_bytes))
                digest_chars.extend(reversed_bytes)
            digest = "".join(digest_chars)
            if len(digest) == 64:  # 32 bytes = 64 hex chars
                return digest.lower()

        # Fallback: try dump command
        result2 = subprocess.run(
            [
                sys.executable,
                "-m",
                "espefuse",
                "--port",
                port,
                "dump",
            ],
            capture_output=True,
            text=True,
            check=True,
        )

        output2 = result2.stdout
        lines = output2.splitlines()
        in_key_block = False
        key_data = []

        for line in lines:
            line_lower = line.lower()
            if "block_key0" in line_lower or "block4" in line_lower:
                in_key_block = True
                # Try to extract hex from this line too
                hex_vals = re.findall(r"\b([0-9a-f]{8})\b", line_lower)
                if hex_vals:
                    key_data.extend(hex_vals)
            elif in_key_block:
                # Extract hex values from lines in the block
                hex_vals = re.findall(r"\b([0-9a-f]{8})\b", line_lower)
                if hex_vals:
                    key_data.extend(hex_vals)
                elif line.strip() == "":
                    # Empty line might separate blocks, check if we have enough data
                    if len(key_data) >= 8:
                        break
                elif "block" in line_lower and "block_key0" not in line_lower:
                    # Another block starts
                    break

        if key_data and len(key_data) >= 8:
            # First 8 words (32 bytes) = 64 hex chars = SHA256 digest
            digest = "".join(key_data[:8])
            if len(digest) == 64:
                return digest.lower()

        return None
    except subprocess.CalledProcessError as e:
        print(f"⚠️  Error reading device eFuses: {e.stderr if e.stderr else e}")
        return None
    except Exception as e:
        print(f"⚠️  Could not read device key digest: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(
        description="Check if signing key matches device secure boot key"
    )
    parser.add_argument(
        "--port",
        help="Serial port (auto-detected if not provided)",
    )
    args = parser.parse_args()

    key = signing_key_path()
    print("\n🔍 Checking secure boot key match...")
    print(f"   Signing key: {key}")
    print(f"   Key exists: {key.exists()}\n")

    if not key.exists():
        print("❌ Signing key not found!")
        sys.exit(1)

    # Get port
    port = resolve_port(args.port)
    print(f"   Device port: {port}\n")

    # Extract public key digest from signing key
    print("📝 Extracting public key digest from signing key...")
    key_digest = extract_public_key_digest(key)
    if not key_digest:
        print("❌ Failed to extract digest from signing key")
        sys.exit(1)
    print(f"   Key digest: {key_digest}\n")

    # Read digest from device
    print("📝 Reading secure boot key digest from device eFuses...")
    device_digest = get_device_key_digest(port)
    if not device_digest:
        print("❌ Failed to read key digest from device")
        print("   Make sure device is accessible and secure boot is enabled")
        sys.exit(1)
    print(f"   Device digest: {device_digest}\n")

    # Compare
    print("🔍 Comparing digests...")
    if key_digest == device_digest:
        print(
            "✅ MATCH! The signing key matches the key burned into the device."
        )
        print("   Your firmware should boot successfully with this key.\n")
        sys.exit(0)
    else:
        print("❌ MISMATCH! The signing key does NOT match the device key.")
        print(
            "\n⚠️  CRITICAL: The device will reject firmware signed with this key."
        )
        print("   This means:")
        print("   1. Secure boot was enabled with a DIFFERENT key")
        print("   2. You need to find the original key that was used")
        print("   3. Sign all firmware with that EXACT key")
        print(
            "   4. If the original key is lost, the device cannot be updated!\n"
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
