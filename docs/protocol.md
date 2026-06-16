# HTTP protocol

The ESP32 is always the HTTP **client**; the hub is the **server**. Three
endpoints, all under `HUB_BASE_URL` (e.g. `http://192.168.10.2:5000`).

## `GET /ready`

Liveness only — "is the hub alive?". The camera will not stream until this
returns success.

**Response 200:**

```json
{ "ready": true }
```

## `GET /config`

Current configuration. The hub re-reads `hub_config.json` on every request, so
changes take effect without restarting the hub or re-flashing the camera.

**Response 200:**

```json
{
  "enabled": true,
  "session_id": "2026-06-11_test_conveyor",
  "frame_interval_ms": 200,
  "framesize": "QVGA",
  "jpeg_quality": 12,
  "max_failures": 5
}
```

Identity is device-owned: `/config` carries **no** `camera_id` — the camera
always reports its compiled `CAMERA_ID` via `X-Camera-Id`.

- `enabled` — when `false`, the camera returns to waiting (no capture).
- `frame_interval_ms` — milliseconds between frame captures; must be > 0.
- `framesize` — one of `QQVGA, QVGA, CIF, VGA, SVGA, XGA, HD, SXGA, UXGA`.
- `jpeg_quality` — ESP driver scale, **lower = better** (less compression).
- `max_failures` — consecutive upload failures before the camera gives up and
  re-polls `/ready`.

## `POST /frame`

Upload a single JPEG. The body is the **raw JPEG bytes**; all metadata travels
in headers (no multipart).

**Headers:**

| Header             | Meaning                                   |
|--------------------|-------------------------------------------|
| `Content-Type`     | `image/jpeg`                              |
| `X-Camera-Id`      | stable camera identity (device-owned)     |
| `X-Session-Id`     | current session                           |
| `X-Frame-Id`       | monotonic frame counter                   |
| `X-Capture-Ts-Us`  | capture timestamp, microseconds (`micros()`) |
| `X-Framesize`      | framesize name in use                     |
| `X-Jpeg-Quality`   | jpeg quality in use                       |
| `X-Free-Heap`      | free heap on the device (bytes)           |
| `X-Rssi`           | Wi-Fi RSSI (dBm)                          |

**Response 200:**

```json
{ "ok": true, "saved": "frames/esp32s3-eye-01_00000001.jpg", "size": 18342 }
```

**Response 400:** empty body.

Notes:
- `X-Camera-Id` is the source of truth for identity; `/config` carries no
  `camera_id`.
- The stored filename uses a **server-assigned, contiguous** index (per camera
  per session), not `X-Frame-Id`. The hub never reuses an index, so a camera
  reboot (which resets `X-Frame-Id` to 1) cannot overwrite earlier frames.
  `X-Frame-Id` is still recorded in `metadata.csv`.
- Missing/invalid numeric headers default to `0` server-side rather than
  failing the upload.
