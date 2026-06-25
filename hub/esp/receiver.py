"""ESP32 receiver loop and all line-level protocol parsers."""
import json
import socket
import threading
import time
from datetime import datetime

import esp.state as state
from config import (
    FRAMES_DIR,
    LATEST_FRAME,
    MAX_FRAME_SIZE,
    SOCKET_IO_TIMEOUT_SECONDS,
    SOCKET_READ_CHUNK_SIZE,
)
from utils.file_utils import save_frame_files


# ---------------------------------------------------------------------------
# Low-level I/O helpers
# ---------------------------------------------------------------------------

def recv_exact(sock_: socket.socket, size: int) -> bytes:
    """Read exactly *size* bytes without repeated string concatenations."""
    buf = bytearray(size)
    view = memoryview(buf)
    received = 0
    while received < size:
        wanted = min(SOCKET_READ_CHUNK_SIZE, size - received)
        try:
            n = sock_.recv_into(view[received : received + wanted])
        except socket.timeout:
            raise TimeoutError(
                f"Timeout while receiving frame: {received}/{size} bytes received"
            )
        if not n:
            raise ConnectionError(
                f"ESP32 closed the connection after {received}/{size} bytes"
            )
        received += n
    return bytes(buf)


# ---------------------------------------------------------------------------
# Frame header parsing
# ---------------------------------------------------------------------------

def parse_frame_header(text: str) -> dict:
    """Parse a FRAME header line and return metadata dict.

    Supported forms:
        FRAME 45000
        FRAME 45000 CAPTURE_MS 120
        FRAME 45000 REASON lidar CAPTURE_MS 120 DISTANCE_MM 87
    """
    parts = text.split()
    if len(parts) < 2:
        raise RuntimeError(f"Invalid FRAME header: {text}")

    try:
        frame_len = int(parts[1])
    except ValueError:
        raise RuntimeError(f"Invalid frame length in header: {text}")

    meta = {"reason": "unknown", "esp_capture_ms": 0, "distance_mm": None}
    idx = 2
    while idx + 1 < len(parts):
        key, value = parts[idx], parts[idx + 1]
        if key == "REASON":
            meta["reason"] = value
        elif key == "CAPTURE_MS":
            try:
                meta["esp_capture_ms"] = int(value)
            except ValueError:
                pass
        elif key == "DISTANCE_MM":
            try:
                meta["distance_mm"] = int(value)
            except ValueError:
                pass
        idx += 2

    if frame_len <= 0:
        raise RuntimeError(f"Invalid frame length: {frame_len}")
    if frame_len > MAX_FRAME_SIZE:
        raise RuntimeError(f"Frame too large or protocol desync: {frame_len}")

    meta["frame_len"] = frame_len
    return meta


def read_until_frame_header() -> dict:
    """Block until a FRAME header arrives and return its parsed metadata."""
    while True:
        text = wait_for_esp_line()
        if text == "CAMERA_ERROR":
            raise RuntimeError("ESP32 camera capture failed")
        if text.startswith("FRAME "):
            return parse_frame_header(text)




def set_incoming_frame_busy(is_busy: bool, reason: str = "frame") -> None:
    """Expose ESP-initiated frame transfer state to routes and SSE clients."""
    with state.state_condition:
        state.incoming_frame_busy = is_busy
        state.ui_busy_reason = reason if is_busy else ""
        state.ui_busy_counter += 1
        state.state_condition.notify_all()


def remember_device_snapshot(payload: str) -> None:
    try:
        snapshot = json.loads(payload)
    except Exception as exc:
        print("[esp] invalid BOOT_CONFIG payload:", exc)
        return

    with state.state_condition:
        state.device_snapshot = snapshot
        state.device_snapshot_counter += 1
        state.state_condition.notify_all()

# ---------------------------------------------------------------------------
# Frame receipt and storage
# ---------------------------------------------------------------------------

