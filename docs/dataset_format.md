# Dataset e modello object detection

Il detector usa un modello FOMO/TFLite Micro con input `96x96x3` e griglia `12x12`. Le classi attuali sono:

```text
0 background
1 Lego
2 Cover
```

Questi valori sono centralizzati in `detector/include/detector_config.h` e in `detector/src/detection_decoder.cpp`.

## Header generati

Il firmware detector richiede due header in `detector/include/`:

- `model_data.h`: contiene `g_model[]`, `g_model_len`, `MODEL_IS_PLACEHOLDER`.
- `test_image.h`: contiene `g_test_image[]`, `IMG_W`, `IMG_H`, `IMG_C`.

Se `MODEL_IS_PLACEHOLDER` è true, il detector si ferma al boot con un log esplicito.

## Formato frame live

Il percorso live preferito è JPEG:

```text
firmware camera -> JPEG -> ESP-NOW chunks -> detector -> JPEG decode -> RGB888 -> quantizzazione int8 -> inferenza
```

Il detector supporta anche raw RGB888 solo se la dimensione è esattamente:

```text
RAW_SRC_W * RAW_SRC_H * IMG_C = 96 * 96 * 3
```

## Output seriale

Ogni cella sopra soglia genera un log:

```text
[12345 ms][INFO][detect] class=Lego probability=0.84 cell=(6,4) px=(52,36)
```

Se non ci sono oggetti:

```text
[12345 ms][INFO][detect] no objects above confidence threshold
```

## Cambiare soglia o classi

- Soglia: `MODEL_CONFIDENCE_THRESHOLD` in `detector/include/detector_config.h`.
- Classi: `CLASS_NAMES` in `detector/src/detection_decoder.cpp`.
- Input/griglia: `MODEL_INPUT_SIZE`, `MODEL_GRID_SIZE`, `MODEL_CLASS_COUNT` in `detector/include/detector_config.h`.

Dopo ogni cambio modello, rigenera `model_data.h` e verifica che gli operatori registrati in `detector/src/model_runner.cpp` corrispondano al modello TFLite.
