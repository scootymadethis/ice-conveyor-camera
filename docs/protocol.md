# Protocollo TCP hub <-> firmware

Il firmware ESP32 ascolta su TCP porta `3131`. Il hub mantiene una connessione persistente e usa righe terminate da `\n` per i comandi e le risposte. Il payload JPEG viene inviato come binario dopo l'handshake `FRAME`/`READY`.

## Comandi camera

### `camera`

Richiede un frame manuale.

Risposta/handshake:

```text
PHOTO_REQUEST REASON manual
ACK
FRAME <bytes> REASON manual CAPTURE_MS <ms>
READY
<jpeg bytes>
```

Errori possibili:

```text
BUSY camera
BUSY lidar_capture
CAMERA_ERROR
```

### `config <framesize> <quality> <ae> <contrast> <saturation> <brightness>`

Aggiorna e salva la configurazione camera.

Risposte:

```text
CONFIG_OK HD 10 0 0 0 0
CONFIG_ERROR <reason>
```

Valori accettati:

- `framesize`: `QQVGA`, `QVGA`, `CIF`, `VGA`, `SVGA`, `XGA`, `HD`, `SXGA`, `UXGA`
- `quality`: 4-63, dove numero più basso = qualità più alta
- `ae`, `contrast`, `saturation`, `brightness`: -2..2

### `ae_level <value>`

Aggiorna solo il livello auto-esposizione.

Risposte:

```text
AE_OK <value>
AE_ERROR <reason>
```

## Preset camera

I preset sono JSON in LittleFS sotto `/presets`.

```text
list_presets
preset_save {json compatto}
preset_get <name>
preset_delete <name>
```

Risposte:

```text
PRESETS_OK <json-array>
PRESET_SAVE_OK <json-object>
PRESET_GET_OK <json-object>
PRESET_DELETE_OK <json-object>
PRESET_ERROR <json-object>
```

## LiDAR

Il polling `lidar_status` è considerato ad alta frequenza: il firmware e il hub evitano log rumorosi per questo comando.

```text
lidar_status
lidar_base <mm>
lidar_base_current
lidar_sample_config <good_sample_count> <delay_ms>
lidar_enable <0|1>
```

Risposte:

```text
LIDAR_OK <json-object>
LIDAR_ERROR <json-object>
```

Campi principali di `LIDAR_OK`:

- `lidar_ok`: sensore inizializzato
- `enabled`: trigger LiDAR abilitato
- `baseline_ready`: baseline disponibile
- `base_mm`: baseline attuale o `null`
- `current_mm`: distanza corrente o `null`
- `threshold_mm`: soglia trigger calcolata
- `good_sample_count`, `sample_delay_ms`: configurazione media campioni

## ESP-NOW

```text
espnow_send_last
```

Invia al detector l'ultimo frame JPEG catturato dal firmware.

Risposte:

```text
ESPNOW_OK {"ok":true,"bytes":...,"chunk_count":...,"chunk_bytes":200,"frame_id":...}
ESPNOW_ERROR {"ok":false,"error":"..."}
```

## Frame ESP-NOW

Il chunk ESP-NOW usa la stessa struttura su firmware e detector:

```cpp
struct EspNowFrameChunk {
    uint32_t magic;       // 0x49434546
    uint32_t frameId;
    uint32_t totalLen;
    uint16_t chunkIndex;
    uint16_t chunkCount;
    uint16_t payloadLen;
    uint8_t payload[200];
};
```

Il detector valida `magic`, dimensioni, indici e limite massimo immagine prima di copiare il payload nel buffer.
