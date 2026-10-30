"""Headless USB host bridge: GR-M10-C/S -> Azarashi -> StopWatch CDC."""
from __future__ import annotations

import argparse
from collections import Counter
from contextlib import ExitStack
from datetime import datetime, timezone
import logging
import os
from pathlib import Path
import time

from serial import Serial, SerialException
from serial.tools import list_ports

from .core import ENABLE_SFRBX_UART1_RAM, decode_record, frames, utc_now, write_jsonl
from .usb import WatchUsb

LOG = logging.getLogger("qzss-watch-bridge")
GNSS_ID = (0x1A86, 0x7523)  # Observed GR-M10-C/S CH340 USB-UART.
WATCH_ID = (0x303A, 0x1001)  # Observed StopWatch ESP32-S3 USB Serial/JTAG.


def detect_ports(ports=None):
    """Return unique (GNSS, StopWatch) device paths, or None while unplugged.

    Fail closed on identical USB IDs: guessing a port could label unrelated
    serial data as satellite reception or send private data to another device.
    """
    ports = list(list_ports.comports() if ports is None else ports)
    found = {}
    for name, usb_id in (("GNSS", GNSS_ID), ("StopWatch", WATCH_ID)):
        matches = [port.device for port in ports if (port.vid, port.pid) == usb_id]
        if len(matches) > 1:
            raise ValueError(f"multiple {name} USB ports found: {matches}")
        if not matches:
            return None
        found[name] = matches[0]
    return found["GNSS"], found["StopWatch"]


class ContinuousRead:
    """Keep the UBX framer alive across short serial read timeouts."""

    def __init__(self, port, silent_limit=15):
        self.port = port
        self.silent_limit = silent_limit
        self.last_data = time.monotonic()

    def read(self, size):
        while True:
            chunk = self.port.read(size)
            if chunk:
                self.last_data = time.monotonic()
                return chunk
            if time.monotonic() - self.last_data > self.silent_limit:
                raise TimeoutError("GNSS USB serial has been silent for 15 seconds")


def run_pair(gnss_path, watch_path, log_dir=None):
    """Run until a USB disconnect or transport failure requires re-detection."""
    with ExitStack() as stack:
        gnss = stack.enter_context(Serial(gnss_path, 9600, timeout=1))
        watch = stack.enter_context(WatchUsb(watch_path))
        gnss.write(ENABLE_SFRBX_UART1_RAM)
        gnss.flush()
        LOG.info("bridge active: GNSS=%s StopWatch=%s", gnss_path, watch_path)
        counts = Counter()
        raw_path = report_path = None
        if log_dir:
            directory = Path(log_dir)
            directory.mkdir(parents=True, exist_ok=True)
            stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
            raw_path = directory / f"live-{stamp}-raw.jsonl"
            report_path = directory / f"live-{stamp}-reports.jsonl"
        try:
            for frame in frames(ContinuousRead(gnss), chunk_size=1):
                counts["frames"] += 1
                at = utc_now()
                if raw_path:
                    write_jsonl(raw_path, {"received_at": at.isoformat(), "frame_hex": frame.hex()})
                record = decode_record(frame, at, "live")
                if record is None:
                    continue
                counts["decoded"] += 1
                if report_path:
                    write_jsonl(report_path, record)
                if record["type"] == "qzss.dcx.null":
                    counts["empty"] += 1
                    continue
                result = watch.send(record, "live")
                counts[result] += 1
                if result == "stored":
                    LOG.info("stored %s", record["type"])
        finally:
            LOG.info("session summary: %s", dict(counts))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Standalone QZSS USB host bridge")
    parser.add_argument("--log-dir", help="optional directory for raw and decoded JSONL logs")
    parser.add_argument("--retry-seconds", type=float, default=5)
    args = parser.parse_args(argv)
    if args.retry_seconds <= 0:
        parser.error("--retry-seconds must be positive")
    if not os.environ.get("QZSS_WATCH_TOKEN", "").strip():
        parser.error("QZSS_WATCH_TOKEN is required")
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    while True:
        try:
            selected = detect_ports()
            if selected is None:
                LOG.info("waiting for both USB devices")
            else:
                run_pair(*selected, log_dir=args.log_dir)
        except (SerialException, OSError, ValueError, TimeoutError) as error:
            LOG.warning("bridge reconnect: %s", error)
        time.sleep(args.retry_seconds)


if __name__ == "__main__":
    raise SystemExit(main())
