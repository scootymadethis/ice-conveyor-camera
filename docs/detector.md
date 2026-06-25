# Detector object_detection_model

Il vecchio `detector/detector.ino` è stato trasformato in un progetto PlatformIO/Arduino strutturato:

```text
detector/
  platformio.ini
  object_detection_model.ps1
  include/
    detector_config.h
    espnow_frame.h
    model_data.h
    test_image.h
  src/
    main.cpp
    espnow_receiver.cpp/.h
    jpeg_decoder.cpp/.h
    model_runner.cpp/.h
    detection_decoder.cpp/.h
    logging.cpp/.h
```

## Avvio

```powershell
cd detector
.\object_detection_model.ps1 venv
.\object_detection_model.ps1 model
.\object_detection_model.ps1 build
.\object_detection_model.ps1 upload
.\object_detection_model.ps1 monitor
```

## Responsabilità file

- `main.cpp`: boot, init servizi, loop che processa un frame pronto.
- `espnow_receiver`: ricezione e ricomposizione chunk ESP-NOW.
- `jpeg_decoder`: decode JPEG in RGB888 usando `TJpg_Decoder`.
- `model_runner`: inizializzazione TFLite Micro, quantizzazione, inferenza.
- `detection_decoder`: lettura heatmap `12x12x3` e stampa risultati.
- `logging`: log espliciti e uniformi.

## Log non spammosi

Il detector non stampa più un punto per ogni chunk ricevuto. Logga solo:

- inizio frame
- frame completo
- decode JPEG
- inferenza completata
- detections sopra soglia
- errori di formato/dimensione

## Compatibilità con firmware

`detector/include/espnow_frame.h` deve rimanere compatibile con `firmware/src/espnow_service.cpp`. I campi critici sono:

- `ESPNOW_FRAME_MAGIC`
- `ESPNOW_FRAME_CHUNK_BYTES`
- ordine dei campi di `EspNowFrameChunk`

## Limite memoria

`MAX_IMAGE_SIZE_BYTES` è 50 KB. Se invii JPEG più grandi, il detector rifiuta il frame. Per immagini più grandi serve PSRAM e va aumentato il limite consapevolmente.
