"""Check the independent alert audit's time and classification boundaries."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


_spec = importlib.util.spec_from_file_location(
    "audit_alert_candidates", Path(__file__).resolve().parents[1] / "scripts" / "audit_alert_candidates.py"
)
audit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(audit)


def report(age_seconds: int) -> dict:
    from datetime import datetime, timedelta, timezone

    received = datetime(2026, 10, 30, 16, 0, tzinfo=timezone.utc)
    issued = received - timedelta(seconds=age_seconds)
    return {
        "type": "qzss.dcr.weather",
        "is_test": False,
        "reception": {"at": received.isoformat()},
        "data": {
            "version": {"status": "valid", "value": 1},
            "report_classification": {"code": "3"},
            "information_type": {"code": "0"},
            "report_time": {"status": "valid", "value": issued.isoformat()},
        },
    }


class AuditAlertCandidatesTest(unittest.TestCase):
    def test_age_boundaries(self) -> None:
        self.assertEqual(audit.reason(report(0)), "candidate")
        self.assertEqual(audit.reason(report(180)), "candidate")
        self.assertEqual(audit.reason(report(181)), "older_than_3_minutes")
        self.assertEqual(audit.reason(report(-1)), "future_report_time")

    def test_training_and_cancellation_are_excluded(self) -> None:
        training = report(60)
        training["is_test"] = True
        self.assertEqual(audit.reason(training), "test_or_unknown_classification")
        cancellation = report(60)
        cancellation["data"]["information_type"]["code"] = "1"
        self.assertEqual(audit.reason(cancellation), "not_issue")


if __name__ == "__main__":
    unittest.main()
