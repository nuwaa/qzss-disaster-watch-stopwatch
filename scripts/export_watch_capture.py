"""Export the StopWatch flash capture through its temporary USB CDC firmware."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time


def read_exact(port, count: int, timeout: float = 300) -> bytes:
    deadline = time.monotonic() + timeout
    chunks = []
    remaining = count
    next_progress = 256_000
    while remaining:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"USB capture stalled with {remaining} bytes remaining")
        chunk = port.read(min(remaining, 4096))
        if chunk:
            chunks.append(chunk)
            remaining -= len(chunk)
            received = count - remaining
            if received >= next_progress:
                print(f"export progress: {received}/{count} bytes", flush=True)
                next_progress += 256_000
    return b"".join(chunks)


def is_dcx_null(payload: bytes) -> bool:
    """Mirror QZSSDCX's null predicate on the first eight L1S words."""
    if len(payload) not in (40, 44):
        raise ValueError("SFRBX must have 40 or 44 bytes")
    message = b"".join(payload[i:i + 4][::-1] for i in range(8, 40, 4))

    def bits(start: int, length: int) -> int:
        return int.from_bytes(message, "big") >> (256 - start - length) & ((1 << length) - 1)

    return (bits(24, 2) == 0 and bits(35, 5) == 0 and bits(40, 7) == 0 and
            bits(47, 2) == 0 and bits(70, 10) == 0)


def export(port, directory: Path) -> list[Path]:
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
    output = []
    port.write(b"DUMP\n")
    port.flush()
    deadline = time.monotonic() + 600
    waiting_since = time.monotonic()
    last_line = ""
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        if line:
            last_line = line[:120]
        if time.monotonic() - waiting_since > 30:
            suffix = f" (last device line: {last_line})" if last_line else " (no device response)"
            raise TimeoutError("StopWatch did not start or continue the capture export" + suffix)
        if line == "LOG_DONE":
            return output
        if line.startswith("LOG_ERROR "):
            raise RuntimeError(line)
        if not line.startswith("LOG_BEGIN "):
            continue
        _, number, size_text = line.split()
        if number not in ("0", "1"):
            raise ValueError(f"unexpected capture file: {number}")
        size = int(size_text)
        if size < 0 or size > 1_600_000:
            raise ValueError(f"invalid capture size: {size}")
        print(f"exporting capture {number}: {size} bytes", flush=True)
        data = read_exact(port, size)
        ending = port.readline().decode("ascii", errors="replace").strip()
        if ending != f"LOG_END {number}":
            raise ValueError(f"capture boundary mismatch: {ending!r}")
        path = directory / f"watch-capture-{stamp}-{number}.jsonl"
        with path.open("xb") as saved:
            saved.write(data)
        output.append(path)
        waiting_since = time.monotonic()
    suffix = f" (last device line: {last_line})" if last_line else " (no device response)"
    raise TimeoutError("StopWatch did not finish the capture export" + suffix)


def summarize(paths: list[Path]) -> int:
    counts = {43: 0, 44: 0}
    tests = 0
    nulls = 0
    invalid = 0
    for path in paths:
        with path.open(encoding="utf-8") as stream:
            for line in stream:
                try:
                    record = json.loads(line)
                    mt = record["mt"]
                    payload = bytes.fromhex(record["sfrbx"])
                    if len(payload) not in (40, 44):
                        raise ValueError("SFRBX must have 40 or 44 bytes")
                    counts[mt] = counts.get(mt, 0) + 1
                    if mt == 44:
                        if is_dcx_null(payload):
                            nulls += 1
                        elif record["a1"] == 0:
                            tests += 1
                except (ValueError, KeyError):
                    invalid += 1
    print(f"mt43={counts[43]} mt44={counts[44]} mt44_null={nulls} "
          f"mt44_a1_test={tests} invalid_lines={invalid}")
    for path in paths:
        print(f"saved: {path} ({path.stat().st_size} bytes)")
    return invalid


def command_ack(port, command: str, expected: str) -> None:
    port.write(command.encode("ascii") + b"\n")
    port.flush()
    deadline = time.monotonic() + 30
    last_line = ""
    while time.monotonic() < deadline:
        line = port.readline().decode("ascii", errors="replace").strip()
        if line:
            last_line = line[:120]
        if line == expected:
            return
        if line.startswith("LOG_ERROR "):
            raise RuntimeError(line)
    suffix = f" (last device line: {last_line})" if last_line else " (no device response)"
    raise TimeoutError(f"StopWatch did not acknowledge {command}{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="StopWatch USB CDC COM port, e.g. COM10")
    parser.add_argument("--output-dir", type=Path, default=Path("logs"))
    parser.add_argument("--initialize", action="store_true", help="explicitly initialize the capture partition before the test")
    parser.add_argument("--clear-after-export", action="store_true", help="erase watch capture only after saving and checking the local export")
    args = parser.parse_args()
    import serial
    port = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=5)
    port.dtr = True
    port.rts = False
    port.port = args.port
    with port:
        time.sleep(5)  # USB CDC may reset when opened; wait for setup to finish.
        if args.initialize:
            command_ack(port, "INIT", "LOG_INIT_OK")
            print("capture partition ready")
            return 0
        paths = export(port, args.output_dir)
        invalid = summarize(paths)
        if args.clear_after_export:
            if not paths:
                raise RuntimeError("no capture files were exported; refusing to clear")
            if invalid:
                raise RuntimeError("capture contains invalid lines; refusing to clear")
            command_ack(port, "CLEAR", "LOG_CLEAR_OK")
            print("watch capture cleared after local export")
    return 0 if paths else 1


if __name__ == "__main__":
    raise SystemExit(main())
