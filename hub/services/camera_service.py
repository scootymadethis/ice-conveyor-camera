"""Camera capture service."""
import esp.state as state
from esp.connection import close_esp_connection, ensure_esp_connected
from esp.receiver import wait_for_next_frame


def capture_frame() -> dict:
    """Request a JPEG frame from the ESP32 and return a result dict."""
    try:
        with state.state_condition:
            previous_counter = state.frame_counter

        with state.sock_lock:
            if state.incoming_frame_busy:
                return {"ok": False, "error": "ESP occupato: sta inviando un frame. Attendi la fine del trasferimento."}

            ensure_esp_connected()
            print("[esp] requesting frame...")
            state.sock.sendall(b"camera\n")

            # Keep the command lock until the frame is fully received: one protocol
            # operation at a time between Hub and ESP. The receiver thread still
            # reads the socket and sends READY/ACK; web routes simply wait.
            result = wait_for_next_frame(previous_counter)

        if not result.get("ok"):
            raise RuntimeError(result.get("error", "ESP32 camera capture failed"))

        return result

    except Exception as exc:
        print("[esp] error:", exc)
        close_esp_connection()
        return {"ok": False, "error": str(exc)}