# Setup rete e router

## Obiettivo

Il PC che esegue il hub Flask e l'ESP32 firmware devono vedersi sulla stessa LAN. Il protocollo usa TCP porta `3131` dal PC verso l'ESP32.

## Requisiti

- Stessa rete Wi-Fi o stesso segmento LAN.
- Nessun isolamento client/AP isolation sul router.
- IP ESP stabile, consigliato tramite DHCP reservation.
- Firewall PC che consenta Python/Flask in rete privata.

## Configurazione consigliata

1. Riserva l'IP dell'ESP32 nel router usando il MAC address dell'ESP.
2. Metti quell'IP in `hub/.env`:

```text
IP_ESP=192.168.1.50
```

3. Flash del firmware con `firmware/include/secrets.h` corretto:

```cpp
#define WIFI_SSID "nome_wifi"
#define WIFI_PASSWORD "password_wifi"
```

4. Avvia monitor seriale e cerca:

```text
[wifi] got ip=...
[tcp] server listening port=3131
```

## Diagnostica

### Il hub non si connette

- Controlla che l'IP in UI o `.env` sia quello stampato dal firmware.
- Riavvia hub dopo cambio rete.
- Verifica che il router non abbia isolamento client.
- Prova da PowerShell:

```powershell
Test-NetConnection 192.168.1.50 -Port 3131
```

### Il TCP si collega ma la cattura fallisce

- Se vedi `PHOTO_REQUEST` ma non `FRAME`, il problema è camera/firmware.
- Se vedi `FRAME` ma non `latest.jpg`, il problema è trasferimento TCP o disco lato hub.
- Se il trasferimento si blocca a metà, riduci framesize o aumenta qualità numerica JPEG.

## ESP-NOW

ESP-NOW serve solo per inviare l'ultimo frame al detector. Il peer MAC si configura in `firmware/include/app_config.h`:

```cpp
#define ESP_NOW_PEER_MAC {0xE0, 0x72, 0xA1, 0xD6, 0x2C, 0xD4}
```

Il detector deve essere acceso e in ascolto prima di chiamare `/espnow/send-last`.
