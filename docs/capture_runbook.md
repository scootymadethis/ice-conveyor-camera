# Capture runbook

How to run a real capture session (milestone M5).

## Before you start

- Router powered on, PC joined to `PalletCamLab`.
- PC at `192.168.10.2`, port `5000` allowed through the firewall.
- `firmware/include/secrets.h` filled in and flashed.

## Steps

1. **Power the router.** Wait for it to settle.
2. **Connect the PC** to `PalletCamLab`; confirm its IP is `192.168.10.2`
   (`ipconfig` / `ip addr`).
3. **Pick a session name** and set it in `hub/hub_config.json`, e.g.
   `"session_id": "2026-06-11_test_conveyor"`. Set `"enabled": false` for now.
4. **Start the hub:**
   ```bash
   cd hub && source .venv/bin/activate && python run.py
   ```
   Check `curl http://localhost:5000/ready`.
5. **Power the camera.** Watch the serial log:
   `PSRAM found: yes` → `Camera init OK` → `WiFi connected` → `[hub] ready` →
   `capture disabled, polling...`.
6. **Start capture:** set `"enabled": true` in `hub/hub_config.json`. Within a
   few seconds the camera fetches the new config and begins streaming.
7. **Verify frames are landing:**
   ```bash
   ls data/sessions/<session_id>/frames | head
   tail -n 3 data/sessions/<session_id>/metadata.csv
   ```
8. **Run the conveyor / pass the pallets.**
9. **Stop capture:** set `"enabled": false` (camera stops), then stop the hub
   (Ctrl-C).
10. **Validate, then archive.** Run `make check-session SESSION=<session_id>`
    and confirm `Result: PASS`. Optionally add `notes.md` in the session folder
    (lighting, conveyor speed, anything unusual), then back up
    `data/sessions/<session_id>/`.

## Acceptance criteria (PASS / FAIL)

Don't trust a session by eye — run the checker:

```bash
make check-session SESSION=<session_id>
# stricter gating (exits non-zero on FAIL):
make check-session SESSION=<session_id> ARGS="--min-frames 100 --max-gaps 0 --min-rssi -70"
```

A session is trustworthy when:

- **PSRAM found: yes** in the serial log (else fix `memory_type` in
  `platformio.ini`).
- **frames ≥ target** — at least 100 for a smoke session (`frames_count`).
- **CSV rows == jpg count** — no missing files, no orphans.
- **0 overwrite** — guaranteed by the server-assigned index; the checker also
  reports `missing_files: 0`.
- **0 corrupt/zero JPEGs** (`bad_jpegs`).
- **RSSI above threshold** — e.g. `rssi: min ≥ -70`.
- **No unexpected gaps** — `gaps` ≈ 0 (each gap is a dropped upload). `resets`
  > 0 only if the camera actually rebooted.

The checker exits non-zero on FAIL, so it also works in scripts/CI.

### Reference PASS output

After your first real bench run, paste a known-good `make check-session` output
here so future operators have a concrete baseline to compare against (and so the
realistic RSSI / frame-count / gap thresholds become visible).

```text
<paste your first real PASS output here — not yet captured>
```

## Tuning during a session

You can change `frame_interval_ms`, `framesize`, and `jpeg_quality` in
`hub/hub_config.json` mid-run; the camera picks them up at the next config
refresh (~10 s). Suggested ramp:

| framesize | jpeg_quality | frame_interval_ms |
|-----------|--------------|-------------------|
| QVGA      | 12           | 200               |
| QVGA      | 12           | 100               |
| QVGA      | 15           | 67                |
| QVGA      | 18           | 33                |

Prefer strong light + short exposure (less motion blur) over higher resolution.

## If something's wrong

- **`NO_AP_FOUND` (reason 201) at Wi-Fi connect:** the ESP cannot see the AP —
  this is a **band** problem, not the password. The ESP32-S3 is **2.4 GHz
  only**; set the Wi-Fi / Windows Mobile Hotspot to **2.4 GHz** (not 5 GHz or
  Auto). A wrong password instead gives reason 202 / handshake timeout.
- **Hub unreachable from the ESP when the PC is the hotspot:** do not run the hub
  in WSL2 — it is NAT-isolated from the hotspot network. Run it on Windows (see
  the README "Run on Windows").
- **No `[hub] ready`:** PC IP wrong, firewall blocking 5000, or wrong
  `HUB_BASE_URL` in `secrets.h`.
- **`PSRAM found: no`:** check `board_build.arduino.memory_type` in
  `platformio.ini` against your board.
- **Uploads failing then `returning to HUB_WAIT`:** hub crashed, network
  congestion, or interval too short for the link — increase `frame_interval_ms` / increase jpeg_quality.
