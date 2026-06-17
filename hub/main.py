import socket
import time
import threading
import shutil
from datetime import datetime
from pathlib import Path
import os
from os.path import join, dirname
from dotenv import load_dotenv
import json
from flask import Flask, Response, jsonify, send_file, request, render_template

dotenv_path = join(dirname(__file__), '.env')
load_dotenv(dotenv_path)

ESP_HOST = os.environ.get("IP_ESP", "").strip()   # IP dell'ESP32
ESP_PORT = 3131

WEB_HOST = "0.0.0.0"
WEB_PORT = 8080

FRAMES_DIR = Path("frames")
LATEST_FRAME = FRAMES_DIR / "latest.jpg"

MAX_FRAME_SIZE = 10_000_000  # 10 MB, più che abbastanza per JPEG ESP32-CAM
SOCKET_READ_CHUNK_SIZE = 64 * 1024

FRAMES_DIR.mkdir(exist_ok=True)

app = Flask(__name__, static_url_path="", static_folder="static")

sock = None
sock_file = None
sock_lock = threading.Lock()

def read_until_esp_preset_response(ok_prefixes):
    while True:
        line = sock_file.readline()

        if not line:
            raise ConnectionError("ESP32 closed the connection")

        text = line.decode(errors="ignore").strip()

        if not text:
            continue

        print("[esp]", text)

        if text.startswith("PRESET_ERROR"):
            payload = text[len("PRESET_ERROR"):].strip()

            try:
                data = json.loads(payload)
            except Exception:
                data = {
                    "ok": False,
                    "error": payload or text,
                }

            return {
                "ok": False,
                "message": text,
                "data": data,
                "error": data.get("error", text) if isinstance(data, dict) else text,
            }

        for prefix in ok_prefixes:
            if text.startswith(prefix):
                payload = text[len(prefix):].strip()

                try:
                    data = json.loads(payload)
                except Exception:
                    data = {
                        "ok": True,
                        "raw": payload,
                    }

                return {
                    "ok": True,
                    "message": text,
                    "data": data,
                }

        # Ignora righe vecchie tipo "ESP32 connected."

def read_until_config_response():
    while True:
        line = sock_file.readline()

        if not line:
            raise ConnectionError("ESP32 closed the connection")

        text = line.decode(errors="ignore").strip()

        if not text:
            continue

        print("[esp]", text)

        if text.startswith("CONFIG_OK"):
            return {
                "ok": True,
                "message": text,
            }

        if text.startswith("CONFIG_ERROR"):
            return {
                "ok": False,
                "error": text,
            }

        # Ignora righe vecchie tipo "ESP32 connected."

def recv_exact(file, size):
    """Read exactly size bytes without repeated bytes concatenations."""
    buffer = bytearray(size)
    view = memoryview(buffer)
    received = 0

    while received < size:
        wanted = min(SOCKET_READ_CHUNK_SIZE, size - received)
        read_count = file.readinto(view[received:received + wanted])

        if not read_count:
            raise ConnectionError("ESP32 closed the connection")

        received += read_count

    return bytes(buffer)


def save_frame_files(frame, filename):
    """Write the JPEG once, then point both latest.jpg and archive file to it when possible."""
    tmp_latest = LATEST_FRAME.with_suffix(".jpg.tmp")

    with open(tmp_latest, "wb") as f:
        f.write(frame)

    os.replace(tmp_latest, LATEST_FRAME)

    try:
        if filename.exists():
            filename.unlink()
        os.link(LATEST_FRAME, filename)
    except OSError:
        shutil.copyfile(LATEST_FRAME, filename)


def normalize_esp_host(value):
    host = str(value or "").strip()

    if host.startswith("http://"):
        host = host[len("http://"):].strip()
    elif host.startswith("https://"):
        host = host[len("https://"):].strip()

    host = host.split("/", 1)[0].strip()

    if ":" in host:
        host = host.split(":", 1)[0].strip()

    if not host:
        raise ValueError("IP ESP richiesto.")

    if any(ch.isspace() for ch in host):
        raise ValueError("IP ESP non valido: non deve contenere spazi.")

    return host


def persist_esp_host(host):
    env_path = Path(dotenv_path)
    lines = []

    if env_path.exists():
        lines = env_path.read_text().splitlines()

    updated = False
    output = []

    for line in lines:
        if line.startswith("IP_ESP="):
            output.append(f"IP_ESP={host}")
            updated = True
        else:
            output.append(line)

    if not updated:
        output.append(f"IP_ESP={host}")

    env_path.write_text("\n".join(output) + "\n")


