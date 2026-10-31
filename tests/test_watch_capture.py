"""The watch capture retains MT43/44 frames and distinguishes DCX A1 tests."""
from datetime import datetime, timezone
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from scripts.decode_watch_capture import decode_file, ubx_sfrbx
from scripts.export_watch_capture import export, is_dcx_null
from scripts.generate_dcx_fixtures import frames as dcx_frames
from qzss_watch.core import decode_record


class WatchCaptureTests(unittest.TestCase):
    def test_reconstructs_ubx_payload(self):
        frame = ubx_sfrbx(dcx_frames()[-1])
        self.assertEqual(frame[:6], bytes.fromhex("b56202132800"))
        self.assertEqual(frame[6:46], dcx_frames()[-1])

    def test_null_dcx_does_not_count_as_a1_test(self):
        null_payload = bytes.fromhex(
            "05030100094102000d60b0c6000000e00000000000000000000000000000000001000000546cb64424c7a1f2"
        )
        self.assertTrue(is_dcx_null(null_payload))
        self.assertFalse(is_dcx_null(dcx_frames()[-1]))

    def test_dcx_test_and_real_actions(self):
        at = datetime(2026, 11, 3, tzinfo=timezone.utc)
        for payload, expected in ((dcx_frames()[-1], True), (dcx_frames()[0], False)):
            record = decode_record(ubx_sfrbx(payload), at, "capture")
            self.assertEqual(record["type"], "qzss.dcx.l_alert")
            self.assertIs(record["is_test"], expected)

    def test_decoding_keeps_outside_area_and_mt43(self):
        dcr = (Path(__file__).parent / "fixtures" / "real_qzss_ash_fall.ubx").read_bytes()
        rows = [
            {"ms": 1, "utc": 0, "mt": 44, "a1": 0, "sfrbx": dcx_frames()[-1].hex()},
            {"ms": 2, "utc": 0, "mt": 43, "a1": -1, "sfrbx": dcr[6:-2].hex()},
        ]
        temp_root = Path("work/tmp")
        temp_root.mkdir(parents=True, exist_ok=True)
        with TemporaryDirectory(dir=temp_root) as temp:
            source = Path(temp) / "capture.jsonl"
            target = Path(temp) / "decoded.jsonl"
            source.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            counts = decode_file(source, target)
            decoded = [json.loads(line) for line in target.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(counts["mt44_test"], 1)
        self.assertEqual(decoded[0]["decoded"]["type"], "qzss.dcx.l_alert")
        self.assertTrue(decoded[0]["a1_match"])
        self.assertEqual(decoded[1]["decoded"]["type"], "qzss.dcr.ash_fall")

    def test_export_preserves_exact_jsonl_bytes(self):
        payload = b'{"mt":44,"a1":0,"sfrbx":"' + dcx_frames()[-1].hex().encode() + b'"}\n'

        class FakePort:
            def __init__(self):
                self.buffer = bytearray(
                    f"LOG_BEGIN 0 {len(payload)}\n".encode() + payload + b"LOG_END 0\nLOG_DONE\n"
                )
                self.sent = b""

            def write(self, data):
                self.sent += data

            def flush(self):
                pass

            def readline(self):
                end = self.buffer.index(10) + 1
                result = bytes(self.buffer[:end])
                del self.buffer[:end]
                return result

            def read(self, length):
                result = bytes(self.buffer[:min(length, 17)])
                del self.buffer[:len(result)]
                return result

        temp_root = Path("work/tmp")
        temp_root.mkdir(parents=True, exist_ok=True)
        with TemporaryDirectory(dir=temp_root) as temp:
            port = FakePort()
            paths = export(port, Path(temp))
            self.assertEqual(port.sent, b"DUMP\n")
            self.assertEqual(len(paths), 1)
            self.assertEqual(paths[0].read_bytes(), payload)


if __name__ == "__main__":
    unittest.main()
