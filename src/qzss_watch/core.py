"""UBX transport framing and Azarashi JSON adapter."""
from __future__ import annotations

from datetime import datetime, timezone
import json


# Prioris GR-M10 example: CFG-VALSET RAM, CFG-MSGOUT-UBX_RXM_SFRBX_UART1=1.
ENABLE_SFRBX_UART1_RAM = bytes.fromhex("b562068a09000101000032029120018130")


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def frames(stream, chunk_size: int = 1024):
    """Yield checksum-valid UBX frames from a binary stream, ignoring other bytes."""
    buffer = bytearray()
    while True:
        chunk = stream.read(chunk_size)
        if not chunk:
            return
        buffer.extend(chunk)
        while True:
            start = buffer.find(b"\xb5\x62")
            if start < 0:
                buffer[:] = buffer[-1:] if buffer.endswith(b"\xb5") else b""
                break
            del buffer[:start]
            if len(buffer) < 6:
                break
            length = int.from_bytes(buffer[4:6], "little")
            if length > 4096:
                del buffer[0]
                continue
            size = length + 8
            if len(buffer) < size:
                break
            candidate = bytes(buffer[:size])
            a = b = 0
            for value in candidate[2:-2]:
                a = (a + value) & 255
                b = (b + a) & 255
            if candidate[-2:] == bytes((a, b)):
                del buffer[:size]
                yield candidate
            else:
                del buffer[0]


def decode_record(frame: bytes, received_at: datetime, source: str):
    """Return an Azarashi v2 record or None for unrelated/invalid frames."""
    if len(frame) < 14 or frame[2:4] != b"\x02\x13" or frame[6] != 5 or frame[8] != 1:
        return None
    import azarashi
    try:
        report = azarashi.decode(frame, "ublox", timestamp=received_at)
    except azarashi.AzarashiDecodeError:
        return None
    record = azarashi.to_json_dict(report)
    record["watch"] = {
        "source": source,
        # Phase 1 has no persistence or age gate. Never authorize an alert here.
        "notification_eligible": False,
    }
    return record


def write_jsonl(path, record):
    with open(path, "a", encoding="utf-8") as output:
        output.write(json.dumps(record, ensure_ascii=False) + "\n")
