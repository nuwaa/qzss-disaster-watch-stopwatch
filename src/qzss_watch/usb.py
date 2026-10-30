"""USB CDC transport for the experimental StopWatch display."""
from __future__ import annotations

import json
import os
import time

from .bridge import display_payload


class WatchUsb:
    def __init__(self, port: str, baud: int = 115200):
        self.port_name = port
        self.baud = baud
        self.serial = None

    def __enter__(self):
        import serial
        port = serial.Serial(port=None, baudrate=self.baud, timeout=0.2)
        # ESP32-S3 USB Serial/JTAG exposes CDC data only while the host asserts DTR.
        port.dtr = True
        port.rts = False
        port.port = self.port_name
        try:
            port.open()
        except serial.SerialException as error:
            raise OSError(f"StopWatch port {self.port_name} cannot be opened; close serial monitors and check the port") from error
        self.serial = port
        return self

    def __exit__(self, *_):
        if self.serial:
            self.serial.close()

    def send(self, record: dict, source: str | None = None) -> str:
        token = os.environ.get("QZSS_WATCH_TOKEN", "")
        if not token or any(c.isspace() for c in token):
            raise ValueError("QZSS_WATCH_TOKEN が未設定か不正です")
        body = json.dumps(display_payload(record, source), ensure_ascii=False, separators=(",", ":"))
        frame = f"WATCH_REPORT {token} {body}\n".encode("utf-8")
        if len(frame) > 4300:
            raise ValueError("USB frame is too large")
        # Arduino-ESP32 HWCDC defaults to a 256-byte RX queue. Its ISR drops
        # bytes when full, so keep each burst below that queue's capacity.
        for offset in range(0, len(frame), 32):
            self.serial.write(frame[offset:offset + 32])
            self.serial.flush()
            time.sleep(0.04)
        self.serial.flush()
        deadline = time.monotonic() + 8
        diagnostic = ""
        while time.monotonic() < deadline:
            line = self.serial.readline().decode("utf-8", errors="replace").strip()
            if line.startswith(("WATCH_BOOT", "WATCH_STATUS", "WATCH_SERVER_READY")):
                diagnostic = line.split(" ", 1)[0]
            if line.startswith("WATCH_ACK "):
                result = line.removeprefix("WATCH_ACK ")
                if result not in ("stored", "duplicate"):
                    raise OSError(f"watch USB rejected report: {result}")
                return result
        suffix = f" (last device line: {diagnostic})" if diagnostic else " (no device response)"
        raise TimeoutError("StopWatch USB acknowledgement timed out" + suffix)
