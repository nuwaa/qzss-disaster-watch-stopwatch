"""Small, display-only HTTP payload for the StopWatch prototype."""
from __future__ import annotations

import json
import os
from urllib.request import Request, urlopen


def display_payload(record: dict, source: str | None = None) -> dict:
    watch = record.get("watch", {})
    origin = source or watch.get("source", "unknown")
    texts = record.get("texts", {})
    japanese = texts.get("ja") or texts.get("en") or "内容を表示できません"
    kind = record.get("type", "unknown")
    if origin == "sample":
        badge = "デモ・訓練" if record.get("is_test") else "デモ"
    elif origin == "replay":
        badge = "再生・訓練" if record.get("is_test") else "再生"
    elif record.get("is_test"):
        badge = "訓練・試験"
    elif kind == "qzss.dcx.null":
        badge = "空メッセージ"
    elif origin == "live":
        badge = "衛星受信"
    else:
        badge = "出所不明"
    return {
        "schema": 1,
        "id": str(record.get("message_id", ""))[:100],
        "type": kind[:80],
        "badge": badge,
        "text": japanese[:90],
        "received_at": record.get("reception", {}).get("at", ""),
        "source": origin,
        "is_test": bool(record.get("is_test", False)),
        "notification_eligible": False,
    }


def send_record(url: str, record: dict, source: str | None = None) -> None:
    token = os.environ.get("QZSS_WATCH_TOKEN", "")
    if not token:
        raise ValueError("QZSS_WATCH_TOKEN が未設定です")
    body = json.dumps(display_payload(record, source), ensure_ascii=False).encode("utf-8")
    request = Request(url, body, {"Content-Type": "application/json; charset=utf-8", "X-Watch-Token": token}, method="POST")
    with urlopen(request, timeout=3) as response:
        if response.status != 200:
            raise OSError(f"watch HTTP {response.status}")
