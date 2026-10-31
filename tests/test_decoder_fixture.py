"""Keep on-device comparison frames identical to the saved received fixtures."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DecoderFixtureTests(unittest.TestCase):
    def test_embedded_frames_match_saved_ubx(self):
        header = (ROOT / "firmware/stopwatch-standalone/include/comparison_fixtures.h").read_text(
            encoding="utf-8"
        )
        embedded = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header))
        saved = b"".join(
            (ROOT / "tests/fixtures" / f"real_qzss_{name}.ubx").read_bytes()
            for name in ("ash_fall", "typhoon", "marine", "weather")
        )
        self.assertEqual(embedded, saved)


if __name__ == "__main__":
    unittest.main()
