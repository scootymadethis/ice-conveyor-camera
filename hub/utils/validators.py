"""Input normalisation and validation helpers."""
from config import LIDAR_OUT_OF_RANGE_DISTANCE_MM

ALLOWED_FRAMESIZES = {"QQVGA", "QVGA", "CIF", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA"}


def normalize_esp_host(value) -> str:
    host = str(value or "").strip()
    for prefix in ("https://", "http://"):
        if host.startswith(prefix):
            host = host[len(prefix):]
    host = host.split("/", 1)[0].strip()
    if ":" in host:
        host = host.split(":", 1)[0].strip()
    if not host:
        raise ValueError("IP ESP richiesto.")
    if any(ch.isspace() for ch in host):
        raise ValueError("IP ESP non valido: non deve contenere spazi.")
    return host


def normalize_lidar_base_mm(value) -> int:
    try:
        base_mm = int(value)
    except (TypeError, ValueError):
        raise ValueError("Base distance must be a number.")
    if base_mm <= 0 or base_mm > LIDAR_OUT_OF_RANGE_DISTANCE_MM:
        raise ValueError(
            f"Base distance must be between 1 and {LIDAR_OUT_OF_RANGE_DISTANCE_MM} mm."
        )
    return base_mm


def normalize_lidar_sample_config(sample_count_value, delay_ms_value):
    try:
        sample_count = int(sample_count_value)
        delay_ms = int(delay_ms_value)
    except (TypeError, ValueError):
        raise ValueError("LiDAR sample count and delay must be numbers.")
    if sample_count < 1 or sample_count > 25:
        raise ValueError("LiDAR sample count must be between 1 and 25.")
    if delay_ms < 0 or delay_ms > 500:
        raise ValueError("LiDAR sample delay must be between 0 and 500 ms.")
    return sample_count, delay_ms


def normalize_lidar_post_frame_delay_ms(value) -> int:
    try:
        delay_ms = int(value)
    except (TypeError, ValueError):
        raise ValueError("LiDAR post-frame delay must be a number.")
    if delay_ms < 0 or delay_ms > 30000:
        raise ValueError("LiDAR post-frame delay must be between 0 and 30000 ms.")
    return delay_ms


def normalize_preset_name(name) -> str:
    name = str(name or "").strip().lower()
    if not name:
        raise ValueError("Preset name is required.")
    if len(name) > 32:
        raise ValueError("Preset name is too long. Max 32 characters.")
    for ch in name:
        if not (ch.isalnum() or ch in ("_", "-")):
            raise ValueError(
                "Preset name can contain only letters, numbers, underscore and dash."
            )
    return name


def validate_camera_config(framesize, quality, ae, contrast, saturation, brightness) -> None:
    """Raise ValueError if any camera config parameter is out of range."""
    if framesize not in ALLOWED_FRAMESIZES:
        raise ValueError(f"Invalid framesize: {framesize}")
    if quality < 4 or quality > 63:
        raise ValueError("Quality must be between 4 and 63. Lower number = better quality.")
    if ae < -2 or ae > 2:
        raise ValueError("Exposure must be between -2 and 2.")
    if contrast < -2 or contrast > 2:
        raise ValueError("Contrast must be between -2 and 2.")
    if saturation < -2 or saturation > 2:
        raise ValueError("Saturation must be between -2 and 2.")
    if brightness < -2 or brightness > 2:
        raise ValueError("Brightness must be between -2 and 2.")