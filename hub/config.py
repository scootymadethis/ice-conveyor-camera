from pathlib import Path
import os

ESP_HOST = os.environ.get("IP_ESP", "").strip()   # IP dell'ESP32
ESP_PORT = 3131
PRESETS_ENABLED = os.environ.get("PRESETS_ENABLED", "true").strip().lower() not in {"0", "false", "no", "off"}

WEB_HOST = "0.0.0.0"
WEB_PORT = 8080

FRAMES_DIR = Path("frames")
LATEST_FRAME = FRAMES_DIR / "latest.jpg"

MAX_FRAME_SIZE = 10_000_000          # 10 MB, più che abbastanza per JPEG ESP32-CAM
SOCKET_READ_CHUNK_SIZE = 64 * 1024
SOCKET_CONNECT_TIMEOUT_SECONDS = 10
SOCKET_IO_TIMEOUT_SECONDS = 75
SOCKET_BUFFER_SIZE = 256 * 1024
LIDAR_OUT_OF_RANGE_DISTANCE_MM = 8191