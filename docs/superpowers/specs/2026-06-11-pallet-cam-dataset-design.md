# Specifica dataset Pallet Cam aggiornata

Questa specifica descrive il dataset previsto dal modello `object_detection_model` attuale.

## Classi

| id | classe | note |
| --- | --- | --- |
| 0 | background | cella senza oggetto |
| 1 | Lego | oggetto Lego target |
| 2 | Cover | cover target |

## Modello runtime

- Input: `96x96x3`
- Output: heatmap `12x12x3`
- Soglia runtime: `0.50`
- Decoder: una cella produce detection se la classe migliore non è background e supera la soglia.

## Regole dataset

- Le immagini devono rappresentare bene le condizioni reali della camera ESP32.
- Includere variabilità di luce, distanza, angolo e sfondo.
- Evitare dataset solo con oggetti centrati: servono esempi nelle celle periferiche della griglia.
- Aggiungere negativi senza oggetti per rendere utile la classe background.
- Tenere separati train/validation/test per scena, non solo per file casuale.

## Allineamento firmware

Il firmware invia JPEG reali dalla camera. Il detector decodifica e ridimensiona con nearest-neighbour verso `96x96`. Il training deve restare coerente con questa pipeline:

```text
JPEG camera -> RGB888 -> resize/quantizzazione -> modello
```

## Quando rigenerare gli header

Rigenera `detector/include/model_data.h` e `detector/include/test_image.h` quando cambi:

- classi
- dimensione input
- architettura modello
- quantizzazione
- immagine self-test

Dopo la rigenerazione, verifica `MODEL_IS_PLACEHOLDER` e l'elenco operatori in `detector/src/model_runner.cpp`.
