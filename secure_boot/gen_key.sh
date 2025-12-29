#!/bin/bash

# This Create RSA2048 key
cd /Users/kevin.jayne/git/vekjja/ESPWiFi && espsecure generate-signing_key --version 2 secure_boot/rsa2048_signing_key.pem

# for esp32c3, use ecdsa256
cd /Users/kevin.jayne/git/vekjja/ESPWiFi && espsecure generate-signing-key --version 2 --scheme ecdsa256 secure_boot/ecdsa256_signing_key.pem