def handle_incoming_frame(header_text: str) -> None:
    set_incoming_frame_busy(True, "frame")
    try:
        _handle_incoming_frame_locked(header_text)
    finally:
        set_incoming_frame_busy(False)


def _handle_incoming_frame_locked(header_text: str) -> None:
    meta = parse_frame_header(header_text)
    frame_len = meta["frame_len"]
    esp_capture_ms = meta["esp_capture_ms"]
    reason = meta.get("reason", "unknown")
    distance_mm = meta.get("distance_mm")

    if reason == "lidar" and distance_mm is not None:
        print(f"[esp] receiving lidar frame: {frame_len} bytes distance_mm={distance_mm}")
    else:
        print(f"[esp] receiving frame: {frame_len} bytes reason={reason}")

    t0 = time.perf_counter()
    state.sock.sendall(b"READY\n")
    frame = recv_exact(state.sock, frame_len)
    t_net_ms = (time.perf_counter() - t0) * 1000.0

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    filename = FRAMES_DIR / f"frame_{timestamp}.jpg"
    save_frame_files(frame, filename)

    kbps = (len(frame) * 8.0 / t_net_ms) if t_net_ms > 0 else 0.0
    result = {
        "ok": True,
        "filename": str(filename),
        "latest": str(LATEST_FRAME),
        "bytes": len(frame),
        "reason": reason,
        "distance_mm": distance_mm,
        "received_at": datetime.now().isoformat(timespec="seconds"),
        "timing": {
            "esp_capture_ms": esp_capture_ms,
            "network_transit_ms": round(t_net_ms, 2),
            "total_ms": round(esp_capture_ms + t_net_ms, 2),
            "transfer_kbps": round(kbps, 2),
        },
    }

    print(f"[disk] saved {filename}")
    print(f"[disk] updated {LATEST_FRAME}")
    print(
        f"[metrics] ESP32 Capture: {esp_capture_ms:.1f} ms | "
        f"Network Transit: {t_net_ms:.1f} ms | "
        f"Total Pipeline: {esp_capture_ms + t_net_ms:.1f} ms | "
        f"{kbps:.0f} kbit/s"
    )

    with state.state_condition:
        state.frame_counter += 1
        result["counter"] = state.frame_counter
        state.frame_results.append((state.frame_counter, result))
        del state.frame_results[:-20]   # keep only the 20 most recent
        state.state_condition.notify_all()



def acknowledge_photo_request(request_text: str) -> None:
    """ACK a firmware-initiated photo request as fast as possible."""
    if state.sock is None:
        raise ConnectionError("ESP32 socket is not connected")
    print(f"[esp] accepting photo request: {request_text}")
    state.sock.sendall(b"ACK\n")


# ---------------------------------------------------------------------------
# Receiver thread
# ---------------------------------------------------------------------------

def _receiver_loop() -> None:
    while not state.rx_stop:
        try:
            if state.sock_file is None:
                time.sleep(0.05)
                continue

            line = state.sock_file.readline()
            if not line:
                raise ConnectionError("ESP32 closed the connection")

            text = line.decode(errors="ignore").strip()
            if not text:
                continue

            print("[esp]", text)

            if text.startswith("PHOTO_REQUEST ") or text.startswith("WAKE "):
                set_incoming_frame_busy(True, "photo_request")
                acknowledge_photo_request(text)
                continue

            if text.startswith("BOOT_CONFIG "):
                remember_device_snapshot(text[len("BOOT_CONFIG "):].strip())
                continue

            if text == "CAMERA_ERROR":
                set_incoming_frame_busy(False)
                with state.state_condition:
                    state.frame_results.append(
                        (state.frame_counter + 1, {"ok": False, "error": "ESP32 camera capture failed"})
                    )
                    state.state_condition.notify_all()
                continue

            if text.startswith("FRAME "):
                handle_incoming_frame(text)
                continue

            with state.state_condition:
                state.command_lines.append(text)
                del state.command_lines[:-100]
                state.state_condition.notify_all()

        except Exception as exc:
            if not state.rx_stop:
                print("[esp] receiver error:", exc)
                from esp.connection import close_esp_connection  # lazy: breaks circular import
                set_incoming_frame_busy(False)
                close_esp_connection()
            return


