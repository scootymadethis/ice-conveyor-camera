import json
from os.path import join, dirname

from dotenv import load_dotenv
from flask import Flask, Response, jsonify, send_file, request, render_template

dotenv_path = join(dirname(__file__), '.env')
load_dotenv(dotenv_path)

import config
import esp.state as state
from config import FRAMES_DIR, LATEST_FRAME, PRESETS_ENABLED, ESP_PORT, WEB_HOST, WEB_PORT
from esp.connection import start_esp_auto_connect, request_esp_reconnect
from esp.esp_client import (
    send_lidar_command,
    send_config_command,
    send_ae_level,
    send_espnow_last,
    send_preset_save,
    send_preset_delete,
    send_preset_get,
    send_preset_list,
)
from services.camera_service import capture_frame
from utils.env_utils import persist_esp_host
from utils.validators import (
    normalize_esp_host,
    normalize_lidar_base_mm,
    normalize_lidar_sample_config,
    normalize_lidar_post_frame_delay_ms,
    normalize_preset_name,
    validate_camera_config,
)

FRAMES_DIR.mkdir(exist_ok=True)

# esp/state.py owns the *runtime* ESP_HOST (it can be changed live from the
# web UI), but the initial value still has to come from config.py / .env,
# otherwise the saved IP_ESP is never picked up on startup.
state.ESP_HOST = config.ESP_HOST

app = Flask(__name__, static_url_path="", static_folder="static")


@app.route("/")
def index():
    return render_template("index.html", presets_enabled=PRESETS_ENABLED)


@app.route("/esp-host", methods=["GET", "POST"])
def esp_host_route():
    if request.method == "GET":
        return jsonify({
            "ok": True,
            "host": state.ESP_HOST,
            "port": ESP_PORT,
            "connected": state.sock is not None and state.sock_file is not None,
        })

    data = request.get_json(force=True)

    try:
        host = normalize_esp_host(data.get("host"))
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    state.ESP_HOST = host

    persisted = True
    try:
        persist_esp_host(host)
    except Exception as e:
        persisted = False
        print("[web] failed to persist IP_ESP:", e)

    request_esp_reconnect()

    return jsonify({
        "ok": True,
        "host": state.ESP_HOST,
        "port": ESP_PORT,
        "persisted": persisted,
    })


@app.route("/capture", methods=["POST"])
def capture_route():
    return jsonify(capture_frame())


@app.route("/latest.jpg")
def latest_frame_route():
    if not LATEST_FRAME.exists():
        return Response("No frame yet", status=404)
    return send_file(LATEST_FRAME, mimetype="image/jpeg")


@app.route("/events")
def events_route():
    def generate():
        last_sent = 0
        last_busy_counter = 0
        while True:
            with state.state_condition:
                state.state_condition.wait(timeout=15)
                pending = [
                    (counter, result)
                    for counter, result in state.frame_results
                    if counter > last_sent
                ]
                busy_counter = state.ui_busy_counter
                busy_payload = {
                    "busy": state.incoming_frame_busy,
                    "reason": state.ui_busy_reason,
                    "counter": busy_counter,
                }

            if busy_counter > last_busy_counter:
                last_busy_counter = busy_counter
                yield "event: busy\n"
                yield "data: " + json.dumps(busy_payload) + "\n\n"

            if pending:
                for counter, result in pending:
                    last_sent = counter
                    yield "event: frame\n"
                    yield "data: " + json.dumps(result) + "\n\n"
            else:
                yield "event: ping\n"
                yield "data: {}\n\n"

    return Response(generate(), mimetype="text/event-stream", headers={
        "Cache-Control": "no-cache",
        "X-Accel-Buffering": "no",
    })


@app.route("/has-frame")
def has_frame_route():
    return jsonify({"exists": LATEST_FRAME.exists()})


@app.route("/frame-status")
def frame_status_route():
    try:
        since = int(request.args.get("since", 0))
    except (TypeError, ValueError):
        since = 0

    with state.state_condition:
        latest_counter = state.frame_counter
        latest_result = state.frame_results[-1][1] if state.frame_results else None

    return jsonify({
        "ok": True,
        "counter": latest_counter,
        "changed": latest_counter > since,
        "latest": latest_result,
        "exists": LATEST_FRAME.exists(),
    })


@app.route("/device-status")
def device_status_route():
    with state.state_condition:
        snapshot = state.device_snapshot
        counter = state.device_snapshot_counter
        busy = state.incoming_frame_busy

    return jsonify({
        "ok": snapshot is not None,
        "snapshot": snapshot,
        "counter": counter,
        "busy": busy,
    })


@app.route("/lidar", methods=["GET"])
def lidar_status_route():
    return jsonify(send_lidar_command("lidar_status"))


@app.route("/lidar/base", methods=["POST"])
def lidar_base_route():
    data = request.get_json(force=True)

    try:
        base_mm = normalize_lidar_base_mm(data.get("base_mm"))
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    return jsonify(send_lidar_command(f"lidar_base {base_mm}"))


