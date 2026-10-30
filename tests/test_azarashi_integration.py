import importlib.util
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path
from datetime import datetime, timezone


@unittest.skipUnless(importlib.util.find_spec("azarashi"), "Azarashi is not installed")
class AzarashiIntegrationTests(unittest.TestCase):
    def test_real_qzss_frame(self):
        from qzss_watch.core import decode_record
        frame = (Path(__file__).parent / "fixtures" / "real_qzss_ash_fall.ubx").read_bytes()
        record = decode_record(frame, datetime(2026, 10, 30, tzinfo=timezone.utc), "replay")
        self.assertEqual(record["type"], "qzss.dcr.ash_fall")
        self.assertFalse(record["is_test"])
        self.assertEqual(record["watch"]["source"], "replay")
        self.assertFalse(record["watch"]["notification_eligible"])

    def test_public_training_sample(self):
        env = os.environ.copy()
        env["PYTHONUTF8"] = "1"
        result = subprocess.run(
            [sys.executable, "-m", "qzss_watch.cli", "sample"],
            capture_output=True, text=True, encoding="utf-8", env=env, check=True,
        )
        record = json.loads(result.stdout)
        self.assertTrue(record["is_test"])
        self.assertEqual(record["type"], "qzss.dcr.earthquake_early_warning")
        self.assertIn("訓練", record["texts"]["ja"])
        self.assertFalse(record["watch"]["notification_eligible"])


if __name__ == "__main__":
    unittest.main()
