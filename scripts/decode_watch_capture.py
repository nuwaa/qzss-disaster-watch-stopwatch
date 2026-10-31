"""Decode exported standalone SFRBX capture with Azarashi, preserving every input row."""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path

from qzss_watch.core import decode_record


def ubx_sfrbx(payload: bytes) -> bytes:
    if len(payload) not in (40, 44):
        raise ValueError("SFRBX payload must be 40 or 44 bytes")
    frame = b"\xb5\x62\x02\x13" + len(payload).to_bytes(2, "little") + payload
    a = b = 0
    for value in frame[2:]:
        a = (a + value) & 255
        b = (b + a) & 255
    return frame + bytes((a, b))


def decode_file(source: Path, destination: Path) -> Counter:
    counts: Counter = Counter()
    with source.open(encoding="utf-8") as incoming, destination.open("w", encoding="utf-8") as outgoing:
        for number, line in enumerate(incoming, 1):
            try:
                saved = json.loads(line)
                payload = bytes.fromhex(saved["sfrbx"])
                received = datetime.fromtimestamp(saved["utc"], timezone.utc) if saved["utc"] else \
                    datetime(2026, 11, 3, tzinfo=timezone.utc)
                record = decode_record(ubx_sfrbx(payload), received, "capture")
                if record is not None and saved.get("source") == "synthetic":
                    record["watch"]["source"] = "synthetic"
                a1_match = None if saved["mt"] != 44 or record is None or \
                    record.get("type") == "qzss.dcx.null" else \
                    (record.get("is_test") is (saved["a1"] == 0))
                result = {"capture": saved, "decoded": record, "a1_match": a1_match,
                          "timestamp_placeholder": not bool(saved["utc"])}
                outgoing.write(json.dumps(result, ensure_ascii=False) + "\n")
                counts[f"mt{saved['mt']}"] += 1
                if saved["mt"] == 44 and record is not None and \
                        record.get("type") != "qzss.dcx.null" and saved["a1"] == 0:
                    counts["mt44_test"] += 1
                if record is None:
                    counts["undecoded"] += 1
                if a1_match is False:
                    counts["a1_mismatch"] += 1
            except (ValueError, KeyError, OverflowError) as error:
                counts["invalid"] += 1
                outgoing.write(json.dumps({"line": number, "error": str(error)}, ensure_ascii=False) + "\n")
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    for source in args.files:
        destination = source.with_name(source.stem + "-decoded.jsonl")
        counts = decode_file(source, destination)
        print(f"{source} -> {destination} {dict(counts)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
