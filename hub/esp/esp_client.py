"""High-level ESP32 command wrappers.

Each function acquires the socket lock, ensures a live connection, sends a
command, waits for the matching response, and returns a plain dict.
"""
import json

import esp.state as state
from esp.connection import close_esp_connection, ensure_esp_connected
from esp.receiver import (
    read_until_config_response,
    read_until_esp_preset_response,
    read_until_espnow_response,
    read_until_lidar_response,
    wait_for_esp_line,
)




def _refuse_if_esp_is_streaming_frame():
    if state.incoming_frame_busy:
        return {"ok": False, "error": "ESP occupato: sta inviando un frame. Attendi la fine del trasferimento."}
    return None

# ---------------------------------------------------------------------------
# LiDAR
# ---------------------------------------------------------------------------

def send_lidar_command(command: str) -> dict:
    with state.sock_lock:
        busy = _refuse_if_esp_is_streaming_frame()
        if busy:
            return busy
        try:
            ensure_esp_connected()
            line = command.strip() + "\n"
            print("[esp] sending:", line.strip())
            state.sock.sendall(line.encode())
            return read_until_lidar_response()
        except Exception as exc:
            print("[esp] lidar error:", exc)
            close_esp_connection()
            return {"ok": False, "error": str(exc)}


# ---------------------------------------------------------------------------
# Camera config
# ---------------------------------------------------------------------------

def send_config_command(framesize, quality, ae, contrast, saturation, brightness) -> dict:
    with state.sock_lock:
        busy = _refuse_if_esp_is_streaming_frame()
        if busy:
            return busy
        try:
            ensure_esp_connected()
            cmd = f"config {framesize} {quality} {ae} {contrast} {saturation} {brightness}\n"
            print("[esp] sending:", cmd.strip())
            state.sock.sendall(cmd.encode())
            result = read_until_config_response()
            if not result["ok"]:
                return result
            return {
                "ok": True,
                "framesize": framesize, "quality": quality, "ae": ae,
                "contrast": contrast, "saturation": saturation, "brightness": brightness,
                "message": result["message"],
            }
        except Exception as exc:
            print("[esp] config error:", exc)
            close_esp_connection()
            return {"ok": False, "error": str(exc)}


def send_ae_level(ae_level: int) -> dict:
    with state.sock_lock:
        busy = _refuse_if_esp_is_streaming_frame()
        if busy:
            return busy
        try:
            ensure_esp_connected()
            cmd = f"ae_level {ae_level}\n"
            print("[esp] sending:", cmd.strip())
            state.sock.sendall(cmd.encode())
            while True:
                text = wait_for_esp_line()
                if not text:
                    continue
                if text.startswith("AE_OK"):
                    return {"ok": True, "ae_level": ae_level}
                if text.startswith("AE_ERROR"):
                    return {"ok": False, "error": text}
        except Exception as exc:
            print("[esp] ae_level error:", exc)
            close_esp_connection()
            return {"ok": False, "error": str(exc)}


# ---------------------------------------------------------------------------
# ESP-NOW
# ---------------------------------------------------------------------------

def send_espnow_last() -> dict:
    with state.sock_lock:
        busy = _refuse_if_esp_is_streaming_frame()
        if busy:
            return busy
        try:
            ensure_esp_connected()
            cmd = "espnow_send_last\n"
            print("[esp] sending:", cmd.strip())
            state.sock.sendall(cmd.encode())
            return read_until_espnow_response()
        except Exception as exc:
            print("[esp] esp-now send error:", exc)
            close_esp_connection()
            return {"ok": False, "error": str(exc)}


# ---------------------------------------------------------------------------
# Presets
# ---------------------------------------------------------------------------

def _send_preset_command(raw_command: str, ok_prefixes: list) -> dict:
    """Send a preset command and normalise the response into a plain dict."""
    with state.sock_lock:
        busy = _refuse_if_esp_is_streaming_frame()
        if busy:
            return busy
        try:
            ensure_esp_connected()
            print("[esp] sending:", raw_command.strip())
            state.sock.sendall(raw_command.encode())
            result = read_until_esp_preset_response(ok_prefixes)
            if result["ok"]:
                return {"ok": True, "preset": result["data"]}
            return {"ok": False, "error": result["error"], "esp": result["data"]}
        except Exception as exc:
            print("[esp] preset error:", exc)
            close_esp_connection()
            return {"ok": False, "error": str(exc)}


def send_preset_save(payload: dict) -> dict:
    cmd = "preset_save " + json.dumps(payload, separators=(",", ":")) + "\n"
    return _send_preset_command(cmd, ["PRESET_SAVE_OK"])


def send_preset_delete(name: str) -> dict:
    return _send_preset_command(f"preset_delete {name}\n", ["PRESET_DELETE_OK"])


def send_preset_get(name: str) -> dict:
    return _send_preset_command(f"preset_get {name}\n", ["PRESET_GET_OK"])


def send_preset_list() -> dict:
    result = _send_preset_command("list_presets\n", ["PRESETS_OK"])
    if result["ok"]:
        return {"ok": True, "presets": result.get("preset")}
    return result