"""Audit decoded Azarashi logs for possible standalone DCR notifications.

This is an independent, conservative upper bound. Raw logs do not contain the
StopWatch's RMC clock lock, warmup state, or NVS ledger, so a candidate here is
not proof that the firmware would notify.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path


def parse_time(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)


def reason(record: dict) -> str:
    if not str(record.get("type", "")).startswith("qzss.dcr."):
        return "not_dcr"
    data = record.get("data") or {}
    version = data.get("version") or {}
    if version.get("status") != "valid" or version.get("value") != 1:
        return "invalid_version"
    classification = data.get("report_classification") or {}
    if record.get("is_test") or classification.get("code") not in {"1", "2", "3"}:
        return "test_or_unknown_classification"
    information = data.get("information_type") or {}
    if information.get("code") != "0":
        return "not_issue"
    report_time = data.get("report_time") or {}
    if report_time.get("status") != "valid" or not report_time.get("value"):
        return "invalid_report_time"
    try:
        age = (parse_time(record["reception"]["at"]) - parse_time(report_time["value"])).total_seconds()
    except (KeyError, TypeError, ValueError):
        return "invalid_report_time"
    if age < 0:
        return "future_report_time"
    if age > 180:
        return "older_than_3_minutes"
    return "candidate"


def audit(path: Path) -> tuple[int, Counter[str], int]:
    counts: Counter[str] = Counter()
    unique_candidates: set[str] = set()
    total = 0
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            if not line.strip():
                continue
            record = json.loads(line)
            total += 1
            verdict = reason(record)
            counts[verdict] += 1
            if verdict == "candidate":
                unique_candidates.add(record.get("message_id", ""))
    return total, counts, len(unique_candidates)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, help="decoded *-reports.jsonl log")
    args = parser.parse_args()
    files = sorted(Path("logs").glob("live-*-reports.jsonl"))
    path = args.path or (files[-1] if files else None)
    if path is None:
        parser.error("no decoded live report log found; pass a path")
    total, counts, unique = audit(path)
    print(f"log={path} decoded_reports={total} possible_notifications={counts['candidate']} unique_candidates={unique}")
    print("reasons=" + json.dumps(counts, sort_keys=True))
    print("Clock lock, 60-second warmup, NVS deduplication, and MCU decoding are not in this audit.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
