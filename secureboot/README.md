# Secure Boot Configuration

## Critical warnings

1. **Back up your signing keys.** The PEM files under `keys/` are **irreplaceable** for that device line once secure boot is fused. Store them in encrypted backups or an HSM; never commit them to git. Without the correct key you cannot ship signed updates to those chips.

2. **Irreversible.** After secure boot is enabled in eFuse, it generally **cannot** be turned off.

3. **Test on a spare board** before production or field devices.

4. **Brick risk.** Losing the key, wrong digest, or bad first flash can make a device unrecoverable without hardware-level recovery options.

## What this is

Secure Boot V2 lets the ROM verify the bootloader (and, depending on configuration, the app) before execution. Only firmware signed with your private key is accepted, which blocks unsigned or tampered images.

Targets differ by chip family:

- **ESP32 (classic)** often uses **RSA** signing keys (see `keys/rsa2048_signing_key.pem` from this repo’s generator).
- **ESP32-C3 / many newer chips** use **ECDSA P-256** (`keys/ecdsa256_signing_key.pem`).

Use the key type and signing settings that match your chip and ESP-IDF `sdkconfig` for that environment.

## Prerequisites: `espsecure`

`espsecure` ships with the **esptool** Python package. PlatformIO does not always put it on your shell `PATH` until esptool is installed into PlatformIO’s virtualenv:

```bash
"${HOME}/.platformio/penv/bin/pip" install esptool
```

After that, `espsecure` and `espefuse` are available as:

```bash
"${HOME}/.platformio/penv/bin/espsecure" --help
"${HOME}/.platformio/penv/bin/espefuse" --help
```

(You can also `export PATH="${HOME}/.platformio/penv/bin:${PATH}"` in your shell.)

## Generating keys

From the repo root:

```bash
./secureboot/gen_key.sh
```

This creates `secureboot/keys/` (if needed) and writes:

| File | Role |
|------|------|
| `keys/rsa2048_signing_key.pem` | RSA private key for ESP32-classic secure boot v2. **esptool 5.x** defaults to **RSA-3072** for `--version 2`; the script filename is historical—use `--scheme rsa3072` and a matching `sdkconfig` path if you want names and bits aligned. |
| `keys/ecdsa256_signing_key.pem` | ECDSA NIST P-256 private key (ESP32-C3 and similar, secure boot v2) |

Equivalent manual commands (same as the script):

```bash
espsecure generate-signing-key --version 2 keys/rsa2048_signing_key.pem
espsecure generate-signing-key --version 2 --scheme ecdsa256 keys/ecdsa256_signing_key.pem
```

To generate RSA explicitly as 3072-bit (matches typical defaults):

```bash
espsecure generate-signing-key --version 2 --scheme rsa3072 keys/rsa3072_signing_key.pem
```

Point ESP-IDF / `sdkconfig` at the PEM you intend to use for that build target.

## Building with secure boot

When secure boot signing is enabled in your project configuration, the build signs the bootloader and/or app and prepares digests for provisioning. Exact behavior depends on your `sdkconfig` / `sdkconfig.defaults` and board.

Example build (adjust the environment to your board):

```bash
pio run -e esp32-c3
```

## First flash

The first successful flash with secure boot enabled typically burns key digests and enables secure boot in eFuse—**permanent**.

1. Build: `pio run -e <your-env>`
2. Flash: `pio run -e <your-env> -t upload`
3. Confirm behavior on serial log and, if needed, eFuse summary (below).

## Updating firmware

All updates must be signed with the **same** private key (or a rotation strategy your process defines). OTA and serial flashing both require matching signatures and compatible partition schemes.

## Verifying eFuse status

With the same tools on `PATH` as above:

```bash
espefuse summary
```

For chip-specific fields, add `--chip esp32c3` (or `esp32`, `esp32s3`, etc.) as required.

Useful checks often include secure boot enable flags and key digest fields (names vary by chip).

## Flash encryption

Flash encryption is separate but complementary; enable it in `sdkconfig` when you want ciphertext in external flash.

## Key management

- Store keys encrypted; restrict access; keep redundant backups in separate locations.
- Document who can sign releases and audit signing usage if needed.

## Troubleshooting

- **`espsecure: command not found`:** Install esptool into PlatformIO’s venv (see Prerequisites) or use the full path under `~/.platformio/penv/bin/`.
- **Build errors about missing or invalid key:** Paths in `sdkconfig` must match the PEM you generated and the chip’s expected scheme (RSA vs ECDSA).
- **Device won’t boot after enable:** Capture early boot logs; verify signature pipeline and that the flashed images match the fused digest.
- **OTA fails:** Ensure the new artifact is signed with the same key as production and that the partition table / version rules allow the update.

## References

- [ESP-IDF Secure Boot V2 (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/secure-boot-v2.html)
- [ESP-IDF Secure Boot V2 (ESP32)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/security/secure-boot-v2.html)
- [esptool / espsecure](https://docs.espressif.com/projects/esptool/en/latest/esp32/espsecure/index.html)
