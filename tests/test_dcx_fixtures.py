"""Independent checks for isolated MT44 issue/update/all-clear fixtures."""
from datetime import datetime, timezone
import importlib.util
from pathlib import Path
import re
import unittest

from scripts.generate_dcx_fixtures import frames, make_message
from scripts.generate_lifecycle_fixtures import crc24q, get_bits

ROOT = Path(__file__).resolve().parents[1]


class DcxFixtureTests(unittest.TestCase):
    def test_header_matches_generator_and_crc(self):
        header = (ROOT / "firmware/stopwatch-standalone/include/dcx_fixtures.h").read_text(
            encoding="utf-8"
        )
        embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header))
        self.assertEqual(embedded, b"".join(frames()))
        self.assertEqual(len(frames()), 6)
        for payload in frames():
            self.assertEqual(len(payload), 40)
            message = b"".join(payload[i:i + 4][::-1] for i in range(8, 40, 4))
            self.assertEqual(get_bits(message, 8, 6), 44)
            self.assertEqual(get_bits(message, 226, 24), crc24q(message))

    def test_actions_and_satellite_independent_identity(self):
        messages = [b"".join(f[i:i + 4][::-1] for i in range(8, 40, 4)) for f in frames()]
        self.assertEqual([get_bits(m, 24, 2) for m in messages], [1, 1, 2, 3, 3, 0])
        self.assertNotEqual(messages[0], messages[1])
        self.assertEqual(messages[0][3:28] + bytes([messages[0][28] & 0xC0]),
                         messages[1][3:28] + bytes([messages[1][28] & 0xC0]))
        self.assertEqual(messages[3], messages[4])

    @unittest.skipUnless(importlib.util.find_spec("azarashi"), "Azarashi is not installed")
    def test_azarashi_decodes_actions(self):
        import azarashi

        for action, expected in ((0, "Test"), (1, "Alert"), (2, "Update"), (3, "All Clear")):
            message = make_message(action)
            sentence = "$QZQSM,55," + message.hex().upper()[:63]
            checksum = 0
            for char in sentence[1:]:
                checksum ^= ord(char)
            report = azarashi.decode(f"{sentence}*{checksum:02X}", "nmea",
                                      timestamp=datetime(2024, 6, 23, 13, tzinfo=timezone.utc))
            self.assertEqual(report.a1_message_type, expected)


if __name__ == "__main__":
    unittest.main()
