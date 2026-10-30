import unittest
from types import SimpleNamespace

from qzss_watch.standalone import ContinuousRead, detect_ports


def port(name, vid, pid):
    return SimpleNamespace(device=name, vid=vid, pid=pid)


class StandaloneTests(unittest.TestCase):
    def test_detects_two_devices_without_port_number_assumptions(self):
        ports = [port("/dev/ttyUSB0", 0x1A86, 0x7523),
                 port("/dev/ttyACM0", 0x303A, 0x1001)]
        self.assertEqual(detect_ports(ports), ("/dev/ttyUSB0", "/dev/ttyACM0"))

    def test_waits_for_both_devices(self):
        self.assertIsNone(detect_ports([port("/dev/ttyUSB0", 0x1A86, 0x7523)]))

    def test_ambiguous_device_is_rejected(self):
        ports = [port("/dev/ttyUSB0", 0x1A86, 0x7523),
                 port("/dev/ttyUSB1", 0x1A86, 0x7523),
                 port("/dev/ttyACM0", 0x303A, 0x1001)]
        with self.assertRaises(ValueError):
            detect_ports(ports)

    def test_read_timeout_does_not_end_frame_stream(self):
        class FakePort:
            items = [b"", b"\xb5"]

            def read(self, _size):
                return self.items.pop(0)

        self.assertEqual(ContinuousRead(FakePort()).read(1), b"\xb5")


if __name__ == "__main__":
    unittest.main()
