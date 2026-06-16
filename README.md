# ice_conveyor_camera — pallet-cam dataset acquisition

A reproducible **dataset acquisition tool** built around an **ESP32-S3-EYE**
camera. It is *not* a webcam: the camera pushes JPEG frames to a Python hub on a
PC, which stores them as a labelled-ready dataset.

```text
ESP32-S3-EYE camera
  → joins the lab Wi-Fi
  → waits until the Python hub is alive (GET /ready)
  → downloads its config       (GET /config)
  → if enabled, captures JPEG frames
  → POSTs each frame to the PC  (POST /frame)
  → the PC saves dataset + metadata
```

No web UI on the ESP. No ESP-as-server. No ESP IP to know. The ESP only needs
the **hub's** URL.

## Why push instead of stream

The goal is **repeatable data**, not watching video. So the ESP is an HTTP
*client* and the PC is the server. The ESP never streams when the hub is down.

## Repository layout

```text
firmware/   ESP32-S3-EYE firmware (PlatformIO + Arduino)
hub/        Python FastAPI hub that receives and stores frames
docs/       router setup, protocol, dataset format, capture runbook
data/       captured sessions (gitignored)
```

## Quick start

### 1. Network

See `docs/router_setup.md`. In short:

- Lab router `PalletCamLab`, DHCP on, router `192.168.10.1`.
- PC: static IP `192.168.10.2`, hub on port `5000`.
- ESP32: any DHCP address; it only knows `http://192.168.10.2:5000`.

### 2. Hub (PC)

```bash
cd hub
python3 -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python run.py                       # or: uvicorn pallet_hub.main:app --host 0.0.0.0 --port 5000
```

Check it: `curl http://localhost:5000/ready` → `{"ready":true}`.

Run the tests: `pip install -r requirements-dev.txt && pytest -q`.

Edit `hub/hub_config.json` at any time to change the frame interval, quality, session, or to
toggle `enabled` — `/config` re-reads it on every request, no restart needed.

### 3. Firmware (ESP32-S3-EYE)

PlatformIO runs from `firmware/.venv` (the system `pio` from apt is too old and
breaks with modern Click). Set it up once:

```bash
make firmware-venv                                              # firmware/.venv + PlatformIO
cp firmware/include/secrets.example.h firmware/include/secrets.h  # then edit secrets.h
make firmware-build                                             # build
make firmware-upload                                            # flash
make firmware-monitor                                           # serial @ 115200
```

> `secrets.h` is **gitignored**. Never commit Wi-Fi/hub credentials — keep
> placeholders in `secrets.example.h` (which *is* tracked).

## Protocol & dataset

- Protocol contract: `docs/protocol.md`
- Dataset format: `docs/dataset_format.md`
- Capture runbook: `docs/capture_runbook.md`

## Capture guidance

Start conservative and ramp:

| framesize | jpeg_quality | frame_interval_ms |
|-----------|--------------|-------------------|
| QVGA      | 12           | 200               |
| QVGA      | 12           | 100               |
| QVGA      | 15           | 67                |
| QVGA      | 18           | 33                |

Note: in the ESP camera driver a **higher** `jpeg_quality` number means **more**
compression (worse image). For a moving conveyor, prefer strong light + short
exposure (less motion blur) over higher resolution.

## Operator commands

Entrypoints (see `Makefile`) so you use commands instead of remembering them:

| Command | What it does |
|---------|--------------|
| `make venv` | create `hub/.venv` and install deps (incl. tests) |
| `make hub` | run the hub on `0.0.0.0:5000` |
| `make test-hub` | run the hub test suite |
| `make check-session SESSION=<id>` | validate a captured session (PASS/FAIL) |
| `make firmware-venv` | create `firmware/.venv` with PlatformIO (run once) |
| `make firmware-build` / `-upload` / `-monitor` | PlatformIO build / flash / serial |

`make check-session` exits non-zero on FAIL. Pass thresholds via `ARGS` for
strict gating:

```bash
make check-session SESSION=<id> ARGS="--min-frames 100 --max-gaps 0 --min-rssi -70"
```

## Run on Windows

Use this when the PC is itself the Wi-Fi hotspot (or you just prefer Windows).
A hub in **WSL2 cannot reach the hotspot network** — WSL is an isolated NAT,
and even mirrored networking does not bridge the ICS hotspot adapter — but a hub
on **Windows** can. The hub is pure Python and runs natively; `hub/hub.ps1`
mirrors the make targets.

Get the code onto Windows (clone from the WSL share, or just copy the folder):

```powershell
git clone "\\wsl.localhost\<distro>\path\to\ice_conveyor_camera" C:\palletcam
cd C:\palletcam\hub

.\hub.ps1 venv          # create .venv + install deps (incl. tests)
.\hub.ps1 run           # run the hub on 0.0.0.0:5000
.\hub.ps1 test          # run the test suite
.\hub.ps1 check <id> ["--min-frames 100 --max-gaps 0 --min-rssi -70"]
.\hub.ps1 firewall      # allow inbound TCP 5000 (run as Administrator)
```

If script execution is blocked:
`powershell -ExecutionPolicy Bypass -File .\hub.ps1 <command>`.

Point the firmware at the hotspot gateway — in `secrets.h`,
`HUB_BASE_URL "http://192.168.137.1:5000"` (the PC's Mobile-hotspot IP). Verify
from a phone on the hotspot: open `http://192.168.137.1:5000/ready`.

Firmware builds and flashes natively on Windows too — `firmware/firmware.ps1`
runs PlatformIO from `firmware\.venv`:

```powershell
cd C:\palletcam\firmware
.\firmware.ps1 venv         # install PlatformIO (add a version, e.g. "venv 3.12", if needed)
.\firmware.ps1 secrets      # create include\secrets.h from the example, then edit it
.\firmware.ps1 build        # pio run
.\firmware.ps1 ports        # find your COM port
.\firmware.ps1 upload       # flash
.\firmware.ps1 monitor      # serial @ 115200
```

Flashing is simpler than from WSL — a direct COM port, no `usbipd-win` USB
passthrough.

## Where do I change what

| Change | File |
|--------|------|
| Frame interval / session / quality / `enabled` | `hub/hub_config.json` |
| Hub IP / Wi-Fi credentials | `firmware/include/secrets.h` |
| Camera pin mapping | `firmware/include/camera_pins.h` |
| HTTP protocol | `firmware/src/hub_client.cpp` + `hub/pallet_hub/main.py` |
| Dataset format | `hub/pallet_hub/storage.py` + `docs/dataset_format.md` |
| Capture pacing | `firmware/src/capture_loop.cpp` |

## Status

- **Hub:** functional, run-tested locally.
- **Firmware:** complete and written against `espressif32` + Arduino. Build and
  on-hardware milestones (M1–M5) are run by you; confirm PSRAM, flash/PSRAM mode
  and the status-LED GPIO on first boot. See the design doc in
  `docs/superpowers/specs/`.
