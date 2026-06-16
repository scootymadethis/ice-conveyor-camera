# Lab network setup

The camera only ever needs the **hub's** address. You never configure or look
up the ESP's IP.

## Addresses

```text
Router (lab Wi-Fi)
  SSID:      PalletCamLab
  Password:  long but easy to type
  DHCP:      enabled
  Router IP: 192.168.10.1

PC (hub)
  Static IP: 192.168.10.2
  Hub port:  5000

ESP32-S3-EYE
  IP:        any (DHCP)
  Knows:     http://192.168.10.2:5000   (HUB_BASE_URL in secrets.h)
```

## Why a static IP on the PC

The camera has the hub URL hard-coded (`HUB_BASE_URL`). If the PC's address
changes, the camera can't find it. Pin the PC to `192.168.10.2` — either a
static address on the PC's adapter or a DHCP reservation in the router.

## Set the PC's static IP

- **Windows:** Settings → Network → Wi-Fi/Ethernet → IP assignment → Manual →
  IP `192.168.10.2`, mask `255.255.255.0`, gateway `192.168.10.1`.
- **Linux (NetworkManager):**
  ```bash
  nmcli con mod "PalletCamLab" ipv4.addresses 192.168.10.2/24 \
      ipv4.gateway 192.168.10.1 ipv4.method manual
  nmcli con up "PalletCamLab"
  ```

## Firewall

Allow inbound TCP **5000** on the PC, otherwise the camera's `POST /frame` is
silently dropped. On Windows the first `uvicorn` run usually prompts to allow
it — accept on private networks.

## Verify

From the PC: `curl http://192.168.10.2:5000/ready` → `{"ready":true}`.
From the camera's serial log you should then see `[hub] ready`.

## Credentials hygiene

Wi-Fi credentials live only in `firmware/include/secrets.h`, which is
gitignored. Never commit them. If a password has leaked, rotate it.
WPA2-PSK is assumed; enterprise/EAP (e.g. eduroam) is a future profile, not the
default.
