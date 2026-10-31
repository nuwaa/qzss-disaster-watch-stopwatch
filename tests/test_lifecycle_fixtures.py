"""Check the synthetic on-device lifecycle frames independently of the UI."""
import importlib.util
from pathlib import Path
import re
import unittest

from scripts.generate_lifecycle_fixtures import crc24q, frames, get_bits, make_message


ROOT = Path(__file__).resolve().parents[1]


class LifecycleFixtureTests(unittest.TestCase):
    def test_generated_header_matches_generator(self):
        header = (ROOT / "firmware/stopwatch-standalone/include/lifecycle_fixtures.h").read_text(
            encoding="utf-8"
        )
        embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header))
        self.assertEqual(embedded, b"".join(frames()))

    def test_issue_update_cancellation_and_repeats_have_valid_crc(self):
        payloads = frames()
        self.assertEqual(len(payloads), 6)
        self.assertEqual(payloads[0], payloads[1])
        self.assertEqual(payloads[3], payloads[4])
        self.assertEqual(payloads[2], payloads[5])
        self.assertNotEqual(payloads[0], payloads[2])
        for index, payload in enumerate(payloads):
            self.assertEqual(len(payload), 40)
            self.assertEqual(payload[:8], bytes((5, 0, 1, 0, 8, 0, 2, 0)))
            message = b"".join(payload[i:i + 4][::-1] for i in range(8, 40, 4))
            self.assertEqual(get_bits(message, 8, 6), 43)
            self.assertEqual(get_bits(message, 14, 3), 7)  # training/test
            self.assertEqual(get_bits(message, 17, 4), 1)  # EEW
            self.assertEqual(get_bits(message, 41, 2), 2 if index in (3, 4) else 0)
            self.assertEqual(get_bits(message, 226, 24), crc24q(message))

    @unittest.skipUnless(importlib.util.find_spec("azarashi"), "Azarashi is not installed")
    def test_azarashi_decodes_issue_update_and_cancellation_as_training(self):
        import azarashi

        reports = []
        for updated, cancellation in ((False, False), (True, False), (True, True)):
            message = make_message(updated=updated, cancellation=cancellation)
            sentence = "$QZQSM,55," + message.hex().upper()[:63]
            checksum = 0
            for char in sentence[1:]:
                checksum ^= ord(char)
            record = azarashi.to_json_dict(
                azarashi.decode(f"{sentence}*{checksum:02X}", "nmea")
            )
            self.assertEqual(record["type"], "qzss.dcr.earthquake_early_warning")
            self.assertTrue(record["is_test"])
            english = record["texts"]["en"]
            self.assertEqual("Cancellation" in english, cancellation)
            reports.append(english)
        self.assertNotEqual(reports[0], reports[1])
        self.assertIn("Depth: 10 km", reports[0])
        self.assertIn("Depth: 26 km", reports[1])


if __name__ == "__main__":
    unittest.main()
