import io
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from datetime import datetime, timezone

from qzss_watch.core import decode_record, frames


def frame(payload=b"abc"):
    body = bytes((2, 19)) + len(payload).to_bytes(2, "little") + payload
    a = b = 0
    for byte in body:
        a = (a + byte) & 255
        b = (b + a) & 255
    return b"\xb5\x62" + body + bytes((a, b))


class FrameTests(unittest.TestCase):
    def test_resynchronizes_and_rejects_checksum(self):
        good = frame()
        bad = good[:-1] + bytes((good[-1] ^ 1,))
        self.assertEqual(list(frames(io.BytesIO(b"junk" + bad + good), 1)), [good])

    def test_split_header(self):
        self.assertEqual(list(frames(io.BytesIO(frame()), 1)), [frame()])

    def test_ignores_unrelated_frame(self):
        self.assertIsNone(decode_record(frame(), datetime.now(timezone.utc), "live"))

    def test_replay_and_test_are_not_notification_eligible(self):
        report = object()
        fake = SimpleNamespace(decode=lambda *args, **kwargs: report, to_json_dict=lambda r: {"is_test": True, "type": "qzss.dcr.typhoon"})
        candidate = frame(bytes((5, 1, 1)) + b"padding")
        for source in ("live", "replay"):
            with self.subTest(source=source), patch.dict(sys.modules, {"azarashi": fake}):
                self.assertFalse(decode_record(candidate, datetime.now(timezone.utc), source)["watch"]["notification_eligible"])

    def test_null_dcx_is_not_notification_eligible(self):
        report = object()
        fake = SimpleNamespace(decode=lambda *args, **kwargs: report, to_json_dict=lambda r: {"is_test": False, "type": "qzss.dcx.null"})
        candidate = frame(bytes((5, 1, 1)) + b"padding")
        with patch.dict(sys.modules, {"azarashi": fake}):
            self.assertFalse(decode_record(candidate, datetime.now(timezone.utc), "live")["watch"]["notification_eligible"])

    def test_live_non_test_still_requires_phase_2_alert_gate(self):
        report = object()
        fake = SimpleNamespace(decode=lambda *args, **kwargs: report, to_json_dict=lambda r: {"is_test": False, "type": "qzss.dcr.typhoon"})
        candidate = frame(bytes((5, 1, 1)) + b"padding")
        with patch.dict(sys.modules, {"azarashi": fake}):
            self.assertFalse(decode_record(candidate, datetime.now(timezone.utc), "live")["watch"]["notification_eligible"])


if __name__ == "__main__":
    unittest.main()
