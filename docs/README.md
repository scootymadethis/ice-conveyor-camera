# Pallet Cam Hub/Firmware/Detector

Questo repository contiene tre parti indipendenti ma coordinate:

- `hub/`: web app Flask che parla con l'ESP32 via TCP, salva i frame JPEG e mostra la UI.
- `firmware/`: firmware ESP32-S3-EYE che gestisce Wi-Fi, TCP, camera, preset, LiDAR e inoltro ESP-NOW dell'ultimo frame.
- `detector/`: firmware separato per il ricevitore `object_detection_model`, che riceve frame via ESP-NOW e lancia il modello FOMO/TFLite Micro.

## Principio di struttura

Il codice è stato diviso in funzioni e file one-purpose:

- `main.cpp`/`main.py` fanno solo bootstrap e wiring.
- I servizi gestiscono una sola responsabilità: camera, TCP, preset, LiDAR, ESP-NOW, frame store, validazione, routes.
- I log sono espliciti e indicano sottosistema, evento e valori utili.
- I log ad alta frequenza, come polling `lidar_status`, sono volutamente filtrati per non sporcare la console.

## Flusso frame manuale

1. Il browser chiama `POST /capture` sul hub.
2. Il hub manda `camera` al firmware via TCP.
3. Il firmware invia `PHOTO_REQUEST ...`.
4. Il hub risponde `ACK`.
5. Il firmware manda `FRAME <bytes> REASON manual CAPTURE_MS <ms>`.
6. Il hub risponde `READY`.
7. Il firmware invia il JPEG binario.
8. Il hub salva `frames/latest.jpg` e un archivio timestampato.

## Flusso detector

1. Il firmware salva in RAM l'ultimo frame catturato.
2. Il browser chiama `POST /espnow/send-last`.
3. Il firmware spezza il JPEG in chunk ESP-NOW.
4. Il firmware `object_detection_model` ricompone il frame.
5. Il detector decodifica JPEG -> RGB888 -> input quantizzato -> inferenza.
6. I risultati vengono stampati su seriale.

## Note importanti

- `firmware/include/secrets.h` contiene Wi-Fi e parametri privati; non va condiviso.
- `hub/.env` contiene `IP_ESP` e `PRESETS_ENABLED`.
- `ENABLE_LIDAR_AT_BOOT` e `ENABLE_DISPLAY_AT_BOOT` sono default a `0` per mantenere il comportamento corrente esplicito.
- Per abilitare LiDAR/display all'avvio, cambia quei define in `firmware/include/app_config.h` o passali come build flag.
