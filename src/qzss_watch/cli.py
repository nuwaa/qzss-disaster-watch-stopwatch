"""Live receiver and deterministic log replay."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
from datetime import datetime
import json
import sys
import time

from .bridge import send_record
from .core import ENABLE_SFRBX_UART1_RAM, decode_record, frames, utc_now, write_jsonl
from .usb import WatchUsb


def process(frame, at, source, output, device_url=None, watch_usb=None):
    record = decode_record(frame, at, source)
    if record is not None:
        line = json.dumps(record, ensure_ascii=False)
        print(line, flush=True)
        if output:
            write_jsonl(output, record)
        if device_url and record["type"] != "qzss.dcx.null":
            try:
                send_record(device_url, record, source)
            except (OSError, ValueError) as error:
                print(f"watch send failed: {error}", file=sys.stderr)
        if watch_usb and record["type"] != "qzss.dcx.null":
            try:
                watch_usb.send(record, source)
            except (OSError, ValueError) as error:
                print(f"watch USB send failed: {error}", file=sys.stderr)
    return record


class LiveSerial:
    """Keep serial framing alive across read timeouts and show receive progress."""

    def __init__(self, port, seconds):
        self.port = port
        self.start = time.monotonic()
        self.deadline = self.start + seconds if seconds else None
        self.last_status = self.start
        self.bytes = 0
        self.line_ends = 0

    def read(self, size):
        while self.deadline is None or time.monotonic() < self.deadline:
            chunk = self.port.read(size)
            self.bytes += len(chunk)
            self.line_ends += chunk.count(b"\n")
            now = time.monotonic()
            if now - self.last_status >= 30:
                print(f"receive progress: seconds={int(now - self.start)} bytes={self.bytes} "
                      f"line_ends={self.line_ends}", file=sys.stderr, flush=True)
                self.last_status = now
            if chunk:
                return chunk
        return b""


def main(argv=None):
    if sys.platform == "win32" and not sys.flags.utf8_mode:
        raise SystemExit("Windows では文字化け防止のため PYTHONUTF8=1 を設定して実行してください")
    parser = argparse.ArgumentParser(description="QZSS DCR/DCX experimental receiver")
    sub = parser.add_subparsers(dest="command", required=True)
    live = sub.add_parser("receive")
    live.add_argument("port")
    live.add_argument("--baud", type=int, default=115200)
    live.add_argument("--enable-sfrbx", action="store_true", help="enable UBX-RXM-SFRBX on UART1 in receiver RAM")
    live.add_argument("--raw-log", help="JSONL with frame hex and reception time")
    live.add_argument("--output", help="decoded JSONL")
    live.add_argument("--device-url", help="StopWatch POST endpoint, e.g. http://192.168.1.20:8080/report")
    live.add_argument("--watch-usb", help="StopWatch USB CDC port, e.g. COM10")
    live.add_argument("--seconds", type=int, default=0, help="stop after this many seconds (0: until Ctrl+C)")
    replay = sub.add_parser("replay")
    replay.add_argument("input", help="raw JSONL created by receive")
    replay.add_argument("--output", help="decoded JSONL")
    replay.add_argument("--device-url", help="StopWatch POST endpoint")
    replay.add_argument("--watch-usb", help="StopWatch USB CDC port")
    sample = sub.add_parser("sample")
    sample.add_argument("--output", help="decoded JSONL")
    sample.add_argument("--device-url", help="StopWatch POST endpoint")
    sample.add_argument("--watch-usb", help="StopWatch USB CDC port")
    args = parser.parse_args(argv)
    if args.command == "receive" and args.watch_usb and args.watch_usb.casefold() == args.port.casefold():
        parser.error("GNSS と StopWatch に同じシリアルポートは指定できません")
    with ExitStack() as stack:
        watch_usb = stack.enter_context(WatchUsb(args.watch_usb)) if args.watch_usb else None
        return run(args, watch_usb, parser)


def run(args, watch_usb, parser):
    if args.command == "sample":
        import azarashi
        sentence = "$QZQSM,55,C6AF89A820000324000050400548C5E2C000000003DFF8001C00001185443FC*05"
        record = azarashi.to_json_dict(azarashi.decode(sentence, "nmea"))
        record["watch"] = {"source": "sample", "notification_eligible": False}
        print(json.dumps(record, ensure_ascii=False))
        if args.output:
            write_jsonl(args.output, record)
        if args.device_url:
            send_record(args.device_url, record, "sample")
        if watch_usb:
            print(f"watch USB: {watch_usb.send(record, 'sample')}", file=sys.stderr)
        return 0
    if args.command == "replay":
        with open(args.input, encoding="utf-8") as log:
            for number, line in enumerate(log, 1):
                try:
                    item = json.loads(line)
                    frame = bytes.fromhex(item["frame_hex"])
                    at = datetime.fromisoformat(item["received_at"].replace("Z", "+00:00"))
                    process(frame, at, "replay", args.output, args.device_url, watch_usb)
                except (ValueError, KeyError) as error:
                    print(f"line {number}: {error}", file=sys.stderr)
        return 0
    import serial
    try:
        with serial.Serial(args.port, args.baud, timeout=1) as port:
            if args.enable_sfrbx:
                port.write(ENABLE_SFRBX_UART1_RAM)
                port.flush()
            stream = LiveSerial(port, args.seconds)
            frame_count = report_count = 0
            try:
                for frame in frames(stream, chunk_size=1):
                    frame_count += 1
                    at = utc_now()
                    if args.raw_log:
                        write_jsonl(args.raw_log, {"received_at": at.isoformat(), "frame_hex": frame.hex()})
                    if process(frame, at, "live", args.output, args.device_url, watch_usb) is not None:
                        report_count += 1
            finally:
                print(f"receive summary: bytes={stream.bytes} ubx_frames={frame_count} "
                      f"reports={report_count}", file=sys.stderr, flush=True)
    except KeyboardInterrupt:
        return 0
    except serial.SerialException as error:
        parser.exit(1, f"serial error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
