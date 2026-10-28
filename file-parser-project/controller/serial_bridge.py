"""
controller/serial_bridge.py
─────────────────────────────────────────────────────────────────────────────
Handles serial communication with the Wokwi simulation via RFC2217.

NOTE ON WOKWI SERIAL INTEGRATION
─────────────────────────────────
Wokwi for VS Code (4.0+) exposes a RFC2217 serial port that can be read with
pyserial.  The Wokwi simulation must be STARTED MANUALLY in VS Code before
this bridge will connect.

The required steps are:
  1. Open arduino/sketch.ino in VS Code with the Wokwi extension installed.
  2. Start the simulation (F1 → "Wokwi: Start Simulation").
  3. Then run: python run.py --mode simulation --input input/sample.csv ...

This module will attempt a TCP connection to the RFC2217 port configured in
wokwi.toml (default: localhost:4000).  If the connection fails, it reports
the error clearly and does NOT fabricate serial output.

pyserial must be installed: pip install pyserial
"""

import socket
import sys
import time
from typing import Optional

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 4000
TIMEOUT_S    = 10.0   # connection timeout
READ_TIMEOUT = 30.0   # intra-message read timeout


class SerialBridgeError(Exception):
    pass


def _check_pyserial():
    try:
        import serial  # noqa: F401
        return True
    except ImportError:
        return False


def collect_serial_output(host: str = DEFAULT_HOST,
                           port: int = DEFAULT_PORT,
                           output_path: str = "output/_serial_capture.log") -> str:
    """
    Connect to the Wokwi RFC2217 port and capture all serial output until the
    END or ERR protocol message is received, then save to output_path.

    Returns the path to the captured log.
    Raises SerialBridgeError on any connection or read failure.
    """
    if not _check_pyserial():
        raise SerialBridgeError(
            "pyserial is not installed.  Run: pip install pyserial\n"
            "Then retry in simulation mode."
        )

    import serial  # type: ignore

    url = f"rfc2217://{host}:{port}?logging=debug"
    print(f"[serial_bridge] Connecting to {url} ...")

    try:
        ser = serial.serial_for_url(url, baudrate=115200, timeout=READ_TIMEOUT)
    except Exception as e:
        raise SerialBridgeError(
            f"Cannot connect to Wokwi serial port at {host}:{port}.\n"
            f"  Cause: {e}\n"
            f"  Make sure the Wokwi simulation is RUNNING in VS Code first."
        ) from e

    lines = []
    transfer_done = False
    try:
        deadline = time.time() + 60.0
        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            lines.append(line + "\n")
            print(f"  [serial] {line}")
            if line.startswith("END|") or line.startswith("ERR|"):
                transfer_done = True
                break
    except Exception as e:
        raise SerialBridgeError(f"Serial read error: {e}") from e
    finally:
        ser.close()

    if not transfer_done:
        raise SerialBridgeError(
            "Serial capture timed out before receiving END or ERR message."
        )

    import os
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.writelines(lines)

    print(f"[serial_bridge] Capture saved: {output_path} ({len(lines)} lines)")
    return output_path