def start_esp_receiver() -> None:
    if state.rx_thread is not None and state.rx_thread.is_alive():
        return
    state.rx_stop = False
    state.rx_thread = threading.Thread(target=_receiver_loop, daemon=True)
    state.rx_thread.start()


# ---------------------------------------------------------------------------
# Waiting helpers (used by routes and camera_service)
# ---------------------------------------------------------------------------

def wait_for_esp_line(timeout: float = SOCKET_IO_TIMEOUT_SECONDS) -> str:
    """Block until a text line arrives from the ESP32, then return it."""
    deadline = time.monotonic() + timeout
    with state.state_condition:
        while True:
            if state.command_lines:
                return state.command_lines.pop(0)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timeout while waiting for ESP32 response")
            state.state_condition.wait(remaining)


def wait_for_next_frame(previous_counter: int, timeout: float = SOCKET_IO_TIMEOUT_SECONDS) -> dict:
    """Block until a frame with counter > *previous_counter* arrives."""
    deadline = time.monotonic() + timeout
    with state.state_condition:
        while True:
            for counter, result in state.frame_results:
                if counter > previous_counter:
                    return result
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timeout while waiting for frame")
            state.state_condition.wait(remaining)


# ---------------------------------------------------------------------------
# Response readers (one per ESP command family)
# ---------------------------------------------------------------------------

def read_until_command_response(ok_prefixes: list, error_prefixes: list) -> dict:
    while True:
        text = wait_for_esp_line()
        if not text:
            continue
        for prefix in ok_prefixes:
            if text.startswith(prefix):
                payload = text[len(prefix):].strip()
                try:
                    data = json.loads(payload) if payload else {"ok": True}
                except Exception:
                    data = {"ok": True, "raw": payload}
                if isinstance(data, dict):
                    data.setdefault("ok", True)
                return data
        for prefix in error_prefixes:
            if text.startswith(prefix):
                payload = text[len(prefix):].strip()
                try:
                    data = json.loads(payload) if payload else {"ok": False, "error": text}
                except Exception:
                    data = {"ok": False, "error": payload or text}
                if isinstance(data, dict):
                    data["ok"] = False
                    data.setdefault("error", payload or text)
                return data


def read_until_config_response() -> dict:
    while True:
        text = wait_for_esp_line()
        if not text:
            continue
        if text.startswith("CONFIG_OK"):
            return {"ok": True, "message": text}
        if text.startswith("CONFIG_ERROR"):
            return {"ok": False, "error": text}


def read_until_lidar_response() -> dict:
    return read_until_command_response(["LIDAR_OK"], ["LIDAR_ERROR"])


def read_until_espnow_response() -> dict:
    return read_until_command_response(["ESPNOW_OK"], ["ESPNOW_ERROR"])


def read_until_esp_preset_response(ok_prefixes: list) -> dict:
    while True:
        text = wait_for_esp_line()
        if not text:
            continue
        if text.startswith("PRESET_ERROR"):
            payload = text[len("PRESET_ERROR"):].strip()
            try:
                data = json.loads(payload)
            except Exception:
                data = {"ok": False, "error": payload or text}
            err = data.get("error", text) if isinstance(data, dict) else text
            return {"ok": False, "message": text, "data": data, "error": err}
        for prefix in ok_prefixes:
            if text.startswith(prefix):
                payload = text[len(prefix):].strip()
                try:
                    data = json.loads(payload)
                except Exception:
                    data = {"ok": True, "raw": payload}
                return {"ok": True, "message": text, "data": data}