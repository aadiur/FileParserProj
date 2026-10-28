"""
controller/staging.py
─────────────────────────────────────────────────────────────────────────────
Stages input files into the arduino/sdcard/ directory so the Wokwi virtual
microSD card is populated before simulation starts.
"""

import os
import shutil


def stage_file(src_path: str, sdcard_dir: str) -> str:
    """
    Copy src_path into sdcard_dir.
    Returns the destination path.
    Raises FileNotFoundError if src_path does not exist.
    Raises OSError on copy failure.
    """
    if not os.path.isfile(src_path):
        raise FileNotFoundError(f"Input file not found: {src_path}")

    os.makedirs(sdcard_dir, exist_ok=True)
    basename = os.path.basename(src_path)
    dst = os.path.join(sdcard_dir, basename)
    shutil.copy2(src_path, dst)
    return dst


def unstage(sdcard_dir: str, filename: str) -> None:
    """Remove a previously staged file (cleanup utility)."""
    path = os.path.join(sdcard_dir, filename)
    if os.path.isfile(path):
        os.remove(path)
