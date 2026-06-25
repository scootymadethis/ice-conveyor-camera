"""TCP connection management for the ESP32."""
import socket as _socket
import time
import threading

import esp.state as state
from config import (
    ESP_PORT,
    SOCKET_CONNECT_TIMEOUT_SECONDS,
    SOCKET_IO_TIMEOUT_SECONDS,
    SOCKET_BUFFER_SIZE,
)


def connect_to_esp() -> None:
    from esp.receiver import start_esp_receiver  # lazy: breaks circular import

    close_esp_connection()

    if not state.ESP_HOST:
        raise RuntimeError(
            "IP ESP non configurato. Impostalo dalla pagina web o nel file .env."
        )

    print(f"[esp] connecting to {state.ESP_HOST}:{ESP_PORT}...")

    s = _socket.socket(_socket.AF_INET, _socket.SOCK_STREAM)
    s.setsockopt(_socket.IPPROTO_TCP, _socket.TCP_NODELAY, 1)
    s.setsockopt(_socket.SOL_SOCKET, _socket.SO_KEEPALIVE, 1)
    s.setsockopt(_socket.SOL_SOCKET, _socket.SO_RCVBUF, SOCKET_BUFFER_SIZE)
    s.settimeout(SOCKET_CONNECT_TIMEOUT_SECONDS)
    s.connect((state.ESP_HOST, ESP_PORT))
    s.settimeout(SOCKET_IO_TIMEOUT_SECONDS)

    state.sock = s
    state.sock_file = s.makefile("rb")
    print("[esp] connected")
    state.rx_stop = False
    start_esp_receiver()


def close_esp_connection() -> None:
    state.rx_stop = True

    if state.sock_file:
        try:
            state.sock_file.close()
        except Exception:
            pass

    if state.sock:
        try:
            state.sock.close()
        except Exception:
            pass

    state.sock = None
    state.sock_file = None

    with state.state_condition:
        state.command_lines.clear()
        state.state_condition.notify_all()


def ensure_esp_connected() -> None:
    from esp.receiver import start_esp_receiver  # lazy: breaks circular import

    apply_pending_reconnect_if_needed()
    if state.sock is None or state.sock_file is None:
        connect_to_esp()
    else:
        start_esp_receiver()


def apply_pending_reconnect_if_needed() -> None:
    if state.connection_reset_requested:
        state.connection_reset_requested = False
        close_esp_connection()


def request_esp_reconnect() -> None:
    """Close the current socket without blocking the caller.

    If the lock is free we close immediately; otherwise we set a flag so
    the next command or auto-connect loop does it.
    """
    acquired = state.sock_lock.acquire(blocking=False)
    if acquired:
        try:
            state.connection_reset_requested = False
            close_esp_connection()
        finally:
            state.sock_lock.release()
    else:
        state.connection_reset_requested = True


# ---------------------------------------------------------------------------
# Auto-connect background thread
# ---------------------------------------------------------------------------

def _auto_connect_loop() -> None:
    while not state.auto_connect_stop:
        try:
            needs_connect = (
                state.connection_reset_requested
                or state.sock is None
                or state.sock_file is None
            )
            if state.ESP_HOST and needs_connect:
                with state.sock_lock:
                    apply_pending_reconnect_if_needed()
                    if state.sock is None or state.sock_file is None:
                        connect_to_esp()
        except Exception as exc:
            print("[esp] auto-connect failed:", exc)
            close_esp_connection()

        time.sleep(2.0)


def start_esp_auto_connect() -> None:
    if state.auto_connect_thread is not None and state.auto_connect_thread.is_alive():
        return
    state.auto_connect_stop = False
    state.auto_connect_thread = threading.Thread(
        target=_auto_connect_loop, daemon=True
    )
    state.auto_connect_thread.start()