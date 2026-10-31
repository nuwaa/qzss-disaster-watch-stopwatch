"""Keep the on-device comparison frame identical to the received test fixture."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DecoderFixtureTests(unittest.TestCase):
    def test_embedded_frame_matches_saved_ubx(self):
        header = (ROOT / "firmware/stopwatch-standalone/include/ash_fixture.h").read_text(
            encoding="utf-8"
        )
        embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header))
        saved = (ROOT / "tests/fixtures/real_qzss_ash_fall.ubx").read_bytes()
        self.assertEqual(embedded, saved)


if __name__ == "__main__":
    unittest.main()
