"""A saved 32-byte L1S value must still decode like the received frame."""
from __future__ import annotations

from datetime import datetime, timezone
import importlib.util
from pathlib import Path
import unittest

import azarashi


def load_script(name: str):
    path = Path(__file__).resolve().parents[1] / "scripts" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


audit = load_script("audit_standalone_fields")
saved = load_script("decode_saved_alert")


class DecodeSavedAlertTest(unittest.TestCase):
    def test_reconstructed_l1s_matches_real_dcr_content(self) -> None:
        timestamp = datetime(2026, 10, 31, 2, 0, tzinfo=timezone.utc)
        for kind in ("ash_fall", "typhoon", "marine", "weather"):
            with self.subTest(kind=kind):
                frame = (Path(__file__).parent / "fixtures" / f"real_qzss_{kind}.ubx").read_bytes()
                l1s = audit.l1s_message(frame)
                self.assertIsNotNone(l1s)
                original = azarashi.to_json_dict(
                    azarashi.decode(frame, "ublox", timestamp=timestamp)
                )
                replayed = saved.decode(l1s.hex(), int(timestamp.timestamp()))
                self.assertEqual(replayed["type"], original["type"])
                self.assertEqual(replayed["data"], original["data"])

    def test_wrong_length_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            saved.reconstructed_sfrbx(bytes(31))


if __name__ == "__main__":
    unittest.main()