def close_esp_connection():
    global sock, sock_file

    if sock_file:
        try:
            sock_file.close()
        except Exception:
            pass

    if sock:
        try:
            sock.close()
        except Exception:
            pass

    sock = None
    sock_file = None


def connect_to_esp():
    global sock, sock_file

    close_esp_connection()

    if not ESP_HOST:
        raise RuntimeError("IP ESP non configurato. Impostalo dalla pagina web o nel file .env.")

    print(f"[esp] connecting to {ESP_HOST}:{ESP_PORT}...")

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    s.settimeout(5)
    s.connect((ESP_HOST, ESP_PORT))
    s.settimeout(None)

    sock = s
    sock_file = s.makefile("rb")

    print("[esp] connected")


def ensure_esp_connected():
    global sock, sock_file

    if sock is None or sock_file is None:
        connect_to_esp()


def read_until_frame_header():
    """
    Legge righe testuali dall'ESP32 finché trova:
    FRAME <len> CAPTURE_MS <ms>
    oppure CAMERA_ERROR
    """
    while True:
        line = sock_file.readline()

        if not line:
            raise ConnectionError("ESP32 closed the connection")

        text = line.decode(errors="ignore").strip()

        if not text:
            continue

        print("[esp]", text)

        if text == "CAMERA_ERROR":
            raise RuntimeError("ESP32 camera capture failed")

        if text.startswith("FRAME "):
            parts = text.split()

            # Handles both old "FRAME 45000" and new "FRAME 45000 CAPTURE_MS 120"
            if len(parts) < 2:
                raise RuntimeError(f"Invalid FRAME header: {text}")

            try:
                frame_len = int(parts[1])
            except ValueError:
                raise RuntimeError(f"Invalid frame length in header: {text}")

            # Parse ESP32 capture time if available in header
            esp_capture_ms = 0
            if len(parts) >= 4 and parts[2] == "CAPTURE_MS":
                try:
                    esp_capture_ms = int(parts[3])
                except ValueError:
                    pass

            if frame_len <= 0:
                raise RuntimeError(f"Invalid frame length: {frame_len}")

            if frame_len > MAX_FRAME_SIZE:
                raise RuntimeError(f"Frame too large or protocol desync: {frame_len}")

            return {"frame_len": frame_len, "esp_capture_ms": esp_capture_ms}



def capture_frame():
    global sock, sock_file

    with sock_lock:
        try:
            ensure_esp_connected()

            print("[esp] requesting frame...")
            sock.sendall(b"camera\n")

            # Get image metadata from ESP32
            header_data = read_until_frame_header()
            frame_len = header_data["frame_len"]
            esp_capture_ms = header_data["esp_capture_ms"]

            print(f"[esp] receiving frame: {frame_len} bytes")

            # ── METRIC TIMING: START NETWORK RECEIVE ────────────────────────
            t_network_start = time.perf_counter()
            
            frame = recv_exact(sock_file, frame_len)
            
            # ── METRIC TIMING: STOP NETWORK RECEIVE ─────────────────────────
            t_network_ms = (time.perf_counter() - t_network_start) * 1000.0

            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = FRAMES_DIR / f"frame_{timestamp}.jpg"

            save_frame_files(frame, filename)

            print(f"[disk] saved {filename}")
            print(f"[disk] updated {LATEST_FRAME}")

            transfer_kbps = (len(frame) * 8.0 / t_network_ms) if t_network_ms > 0 else 0.0

            # Print latency statistics to python terminal
            print(f"[metrics] ESP32 Capture: {esp_capture_ms:.1f} ms | Network Transit: {t_network_ms:.1f} ms | Total Pipeline: {esp_capture_ms + t_network_ms:.1f} ms | {transfer_kbps:.0f} kbit/s")

            return {
                "ok": True,
                "filename": str(filename),
                "latest": str(LATEST_FRAME),
                "bytes": len(frame),
                "timing": {
                    "esp_capture_ms": esp_capture_ms,
                    "network_transit_ms": round(t_network_ms, 2),
                    "total_ms": round(esp_capture_ms + t_network_ms, 2),
                    "transfer_kbps": round(transfer_kbps, 2)
                }
            }

        except Exception as e:
            print("[esp] error:", e)
            close_esp_connection()

            return {
                "ok": False,
                "error": str(e),
            }

def normalize_preset_name(name):
    name = str(name or "").strip().lower()

    if not name:
        raise ValueError("Preset name is required.")

    if len(name) > 32:
        raise ValueError("Preset name is too long. Max 32 characters.")

    for ch in name:
        if not (ch.isalnum() or ch in ["_", "-"]):
            raise ValueError("Preset name can contain only letters, numbers, underscore and dash.")

    return name


@app.route("/")
def index():
    return render_template("index.html")



