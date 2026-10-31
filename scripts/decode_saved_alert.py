"""Decode the 32-byte L1S value shown on StopWatch's saved-alert screen.

The original UBX transport header and ninth receiver word are not stored.
They are reconstructed for Azarashi; this verifies L1S content, not the
original receiver bytes or the watch's alert timing and NVS behavior.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json

import azarashi


def reconstructed_sfrbx(l1s: bytes) -> bytes:
    if len(l1s) != 32:
        raise ValueError("L1S must contain exactly 32 bytes (64 hexadecimal digits)")
    # GNSS ID 5 = QZSS, nine words. The final receiver word is not used by
    # the standalone L1S decoder and is filled with zero for this comparison.
    payload = bytes.fromhex("0502010009400200")
    payload += b"".join(l1s[i:i + 4][::-1] for i in range(0, 32, 4))
    payload += bytes(4)
    frame = bytes.fromhex("b5620213") + len(payload).to_bytes(2, "little") + payload
    check_a = check_b = 0
    for value in frame[2:]:
        check_a = (check_a + value) & 0xFF
        check_b = (check_b + check_a) & 0xFF
    return frame + bytes((check_a, check_b))


def decode(l1s_hex: str, received_utc: int) -> dict:
    l1s = bytes.fromhex("".join(l1s_hex.split()))
    frame = reconstructed_sfrbx(l1s)
    timestamp = datetime.fromtimestamp(received_utc, tz=timezone.utc)
    return azarashi.to_json_dict(azarashi.decode(frame, "ublox", timestamp=timestamp))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("l1s_hex", help="four 16-digit rows from the watch, joined or quoted")
    parser.add_argument("received_utc", type=int, help="UTC seconds shown on the watch")
    args = parser.parse_args()
    try:
        report = decode(args.l1s_hex, args.received_utc)
    except (ValueError, OverflowError, azarashi.AzarashiDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
