"""Cross-check the firmware-shaped L1S extraction against an actual frame."""
from __future__ import annotations

from datetime import datetime, timezone
import importlib.util
from pathlib import Path
import unittest


_spec = importlib.util.spec_from_file_location(
    "audit_standalone_fields", Path(__file__).resolve().parents[1] / "scripts" / "audit_standalone_fields.py"
)
audit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(audit)


class AuditStandaloneFieldsTest(unittest.TestCase):
    def test_real_dcr_header_agrees_with_azarashi(self) -> None:
        frame = (Path(__file__).parent / "fixtures" / "real_qzss_ash_fall.ubx").read_bytes()
        message = audit.l1s_message(frame)
        self.assertIsNotNone(message)
        self.assertEqual(audit.bits(message, 8, 6), 43)
        self.assertEqual(audit.compare_dcr(frame, message, datetime(2026, 10, 30, tzinfo=timezone.utc)), [])

    def test_corrupted_ubx_is_rejected(self) -> None:
        frame = bytearray((Path(__file__).parent / "fixtures" / "real_qzss_ash_fall.ubx").read_bytes())
        frame[-1] ^= 1
        self.assertIsNone(audit.l1s_message(bytes(frame)))


if __name__ == "__main__":
    unittest.main()