@app.route("/lidar/base/current", methods=["POST"])
def lidar_base_current_route():
    return jsonify(send_lidar_command("lidar_base_current"))


@app.route("/lidar/enabled", methods=["POST"])
def lidar_enabled_route():
    data = request.get_json(force=True)
    enabled = bool(data.get("enabled"))
    return jsonify(send_lidar_command(f"lidar_enable {1 if enabled else 0}"))


@app.route("/lidar/sample-config", methods=["POST"])
def lidar_sample_config_route():
    data = request.get_json(force=True)

    try:
        sample_count, delay_ms = normalize_lidar_sample_config(
            data.get("sample_count"),
            data.get("delay_ms"),
        )
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    return jsonify(send_lidar_command(f"lidar_sample_config {sample_count} {delay_ms}"))


@app.route("/lidar/post-frame-delay", methods=["POST"])
def lidar_post_frame_delay_route():
    data = request.get_json(force=True)

    try:
        delay_ms = normalize_lidar_post_frame_delay_ms(data.get("delay_ms"))
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    return jsonify(send_lidar_command(f"lidar_post_frame_delay {delay_ms}"))


@app.route("/espnow/send-last", methods=["POST"])
def espnow_send_last_route():
    return jsonify(send_espnow_last())


@app.route("/ae-level", methods=["POST"])
def ae_level_route():
    data = request.get_json(force=True)

    try:
        ae_level = int(data.get("ae_level", 0))
    except (TypeError, ValueError):
        return jsonify({"ok": False, "error": "ae_level must be between -2 and +2"}), 400

    if ae_level < -2 or ae_level > 2:
        return jsonify({"ok": False, "error": "ae_level must be between -2 and +2"}), 400

    return jsonify(send_ae_level(ae_level))


@app.route("/config", methods=["POST"])
def config_route():
    data = request.get_json(force=True)

    framesize = str(data.get("framesize", "HD")).upper()

    try:
        quality = int(data.get("quality", 10))
        ae = int(data.get("ae", 0))
        contrast = int(data.get("contrast", 0))
        saturation = int(data.get("saturation", 0))
        brightness = int(data.get("brightness", 0))
    except (TypeError, ValueError):
        return jsonify({"ok": False, "error": "Camera config values must be numbers."}), 400

    try:
        validate_camera_config(framesize, quality, ae, contrast, saturation, brightness)
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    return jsonify(send_config_command(framesize, quality, ae, contrast, saturation, brightness))


@app.route("/preset/save", methods=["POST"])
def preset_save_route():
    if not PRESETS_ENABLED:
        return jsonify({"ok": False, "error": "Preset disabled by PRESETS_ENABLED=false"}), 503

    data = request.get_json(force=True)

    try:
        name = normalize_preset_name(data.get("name"))
        framesize = str(data.get("framesize", "HD")).upper()
        quality = int(data.get("quality", 10))
        ae = int(data.get("ae", 0))
        contrast = int(data.get("contrast", 0))
        saturation = int(data.get("saturation", 0))
        brightness = int(data.get("brightness", 0))
        validate_camera_config(framesize, quality, ae, contrast, saturation, brightness)
    except (TypeError, ValueError) as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    payload = {
        "name": name,
        "framesize": framesize,
        "quality": quality,
        "ae": ae,
        "contrast": contrast,
        "saturation": saturation,
        "brightness": brightness,
    }

    result = send_preset_save(payload)
    if not result["ok"]:
        return jsonify(result), 500
    return jsonify(result)


@app.route("/preset/delete", methods=["POST"])
def preset_delete_route():
    if not PRESETS_ENABLED:
        return jsonify({"ok": False, "error": "Preset disabled by PRESETS_ENABLED=false"}), 503

    data = request.get_json(force=True)

    try:
        name = normalize_preset_name(data.get("name"))
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    result = send_preset_delete(name)
    if not result["ok"]:
        return jsonify(result), 500
    return jsonify(result)


@app.route("/preset/<name>", methods=["GET"])
def preset_get_route(name):
    if not PRESETS_ENABLED:
        return jsonify({"ok": False, "error": "Preset disabled by PRESETS_ENABLED=false"}), 503

    try:
        name = normalize_preset_name(name)
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    result = send_preset_get(name)
    if not result["ok"]:
        return jsonify(result), 404
    return jsonify(result)


@app.route("/presets", methods=["GET"])
def presets_route():
    if not PRESETS_ENABLED:
        return jsonify({"ok": False, "error": "Preset disabled by PRESETS_ENABLED=false"}), 503

    result = send_preset_list()
    if not result["ok"]:
        return jsonify(result), 500
    return jsonify(result)


if __name__ == "__main__":
    start_esp_auto_connect()
    print(f"[web] open http://127.0.0.1:{WEB_PORT}")
    app.run(host=WEB_HOST, port=WEB_PORT, debug=False, threaded=True)
