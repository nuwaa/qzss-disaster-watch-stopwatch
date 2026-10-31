"""Generate CRC-valid MT44 frames for the isolated on-device DCX test mode.

The public Azarashi L-Alert example is used only as a format seed. These
synthetic frames are never supplied to the normal GNSS receiver path.
"""
from pathlib import Path

from scripts.generate_lifecycle_fixtures import crc24q, get_bits, set_bits, sfrbx_payload

BASE = bytes.fromhex(
    "53B0604DE19524CDA305B2C1E355B57800000CCC000000000000001022A81880"
)
OUTPUT = Path(__file__).resolve().parents[1] / "firmware/stopwatch-standalone/include/dcx_fixtures.h"


def make_message(action: int, *, severity: int = 2, satellite: int = 0) -> bytes:
    message = bytearray(BASE)
    assert get_bits(message, 8, 6) == 44
    set_bits(message, 14, 10, satellite)
    set_bits(message, 24, 2, action)
    set_bits(message, 47, 2, severity)
    set_bits(message, 226, 24, crc24q(message))
    message[31] &= 0xC0
    return bytes(message)


def frames() -> tuple[bytes, ...]:
    issue = sfrbx_payload(make_message(1))
    repeat_other_satellite = sfrbx_payload(make_message(1, satellite=1))
    update = sfrbx_payload(make_message(2, severity=3))
    all_clear = sfrbx_payload(make_message(3, severity=3))
    test = sfrbx_payload(make_message(0))
    return issue, repeat_other_satellite, update, all_clear, all_clear, test


def main() -> None:
    lines = ["#pragma once", "#include <cstdint>",
             "// Synthetic MT44 only. QZSS_DCX_TEST does not start USB or alert output.",
             "constexpr uint8_t dcxFrames[][40] = {"]
    for payload in frames():
        lines.append("  {" + ", ".join(f"0x{byte:02x}" for byte in payload) + "},")
    lines.append("};")
    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
