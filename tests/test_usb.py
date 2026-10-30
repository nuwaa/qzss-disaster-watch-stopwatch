import os
import unittest
from unittest.mock import patch

from qzss_watch.usb import WatchUsb


class FakeSerial:
    def __init__(self, response=b"WATCH_ACK stored\n"):
        self.response = response
        self.written = b""
        self.chunks = []

    def write(self, data):
        self.written += data
        self.chunks.append(data)

    def flush(self):
        pass

    def readline(self):
        return self.response


class UsbTransportTests(unittest.TestCase):
    def test_sample_frame_and_ack(self):
        fake = FakeSerial()
        watch = WatchUsb("COM10")
        watch.serial = fake
        record = {"message_id": "example", "type": "qzss.dcr.weather",
                  "texts": {"ja": "訓練です" * 20}, "is_test": True}
        with patch.dict(os.environ, {"QZSS_WATCH_TOKEN": "secret"}):
            self.assertEqual(watch.send(record, "sample"), "stored")
        self.assertTrue(fake.written.startswith(b"WATCH_REPORT secret "))
        self.assertTrue(fake.written.endswith(b"\n"))
        self.assertIn("訓練です".encode(), fake.written)
        self.assertIn("デモ・訓練".encode(), fake.written)
        self.assertGreater(len(fake.chunks), 1)
        self.assertTrue(all(len(chunk) <= 32 for chunk in fake.chunks))

    def test_rejection(self):
        watch = WatchUsb("COM10")
        watch.serial = FakeSerial(b"WATCH_ACK forbidden\n")
        with patch.dict(os.environ, {"QZSS_WATCH_TOKEN": "secret"}):
            with self.assertRaises(OSError):
                watch.send({"message_id": "example"}, "sample")


if __name__ == "__main__":
    unittest.main()
