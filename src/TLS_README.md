# HTTPS/WSS on-device using Cloudflare Origin CA (LAN usage)

This project can run the ESP-IDF web server over **HTTPS + WSS** using
`esp_https_server` when TLS credentials are present on LittleFS.

## Important: Cloudflare Origin CA is not browser-trusted by default

Cloudflare **Origin CA** certificates are intended for encrypting traffic
between Cloudflare and your origin. When you access the device **directly from a
browser on your LAN (no Cloudflare proxy in the middle)**:

- The device can still serve HTTPS/WSS.
- **Your browser will NOT trust the certificate unless you install the
  Cloudflare Origin CA root certificate into your OS/browser trust store.**
- You also must access the device using a hostname covered by the certificate
  (e.g. `device1.espwifi.io`), not `espwifi.local`.

## Where to place the cert/key on the device

On boot, the firmware checks for:

- **Certificate**: LittleFS path `/tls/server.crt`
- **Private key**: LittleFS path `/tls/server.key`

If both exist and HTTPS starts successfully, the web server runs on **port 443**.
Otherwise it falls back to plain HTTP on **port 80**.

### Uploading the files

You can upload these files to LittleFS using the existing file APIs (via the UI
File Browser), placing them under the `lfs` filesystem at:

- `/lfs/tls/server.crt`
- `/lfs/tls/server.key`

Then reboot the device.

## LAN DNS requirement (hostname must match cert)

To make the certificate name match on LAN, you need your LAN to resolve the
public hostname to the device’s LAN IP.

Typical approaches:

- Configure your router/DNS to map `device1.espwifi.io -> 192.168.1.50`
- Or run a local DNS server (Pi-hole / dnsmasq) with that A record.
- Or edit `hosts` on development machines (not scalable).

## Cloudflare Origin CA issuance (high level)

In Cloudflare dashboard for your zone:

1. Create an **Origin Certificate** (ECC is recommended for embedded).
2. Include SANs for the hostnames you’ll use (e.g. `device1.espwifi.io`).
3. Download **PEM cert** and **PEM private key**.
4. Install the **Cloudflare Origin CA root** on the client devices that will
   access the ESP directly on LAN.

## Security notes

- Treat `server.key` as **sensitive**.
- For production, consider storing keys in a protected partition or enabling
  flash encryption. LittleFS is convenient but not a secure enclave.


