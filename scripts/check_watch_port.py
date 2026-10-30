"""Reject a GNSS CH340 port before attempting a StopWatch firmware upload."""
from __future__ import annotations

import sys
from serial.tools import list_ports


def main() -> int:
    requested = sys.argv[1].casefold()
    port = next((item for item in list_ports.comports() if item.device.casefold() == requested), None)
    if port is None:
        print(f"{sys.argv[1]} is not connected. Connect StopWatch and list ports again.", file=sys.stderr)
        return 1
    if (port.vid, port.pid) == (0x1A86, 0x7523):
        print(f"{port.device} is CH340 (the GNSS receiver), not StopWatch. Upload cancelled.", file=sys.stderr)
        return 1
    print(f"Upload target: {port.device} ({port.description}; VID:PID={port.vid}:{port.pid})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
