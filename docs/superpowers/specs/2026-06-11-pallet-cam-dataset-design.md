# Design — Pallet-Cam Dataset Acquisition Tool

**Date:** 2026-06-11
**Repo:** `ice_conveyor_camera`
**Status:** Approved (brainstorming)

## Purpose

A reproducible **dataset acquisition tool** (not a webcam) built around an
ESP32-S3-EYE camera. The camera joins a lab Wi-Fi, waits for a Python hub on a
PC to be alive, fetches its configuration, and — only while enabled — captures
JPEG frames and POSTs them to the hub. The hub stores frames plus per-frame
metadata as a labelled-ready dataset.

The key architectural decision: **push, don't serve**. The ESP32 is an HTTP
*client*; the PC is the server. No web UI on the ESP, no ESP IP to know, no
streaming when the hub is absent.

## Decisions (resolved during brainstorming)

- **Scope:** working implementation — firmware structured to compile + a hub
  that runs and is tested locally. Not stubs.
- **Layout:** component folders at repo root (`firmware/`, `hub/`, `docs/`).
  No `src/` wrapper at the root (heterogeneous components self-document). The
  `src/` inside `firmware/` is the PlatformIO convention and stays.
- **Repo name:** stays `ice_conveyor_camera`.
- **Docs / code language:** English.
- **Secrets:** Wi-Fi/hub credentials live only in `firmware/include/secrets.h`,
  which is gitignored. `secrets.example.h` is the committed template.

## Repository structure

```text
ice_conveyor_camera/
├── README.md
├── .gitignore
├── firmware/
│   ├── platformio.ini
│   ├── partitions.csv
│   ├── include/   board_config.h, camera_pins.h, app_config.h,
│   │              secrets.example.h, secrets.h (gitignored)
│   └── src/       main.cpp, camera_service.*, wifi_service.*,
│                  hub_client.*, capture_loop.*, status_led.*
├── hub/
│   ├── requirements.txt, run.py, hub_config.json
│   ├── pallet_hub/  __init__.py, main.py, config.py, storage.py, models.py
│   └── tests/       test_smoke.py
├── docs/          router_setup.md, dataset_format.md, protocol.md,
│                  capture_runbook.md
└── data/          (gitignored) sessions/<session_id>/...
```

## Firmware behaviour — state machine

```text
BOOT → CAMERA_INIT → WIFI_CONNECT → HUB_WAIT → CONFIG_FETCH → STREAMING
                                       ▲                          │
                                       └──────── on failures ─────┘
```

- `HUB_WAIT` polls `GET /ready` until the hub answers.
- `CONFIG_FETCH` reads `GET /config`; if `enabled=false`, returns to wait.
- `STREAMING` captures and POSTs frames, paced to `frame_interval_ms`, with no
  backlog (drop pacing delay if slow). It periodically returns to
  `CONFIG_FETCH` to honour live config changes, and to `HUB_WAIT` after
  `max_failures` consecutive upload failures.

Firmware modules (one responsibility each): `camera_service`, `wifi_service`,
`hub_client`, `capture_loop`, `status_led`.

## HTTP protocol

- `GET /ready` → `{"ready": true}` — liveness only.
- `GET /config` → enabled, session_id, frame_interval_ms, framesize, jpeg_quality,
  max_failures. Identity is device-owned, so `/config` carries no `camera_id`.
- `POST /frame` → body is raw JPEG bytes; metadata travels in headers:
  `X-Camera-Id`, `X-Session-Id`, `X-Frame-Id`, `X-Capture-Ts-Us`,
  `X-Framesize`, `X-Jpeg-Quality`, `X-Free-Heap`, `X-Rssi`.

See `docs/protocol.md` for the authoritative contract.

## Dataset format

```text
data/sessions/<session_id>/
├── frames/<camera_id>_<index:08d>.jpg   (server-assigned, never reused)
├── metadata.csv
└── config.json   (snapshot of the config used)
```

`metadata.csv` columns: session_id, camera_id, frame_id, filename, server_ts,
capture_ts_us, size_bytes, framesize, jpeg_quality, rssi, free_heap, label.
`label` is left empty at capture time and filled in later.

## Hub

FastAPI + uvicorn. `/config` re-reads `hub_config.json` on every request so the frame interval,
quality, session and the `enabled` flag can change without a restart or a
re-flash. `storage` owns session-directory creation, frame writing, the
config snapshot, and CSV append. IDs are sanitised to prevent path traversal.
Frame filenames use a **server-assigned, contiguous index** (per camera per
session, seeded from disk) instead of the device `frame_id`, so a camera reboot
can never overwrite earlier frames.

## Defaults / capture guidance

Start conservative: QVGA, jpeg_quality 12, 200 ms interval, then ramp to 100 → 67 → 33 ms.
In the ESP camera driver a **higher** `quality` number means **more**
compression (worse image). For a conveyor dataset prefer short exposure and
strong light (less motion blur) over higher resolution.

## Verification

- **Hub:** run locally and exercised via tests (`/ready`, `/config`, a fake
  `POST /frame` that lands a file + a CSV row).
- **Firmware:** written to compile against `espressif32` + Arduino, but the
  build toolchain is not available in this environment, so compiling and
  flashing (milestones M1–M5) are done by the user on hardware. PSRAM,
  flash/PSRAM mode and the status-LED GPIO must be confirmed on first boot.

## Milestones

- **M0** — clean repo; hub runs; README explains setup; no secrets in git.
- **M1** — camera standalone (PSRAM found, init OK, frame captured).
- **M2** — Wi-Fi + `/ready` polling.
- **M3** — single frame upload lands on the PC.
- **M4** — controlled loop driven by `enabled`.
- **M5** — real conveyor dataset session (see `docs/capture_runbook.md`).
