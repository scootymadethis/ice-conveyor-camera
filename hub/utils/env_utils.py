"""Utilities for persisting key=value pairs in the .env file."""
from pathlib import Path

# Hub root is two levels up from this file (hub/utils/env_utils.py → hub/)
_ENV_PATH = Path(__file__).parent.parent / ".env"


def persist_env_value(key: str, value: str) -> None:
    lines = _ENV_PATH.read_text().splitlines() if _ENV_PATH.exists() else []
    prefix = key + "="
    updated = False
    output = []
    for line in lines:
        if line.startswith(prefix):
            output.append(f"{key}={value}")
            updated = True
        else:
            output.append(line)
    if not updated:
        output.append(f"{key}={value}")
    _ENV_PATH.write_text("\n".join(output) + "\n")


def persist_esp_host(host: str) -> None:
    persist_env_value("IP_ESP", host)