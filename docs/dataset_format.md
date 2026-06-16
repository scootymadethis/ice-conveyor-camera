# Dataset format

Each capture session is a self-contained directory under `data/sessions/`.

```text
data/
└── sessions/
    └── 2026-06-11_test_conveyor/
        ├── frames/
        │   ├── esp32s3-eye-01_00000001.jpg
        │   ├── esp32s3-eye-01_00000002.jpg
        │   └── esp32s3-eye-01_00000003.jpg
        ├── metadata.csv
        ├── config.json        # snapshot of the config used (written once)
        └── notes.md           # optional, written by you
```

- **Filename:** `<camera_id>_<index:08d>.jpg`, where `index` is a
  **server-assigned, contiguous** counter per (camera, session) — *not* the
  device `frame_id`. The hub never reuses an index, so a camera reboot (which
  resets `frame_id` to 1) cannot overwrite earlier frames.
- **`config.json`:** the hub config in force when the session's first frame
  arrived — keeps the session reproducible.
- `data/` is **gitignored**; the dataset is not committed.

## `metadata.csv`

One header row, then one row per frame.

```csv
session_id,camera_id,frame_id,filename,server_ts,capture_ts_us,size_bytes,framesize,jpeg_quality,rssi,free_heap,label
2026-06-11_test_conveyor,esp32s3-eye-01,1,frames/esp32s3-eye-01_00000001.jpg,2026-06-11T10:32:01.123456+00:00,987654321,18342,QVGA,12,-54,123456,
```

| Column          | Source              | Notes                                  |
|-----------------|---------------------|----------------------------------------|
| `session_id`    | `X-Session-Id`      |                                        |
| `camera_id`     | `X-Camera-Id`       |                                        |
| `frame_id`      | `X-Frame-Id`        | device counter; resets on reboot — gaps spot drops |
| `filename`      | hub                 | relative to the session dir            |
| `server_ts`     | hub                 | UTC ISO-8601, when the frame landed    |
| `capture_ts_us` | `X-Capture-Ts-Us`   | device `micros()` at capture           |
| `size_bytes`    | hub                 | JPEG length                            |
| `framesize`     | `X-Framesize`       |                                        |
| `jpeg_quality`  | `X-Jpeg-Quality`    |                                        |
| `rssi`          | `X-Rssi`            | dBm                                    |
| `free_heap`     | `X-Free-Heap`       | bytes                                  |
| `label`         | you                 | empty at capture; fill in later        |

## Labelling

`label` is intentionally empty during capture. Fill it afterwards (manually, or
with a script that edits `metadata.csv`) — e.g. `pallet`, `empty`, `damaged`.
Keeping labels in the CSV rather than in filenames means you can relabel without
touching the image files.
