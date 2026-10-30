"""Read StopWatch USB serial output without printing Wi-Fi credentials."""
from __future__ import annotations

import argparse
import re
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?", default="COM10")
    parser.add_argument("--seconds", type=int, default=25)
    args = parser.parse_args()
    address = re.compile(rb"\b(?:\d{1,3}\.){3}\d{1,3}\b")
    end = time.monotonic() + args.seconds
    status_lines = 0
    with serial.Serial(args.port, 115200, timeout=1) as port:
        while time.monotonic() < end:
            line = port.readline().strip()
            if line.startswith((b"WATCH_BOOT ", b"WATCH_STATUS ")):
                print(line.decode("ascii", errors="replace"))
                status_lines += 1
            match = address.search(line)
            if match:
                print("WATCH_IP=" + match.group().decode("ascii"))
                return 0
    if status_lines:
        print("No IP address received; see WATCH_STATUS wifi code above.")
    else:
        print("No serial diagnostics received; check power and COM port.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
