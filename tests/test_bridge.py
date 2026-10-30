import unittest

from qzss_watch.bridge import display_payload


class BridgeTests(unittest.TestCase):
    def test_sample_is_marked_demo_and_never_alerts(self):
        record = {"type": "qzss.dcr.typhoon", "is_test": True, "texts": {"ja": "訓練"}, "message_id": "id", "reception": {"at": "2026-10-30T00:00:00Z"}}
        payload = display_payload(record, "sample")
        self.assertEqual(payload["badge"], "デモ・訓練")
        self.assertFalse(payload["notification_eligible"])

    def test_live_null_message_never_alerts(self):
        record = {"type": "qzss.dcx.null", "is_test": False, "texts": {"en": "Null"}}
        self.assertFalse(display_payload(record, "live")["notification_eligible"])

    def test_live_test_badge_and_japanese_length(self):
        record = {"type": "qzss.dcr.typhoon", "is_test": True, "texts": {"ja": "警" * 120}}
        payload = display_payload(record, "live")
        self.assertEqual(payload["badge"], "訓練・試験")
        self.assertEqual(len(payload["text"]), 90)


if __name__ == "__main__":
    unittest.main()
