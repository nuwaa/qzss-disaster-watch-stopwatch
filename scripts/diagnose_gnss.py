"""Count GR-M10 serial traffic without printing positions or message content."""
from __future__ import annotations

import argparse
from collections import Counter
import io
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from qzss_watch.core import ENABLE_SFRBX_UART1_RAM, frames


def capture(port: serial.Serial, seconds: int) -> None:
    end = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < end:
        data.extend(port.read(1024))
    kinds = Counter(f"{frame[2]:02X}/{frame[3]:02X}" for frame in frames(io.BytesIO(data)))
    lines = [line.strip() for line in data.split(b"\n") if line.startswith(b"$")]
    nmea = sum(b"*" in line for line in lines)
    qzss_gsv = [line.decode("ascii", errors="replace") for line in lines if line.startswith(b"$GQGSV")]
    print(f"bytes={len(data)} nonzero={sum(value != 0 for value in data)} "
          f"nmea_lines={nmea} ubx_frames={sum(kinds.values())} "
          f"ubx_types={dict(kinds)}", flush=True)
    print("qzss_gsv_last=" + (qzss_gsv[-1] if qzss_gsv else "none"), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?", default="COM11")
    parser.add_argument("--seconds", type=int, default=15)
    args = parser.parse_args()
    try:
        with serial.Serial(args.port, 9600, timeout=1) as port:
            print("Passive traffic:", flush=True)
            capture(port, args.seconds)
            port.write(ENABLE_SFRBX_UART1_RAM)
            port.flush()
            print("After enabling SFRBX in receiver RAM:", flush=True)
            capture(port, args.seconds)
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
