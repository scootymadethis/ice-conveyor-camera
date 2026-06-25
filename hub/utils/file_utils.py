"""Frame file management utilities."""
import os
import shutil
from pathlib import Path

from config import LATEST_FRAME


def save_frame_files(frame: bytes, filename: Path) -> None:
    """Write *frame* atomically to latest.jpg and hard-link to *filename*.

    Uses an atomic replace so readers never see a partial file.
    Falls back to a copy if cross-device hard-linking is not possible.
    """
    tmp = LATEST_FRAME.with_suffix(".jpg.tmp")
    with open(tmp, "wb") as f:
        f.write(frame)
    os.replace(tmp, LATEST_FRAME)

    try:
        if filename.exists():
            filename.unlink()
        os.link(LATEST_FRAME, filename)
    except OSError:
        shutil.copyfile(LATEST_FRAME, filename)