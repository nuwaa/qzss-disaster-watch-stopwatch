"""Capture safe StopWatch boot diagnostics without printing Wi-Fi secrets."""
from __future__ import annotations

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?", default="COM10")
    parser.add_argument("--seconds", type=int, default=20)
    parser.add_argument("--verbose", action="store_true", help="Print non-secret boot lines")
    args = parser.parse_args()

    prefixes = ("ESP-ROM:", "Build:", "rst:", "boot:", "Saved PC:", "entry ",
                "WATCH_BOOT ", "WATCH_STATUS ", "Guru Meditation Error:",
                "Rebooting...", "invalid header:")
    seen = 0
    total = 0
    deadline = time.monotonic() + args.seconds
    try:
        with serial.Serial(args.port, 115200, timeout=0.5) as port:
            print("Listening. Press the StopWatch power button briefly once now.", flush=True)
            while time.monotonic() < deadline:
                raw = port.readline()
                total += len(raw)
                line = raw.decode("ascii", errors="replace").strip()
                if any(
                    word in line.lower() for word in ("ssid", "password", "token", "credential")
                ):
                    continue
                if args.verbose and line:
                    print(line[:200], flush=True)
                    seen += 1
                elif line.startswith(prefixes):
                    print(line[:200], flush=True)
                    seen += 1
    except serial.SerialException as exc:
        print(f"Cannot open {args.port}: {exc.__class__.__name__}")
        return 2
    print(f"RESULT bytes={total} diagnostic_lines={seen}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
