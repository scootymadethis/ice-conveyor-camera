# Runbook acquisizione frame

## Avvio hub

Da Windows PowerShell:

```powershell
cd hub
.\hub.ps1 venv
.\hub.ps1 run
```

Apri:

```text
http://127.0.0.1:8080
```

Imposta l'IP ESP dalla UI oppure in `hub/.env`:

```text
IP_ESP=192.168.x.x
PRESETS_ENABLED=true
```

## Avvio firmware

Da Windows PowerShell:

```powershell
cd firmware
.\firmware.ps1 venv
.\firmware.ps1 secrets
.\firmware.ps1 build
.\firmware.ps1 upload
.\firmware.ps1 monitor
```

## Sequenza di test consigliata

1. Flash firmware.
2. Avvia monitor seriale.
3. Avvia hub.
4. Controlla che il hub mostri connessione TCP.
5. Premi Capture.
6. Verifica che compaia `frames/latest.jpg`.
7. Apri `http://127.0.0.1:8080/latest.jpg`.
8. Premi `Send last frame via ESP-NOW` solo dopo almeno una cattura riuscita.

## Log attesi lato firmware

Esempi di log sani:

```text
[1234 ms][INFO][boot] ESP32-S3-EYE firmware starting
[1800 ms][INFO][wifi] got ip=192.168.1.50 rssi=-55 dBm
[2500 ms][INFO][tcp] server listening port=3131
[3100 ms][INFO][camera] camera init complete
[9000 ms][INFO][tcp] hub connected remote_ip=192.168.1.10
[9500 ms][INFO][command] received: camera
[9600 ms][INFO][capture] starting camera capture reason=manual
[9900 ms][INFO][capture] done reason=manual bytes=42123 capture_ms=120 send_ms=230 kbps=1465
```

## Log attesi lato hub

```text
[12:00:01][esp] connecting to 192.168.1.50:3131
[12:00:01][esp] tcp connection established and receiver thread started
[12:00:10][esp] requesting manual camera frame
[12:00:10][esp] photo request accepted: PHOTO_REQUEST REASON manual
[12:00:10][frame] incoming frame bytes=42123 reason=manual
[12:00:11][frame] saved frame_YYYYMMDD_HHMMSS_xxxxxx.jpg reason=manual bytes=42123 ...
```

## Cose da controllare se non arriva il frame

- `IP_ESP` nel hub è corretto?
- PC e ESP sono sulla stessa rete?
- Il firmware stampa `server listening port=3131`?
- Il firewall di Windows blocca Python/Flask?
- La camera è inizializzata o vedi `camera init failed`?
- Il hub riceve `PHOTO_REQUEST` ma non `FRAME`? Allora controlla il monitor seriale firmware durante capture.

## LiDAR

Il codice LiDAR è presente, ma l'inizializzazione all'avvio è esplicitamente disabilitata da default:

```cpp
#define ENABLE_LIDAR_AT_BOOT 0
#define ENABLE_DISPLAY_AT_BOOT 0
```

Per attivare trigger automatici, abilita il LiDAR, riflasha e poi configura baseline e sample dalla UI/API.