@app.route("/esp-host", methods=["GET", "POST"])
def esp_host_route():
    global ESP_HOST

    if request.method == "GET":
        return jsonify({
            "ok": True,
            "host": ESP_HOST,
            "port": ESP_PORT,
            "connected": sock is not None and sock_file is not None,
        })

    data = request.get_json(force=True)

    try:
        host = normalize_esp_host(data.get("host"))
    except ValueError as e:
        return jsonify({
            "ok": False,
            "error": str(e),
        }), 400

    with sock_lock:
        ESP_HOST = host
        close_esp_connection()

        persisted = True
        try:
            persist_esp_host(host)
        except Exception as e:
            persisted = False
            print("[web] failed to persist IP_ESP:", e)

    return jsonify({
        "ok": True,
        "host": ESP_HOST,
        "port": ESP_PORT,
        "persisted": persisted,
    })

@app.route("/capture", methods=["POST"])
def capture_route():
    result = capture_frame()
    return jsonify(result)


@app.route("/latest.jpg")
def latest_frame_route():
    if not LATEST_FRAME.exists():
        return Response("No frame yet", status=404)

    return send_file(LATEST_FRAME, mimetype="image/jpeg")

@app.route("/has-frame")
def has_frame_route():
    return jsonify({
        "exists": LATEST_FRAME.exists()
    })

@app.route("/ae-level", methods=["POST"])
def ae_level_route():
    global sock, sock_file

    data = request.get_json(force=True)

    try:
        ae_level = int(data.get("ae_level", 0))
    except (TypeError, ValueError):
        return jsonify({"ok": False, "error": "ae_level must be between -2 and +2"})
    
    with sock_lock:
        try:
            ensure_esp_connected()

            command = f"ae_level {ae_level}\n"
            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            # legge risposta AE_OK / AE_ERROR
            while True:
                line = sock_file.readline()
                if not line:
                    raise ConnectionError("ESP32 closed the connection")
                text = line.decode(errors="ignore").strip()
                if not text:
                    continue
                print("[esp]", text)
                if text.startswith("AE_OK"):
                    return jsonify({"ok": True, "ae_level": ae_level})
                if text.startswith("AE_ERROR"):
                    return jsonify({"ok": False, "error": text})
                
        except Exception as e:
            print("[esp] ae_level error:", e)
            close_esp_connection()
            return jsonify({"ok": False, "error": str(e)})

@app.route("/config", methods=["POST"])
def config_route():
    global sock, sock_file

    data = request.get_json(force=True)

    framesize = str(data.get("framesize", "HD")).upper()
    quality = int(data.get("quality", 10))
    ae = int(data.get("ae", 0))
    contrast = int(data.get("contrast", 0))
    saturation = int(data.get("saturation", 0))
    brightness = int(data.get("brightness", 0))

    allowed_framesizes = {
        "QQVGA", "QVGA", "CIF", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"
    }

    if framesize not in allowed_framesizes:
        return jsonify({
            "ok": False,
            "error": f"Invalid framesize: {framesize}",
        })

    if quality < 4 or quality > 63:
        return jsonify({
            "ok": False,
            "error": "Quality must be between 4 and 63. Lower number = better quality.",
        })
    
    if ae < -2 or ae > 2:
        return jsonify({
            "ok": False,
            "error": "Exposure must be between -2 and 2."
        })

    if contrast < -2 or contrast > 2:
        return jsonify({
            "ok": False,
            "error": "Contrast must be between -2 and 2."
        })
    
    if saturation < -2 or saturation > 2:
        return jsonify({
            "ok": False,
            "error": "Saturation must be between -2 and 2."
        })
    
    if brightness < -2 or brightness > 2:
        return jsonify({
            "ok": False,
            "error": "Brightness must be between -2 and 2."
        })
    
    with sock_lock:
        try:
            ensure_esp_connected()

            command = f"config {framesize} {quality} {ae} {contrast} {saturation} {brightness}\n"
            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            result = read_until_config_response()

            if not result["ok"]:
                return jsonify(result)

            return jsonify({
                "ok": True,
                "framesize": framesize,
                "quality": quality,
                "ae": ae,
                "contrast": contrast,
                "saturation": saturation,
                "brightness": brightness,
                "message": result["message"],
            })

        except Exception as e:
            print("[esp] config error:", e)
            close_esp_connection()

            return jsonify({
                "ok": False,
                "error": str(e),
            })

