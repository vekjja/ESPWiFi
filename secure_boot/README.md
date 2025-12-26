# Secure Boot Configuration

## ⚠️ CRITICAL WARNINGS

1. **BACKUP YOUR SIGNING KEY**: The `ecdsa_signing_key.pem` file is **EXTREMELY IMPORTANT**. 
   - Store it securely in a safe location (encrypted backup, hardware security module, etc.)
   - Never commit it to version control
   - Without this key, you cannot update firmware on devices with secure boot enabled

2. **IRREVERSIBLE**: Once secure boot is enabled on a device, it **CANNOT BE DISABLED**

3. **TESTING**: Always test on a development board first before deploying to production devices

4. **BRICKING RISK**: If you lose the signing key or misconfigure secure boot, your device may become permanently unusable

## What is Secure Boot?

Secure Boot V2 ensures that only cryptographically signed firmware can run on your ESP32-C3. This prevents unauthorized firmware modifications and provides a root of trust.

## Files

- `ecdsa_signing_key.pem` - ECDSA private key (NIST P-256 curve) used to sign firmware
- This key was generated using: `espsecure generate-signing-key --version 2 --scheme ecdsa256`

## Building with Secure Boot

When secure boot is enabled, PlatformIO will automatically:
1. Sign the bootloader with your private key
2. Sign your application firmware
3. Generate a public key digest that gets written to eFuse on first boot

## First Flash Process

**IMPORTANT**: The first time you flash a device with secure boot enabled:

1. Build your project:
   ```bash
   pio run -e esp32-c3
   ```

2. Flash the device (this will also burn the secure boot key to eFuse):
   ```bash
   pio run -e esp32-c3 -t upload
   ```

3. On first boot, the bootloader will:
   - Write the public key digest to eFuse
   - Enable secure boot in eFuse
   - These operations are **permanent and irreversible**

## Updating Firmware

After secure boot is enabled, all future firmware updates must be signed with the same key:
- OTA updates will automatically use signed binaries
- Manual flashing requires signed firmware

## Verifying Secure Boot Status

You can check the eFuse status with:
```bash
espefuse summary
```

Look for:
- `SECURE_BOOT_EN`: Should be set
- `SECURE_BOOT_KEY_DIGESTS`: Should contain your key digest

## Combining with Flash Encryption

For maximum security, consider also enabling flash encryption (see sdkconfig.defaults). This encrypts the firmware stored in flash memory.

## Key Management Best Practices

1. **Store securely**: Use a password manager, HSM, or encrypted storage
2. **Backup**: Keep multiple encrypted backups in different secure locations
3. **Access control**: Limit who has access to the signing key
4. **Audit**: Log all uses of the signing key
5. **Rotation**: Have a plan for key rotation (requires physical access to devices)

## Troubleshooting

- **Build fails**: Ensure the signing key path in `sdkconfig.defaults` is correct
- **Device won't boot**: May have corrupted bootloader - reflash if still in development
- **OTA fails**: Ensure the new firmware is properly signed with the same key

## References

- [ESP-IDF Secure Boot V2 Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/security/secure-boot-v2.html)
- [espsecure.py Documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/espsecure/index.html)

