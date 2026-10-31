"""Generate isolated, CRC-correct DCR test payloads from the public QZSS sample.

The generated frames are only for QZSS_LIFECYCLE_TEST, which never starts USB
reception or automatic notification. They are not live disaster reports.
"""
from pathlib import Path

SAMPLE = bytes.fromhex(
    "C6AF89A820000324000050400548C5E2C000000003DFF8001C00001185443FC0"
)
OUTPUT = Path(__file__).resolve().parents[1] / "firmware/stopwatch-standalone/include/lifecycle_fixtures.h"


def set_bits(data: bytearray, start: int, length: int, value: int) -> None:
    for offset in range(length):
        bit = start + offset
        mask = 1 << (7 - bit % 8)
        data[bit // 8] = (data[bit // 8] & ~mask) | (
            ((value >> (length - offset - 1)) & 1) * mask
        )


def get_bits(data: bytes, start: int, length: int) -> int:
    result = 0
    for bit in range(start, start + length):
        result = (result << 1) | ((data[bit // 8] >> (7 - bit % 8)) & 1)
    return result


def crc24q(data: bytes) -> int:
    crc = 0
    for bit in range(226):
        crc ^= ((data[bit // 8] >> (7 - bit % 8)) & 1) << 23
        crc <<= 1
        if crc & 0x1000000:
            crc ^= 0x1864CFB
        crc &= 0xFFFFFF
    return crc


def make_message(*, updated: bool, cancellation: bool) -> bytes:
    data = bytearray(SAMPLE)
    # Public sample already has Rc=7 (training), MT=43, Dc=1 (EEW).
    set_bits(data, 41, 2, 2 if cancellation else 0)  # IS-QZSS-DCR It
    if updated:
        set_bits(data, 100, 1, 1 - get_bits(data, 100, 1))
    set_bits(data, 226, 24, crc24q(data))
    data[31] &= 0xC0
    return bytes(data)


def sfrbx_payload(message: bytes) -> bytes:
    # gnssId=5, numWords=8, version=1. The firmware reverses each UBX word.
    header = bytes((5, 0, 1, 0, 8, 0, 2, 0))
    words = b"".join(message[i:i + 4][::-1] for i in range(0, 32, 4))
    return header + words


def frames() -> tuple[bytes, ...]:
    issue = sfrbx_payload(make_message(updated=False, cancellation=False))
    update = sfrbx_payload(make_message(updated=True, cancellation=False))
    cancel = sfrbx_payload(make_message(updated=True, cancellation=True))
    return issue, issue, update, cancel, cancel, update


def main() -> None:
    lines = ["#pragma once", "#include <cstdint>",
             "// Synthetic training data. Only QZSS_LIFECYCLE_TEST uses these payloads.",
             "constexpr uint8_t lifecycleFrames[][40] = {"]
    for payload in frames():
        lines.append("  {" + ", ".join(f"0x{byte:02x}" for byte in payload) + "},")
    lines.append("};")
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