@app.route("/preset/save", methods=["POST"])
def preset_save_route():
    global sock, sock_file

    data = request.get_json(force=True)

    try:
        name = normalize_preset_name(data.get("name"))
        framesize = str(data.get("framesize", "HD")).upper()

        quality = int(data.get("quality", 10))
        ae = int(data.get("ae", 0))
        contrast = int(data.get("contrast", 0))
        saturation = int(data.get("saturation", 0))
        brightness = int(data.get("brightness", 0))

    except (TypeError, ValueError) as e:
        return jsonify({
            "ok": False,
            "error": str(e),
        }), 400

    allowed_framesizes = {
        "QQVGA", "QVGA", "CIF", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"
    }

    if framesize not in allowed_framesizes:
        return jsonify({
            "ok": False,
            "error": f"Invalid framesize: {framesize}",
        }), 400

    if quality < 4 or quality > 63:
        return jsonify({
            "ok": False,
            "error": "Quality must be between 4 and 63. Lower number = better quality.",
        }), 400

    if ae < -2 or ae > 2:
        return jsonify({
            "ok": False,
            "error": "Exposure must be between -2 and 2.",
        }), 400

    if contrast < -2 or contrast > 2:
        return jsonify({
            "ok": False,
            "error": "Contrast must be between -2 and 2.",
        }), 400

    if saturation < -2 or saturation > 2:
        return jsonify({
            "ok": False,
            "error": "Saturation must be between -2 and 2.",
        }), 400

    if brightness < -2 or brightness > 2:
        return jsonify({
            "ok": False,
            "error": "Brightness must be between -2 and 2.",
        }), 400

    payload = {
        "name": name,
        "framesize": framesize,
        "quality": quality,
        "ae": ae,
        "contrast": contrast,
        "saturation": saturation,
        "brightness": brightness,
    }

    with sock_lock:
        try:
            ensure_esp_connected()

            command = "preset_save " + json.dumps(payload, separators=(",", ":")) + "\n"

            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            result = read_until_esp_preset_response(["PRESET_SAVE_OK"])

            if not result["ok"]:
                return jsonify({
                    "ok": False,
                    "error": result["error"],
                    "esp": result["data"],
                }), 500

            return jsonify({
                "ok": True,
                "preset": result["data"],
            })

        except Exception as e:
            print("[esp] preset save error:", e)
            close_esp_connection()

            return jsonify({
                "ok": False,
                "error": str(e),
            }), 500

@app.route("/preset/delete", methods=["POST"])
def preset_delete_route():
    global sock, sock_file

    data = request.get_json(force=True)

    try:
        name = normalize_preset_name(data.get("name"))
    except ValueError as e:
        return jsonify({
            "ok": False,
            "error": str(e),
        }), 400

    with sock_lock:
        try:
            ensure_esp_connected()

            command = f"preset_delete {name}\n"

            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            result = read_until_esp_preset_response(["PRESET_DELETE_OK"])

            if not result["ok"]:
                return jsonify({
                    "ok": False,
                    "error": result["error"],
                    "esp": result["data"],
                }), 500

            return jsonify({
                "ok": True,
                "preset": result["data"],
            })

        except Exception as e:
            print("[esp] preset delete error:", e)
            close_esp_connection()

            return jsonify({
                "ok": False,
                "error": str(e),
            }), 500
        
@app.route("/preset/<name>", methods=["GET"])
def preset_get_route(name):
    global sock, sock_file

    try:
        name = normalize_preset_name(name)
    except ValueError as e:
        return jsonify({
            "ok": False,
            "error": str(e),
        }), 400

    with sock_lock:
        try:
            ensure_esp_connected()

            command = f"preset_get {name}\n"

            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            result = read_until_esp_preset_response(["PRESET_GET_OK"])

            if not result["ok"]:
                return jsonify({
                    "ok": False,
                    "error": result["error"],
                    "esp": result["data"],
                }), 404

            return jsonify({
                "ok": True,
                "preset": result["data"],
            })

        except Exception as e:
            print("[esp] preset get error:", e)
            close_esp_connection()

            return jsonify({
                "ok": False,
                "error": str(e),
            }), 500

@app.route("/presets", methods=["GET"])
def presets_route():
    global sock, sock_file

    with sock_lock:
        try:
            ensure_esp_connected()

            command = "list_presets\n"

            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            result = read_until_esp_preset_response(["PRESETS_OK"])

            if not result["ok"]:
                return jsonify({
                    "ok": False,
                    "error": result["error"],
                    "esp": result["data"],
                }), 500

            return jsonify({
                "ok": True,
                "presets": result["data"],
            })

        except Exception as e:
            print("[esp] presets list error:", e)
            close_esp_connection()

            return jsonify({
                "ok": False,
                "error": str(e),
            }), 500

if __name__ == "__main__":
    print(f"[web] open http://127.0.0.1:{WEB_PORT}")
    app.run(host=WEB_HOST, port=WEB_PORT, debug=False, threaded=True)