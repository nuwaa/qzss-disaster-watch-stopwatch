"""Summarize the latest local live receive log without printing alert text."""
from __future__ import annotations

from collections import Counter
import json
from pathlib import Path


def main() -> int:
    raw_files = sorted(Path("logs").glob("live-*-raw.jsonl"))
    files = sorted(Path("logs").glob("live-*-reports.jsonl"))
    if raw_files:
        raw = raw_files[-1]
        frame_types: Counter[str] = Counter()
        with raw.open(encoding="utf-8") as stream:
            for line in stream:
                frame = bytes.fromhex(json.loads(line)["frame_hex"])
                if len(frame) >= 4:
                    frame_types[f"{frame[2]:02X}/{frame[3]:02X}"] += 1
        print(f"raw_log={raw} frames={sum(frame_types.values())}")
        print("ubx_types=" + json.dumps(frame_types, sort_keys=True))
    if not files:
        print("No live report log found.")
        return 1
    path = files[-1]
    counts: Counter[str] = Counter()
    tests: Counter[str] = Counter()
    unique: set[tuple[str, str]] = set()
    first = last = None
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            record = json.loads(line)
            kind = record.get("type", "unknown")
            counts[kind] += 1
            unique.add((kind, record.get("message_id", "")))
            tests["test" if record.get("is_test") else "not_test"] += 1
            at = record.get("reception", {}).get("at")
            first = first or at
            last = at
    print(f"log={path} reports={sum(counts.values())} first={first} last={last}")
    print("types=" + json.dumps(counts, ensure_ascii=False, sort_keys=True))
    print(f"unique_messages={len(unique)} unique_non_null={sum(kind != 'qzss.dcx.null' for kind, _ in unique)}")
    print("test_flags=" + json.dumps(tests, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
