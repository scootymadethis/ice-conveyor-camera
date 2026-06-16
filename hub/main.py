import socket
import time
import threading
from datetime import datetime
from pathlib import Path

from flask import Flask, Response, jsonify, send_file, request, render_template


ESP_HOST = "192.168.0.31"   # IP dell'ESP32
ESP_PORT = 3131

WEB_HOST = "0.0.0.0"
WEB_PORT = 8080

FRAMES_DIR = Path("frames")
LATEST_FRAME = FRAMES_DIR / "latest.jpg"

MAX_FRAME_SIZE = 10_000_000  # 10 MB, più che abbastanza per JPEG ESP32-CAM

FRAMES_DIR.mkdir(exist_ok=True)

app = Flask(__name__, static_url_path="", static_folder="static")

sock = None
sock_file = None
sock_lock = threading.Lock()

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
    data = b""

    while len(data) < size:
        chunk = file.read(size - len(data))

        if not chunk:
            raise ConnectionError("ESP32 closed the connection")

        data += chunk

    return data


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

    print(f"[esp] connecting to {ESP_HOST}:{ESP_PORT}...")

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
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
    FRAME <len>
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

            if len(parts) != 2:
                raise RuntimeError(f"Invalid FRAME header: {text}")

            try:
                frame_len = int(parts[1])
            except ValueError:
                raise RuntimeError(f"Invalid frame length in header: {text}")

            if frame_len <= 0:
                raise RuntimeError(f"Invalid frame length: {frame_len}")

            if frame_len > MAX_FRAME_SIZE:
                raise RuntimeError(
                    f"Frame too large or protocol desync: {frame_len}"
                )

            return frame_len

        # Qualsiasi altra riga tipo "ESP32 connected." viene solo ignorata


def capture_frame():
    global sock, sock_file

    with sock_lock:
        try:
            ensure_esp_connected()

            print("[esp] requesting frame...")

            sock.sendall(b"camera\n")

            frame_len = read_until_frame_header()

            print(f"[esp] receiving frame: {frame_len} bytes")

            frame = recv_exact(sock_file, frame_len)

            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = FRAMES_DIR / f"frame_{timestamp}.jpg"

            with open(filename, "wb") as f:
                f.write(frame)

            with open(LATEST_FRAME, "wb") as f:
                f.write(frame)

            print(f"[disk] saved {filename}")
            print(f"[disk] updated {LATEST_FRAME}")

            return {
                "ok": True,
                "filename": str(filename),
                "latest": str(LATEST_FRAME),
                "bytes": len(frame),
            }

        except Exception as e:
            print("[esp] error:", e)
            close_esp_connection()

            return {
                "ok": False,
                "error": str(e),
            }


@app.route("/")
def index():
    return render_template("index.html")


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

@app.route("/config", methods=["POST"])
def config_route():
    global sock, sock_file

    data = request.get_json(force=True)

    framesize = str(data.get("framesize", "HD")).upper()
    quality = int(data.get("quality", 10))

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

    with sock_lock:
        try:
            ensure_esp_connected()

            command = f"config {framesize} {quality}\n"
            print("[esp] sending:", command.strip())

            sock.sendall(command.encode())

            result = read_until_config_response()

            if not result["ok"]:
                return jsonify(result)

            return jsonify({
                "ok": True,
                "framesize": framesize,
                "quality": quality,
                "message": result["message"],
            })

        except Exception as e:
            print("[esp] config error:", e)
            close_esp_connection()

            return jsonify({
                "ok": False,
                "error": str(e),
            })

if __name__ == "__main__":
    print(f"[web] open http://127.0.0.1:{WEB_PORT}")
    app.run(host=WEB_HOST, port=WEB_PORT, debug=False, threaded=True)