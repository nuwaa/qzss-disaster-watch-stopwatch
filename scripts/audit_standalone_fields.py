"""Compare standalone firmware's L1S field positions with Azarashi on a raw log.

This checks transport extraction and DCR metadata. It cannot compare the
Arduino libraries' Japanese body text or prove behavior on the StopWatch.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path

import azarashi


def bits(data: bytes, start: int, length: int) -> int:
    value = 0
    for bit in range(start, start + length):
        value = (value << 1) | ((data[bit // 8] >> (7 - bit % 8)) & 1)
    return value


def l1s_message(frame: bytes) -> bytes | None:
    if len(frame) < 48 or frame[:4] != b"\xb5\x62\x02\x13":
        return None
    length = int.from_bytes(frame[4:6], "little")
    if len(frame) != length + 8 or length < 40 or frame[6] != 5 or frame[8] != 1:
        return None
    if frame[10] < 8 or length != 8 + frame[10] * 4:
        return None
    a = b = 0
    for byte in frame[2:-2]:
        a = (a + byte) & 255
        b = (b + a) & 255
    if frame[-2:] != bytes((a, b)):
        return None
    message = bytearray()
    for word in range(8):
        message.extend(frame[14 + word * 4:18 + word * 4][::-1])
    message[31] &= 0xC0
    return bytes(message)


def compare_dcr(frame: bytes, message: bytes, received_at: datetime) -> list[str]:
    report = azarashi.to_json_dict(azarashi.decode(frame, "ublox", timestamp=received_at))
    data = report["data"]
    source = data["report_time"]["source"]
    expected = {
        "version": (bits(message, 214, 6), data["version"]["value"]),
        "classification": (bits(message, 14, 3), int(data["report_classification"]["code"])),
        "information_type": (bits(message, 41, 2), int(data["information_type"]["code"])),
        "month": (bits(message, 21, 4), source["month"]),
        "day": (bits(message, 25, 5), source["day"]),
        "hour": (bits(message, 30, 5), source["hour"]),
        "minute": (bits(message, 35, 6), source["minute"]),
        "test_flag": (bits(message, 14, 3) == 7, report["is_test"]),
    }
    return [name for name, (firmware, reference) in expected.items() if firmware != reference]


def audit(path: Path) -> tuple[Counter[str], Counter[str]]:
    counts: Counter[str] = Counter()
    mismatches: Counter[str] = Counter()
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            item = json.loads(line)
            frame = bytes.fromhex(item["frame_hex"])
            counts["ubx_frames"] += 1
            message = l1s_message(frame)
            if message is None:
                continue
            counts["qzss_l1s"] += 1
            if bits(message, 8, 6) != 43:
                continue
            counts["dcr_frames"] += 1
            received_at = datetime.fromisoformat(item["received_at"].replace("Z", "+00:00"))
            try:
                differences = compare_dcr(frame, message, received_at)
            except azarashi.AzarashiDecodeError:
                counts["azarashi_rejected"] += 1
                continue
            counts["dcr_compared"] += 1
            mismatches.update(differences)
    return counts, mismatches


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="raw *-raw.jsonl log")
    args = parser.parse_args()
    counts, mismatches = audit(args.path)
    print("counts=" + json.dumps(counts, sort_keys=True))
    print("field_mismatches=" + json.dumps(mismatches, sort_keys=True))
    return 1 if mismatches or not counts["dcr_compared"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
