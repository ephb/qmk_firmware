# SPDX-License-Identifier: GPL-2.0-or-later
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch

# Packet tests require no HID hardware or installed hidapi package.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "util"))
with patch.dict(sys.modules, {"hid": types.ModuleType("hid")}):
    import k4_bluetooth_control as control


class Device:
    def __init__(self, replies, write_count=33):
        self.replies = replies
        self.written = None
        self.write_count = write_count

    def write(self, data):
        self.written = data
        return self.write_count

    def read(self, length, timeout):
        return self.replies.pop(0)


def reply(sequence, operation, result=0):
    packet = bytearray(32)
    packet[:4] = control.MAGIC
    packet[4:7] = bytes((sequence, operation, result))
    return packet


class ControlTests(unittest.TestCase):
    def test_usb_report_and_connection_status(self):
        packet = reply(7, 0)
        packet[8:13] = bytes((1, 1, 1, 1, 2))
        packet[20:22] = bytes((0x34, 0x12))
        packet[30] = 4
        device = Device([packet])
        status = control.request(device, 0, sequence=7)
        self.assertEqual(device.written, bytes((0,)) + control.MAGIC + bytes((7, 0)) + bytes(26))
        self.assertTrue(status["connected"])
        self.assertEqual(status["tx"], 0x1234)
        self.assertEqual(status["profile"], 2)
        self.assertEqual(status["build"], 4)

    def test_ignores_another_clients_sequence(self):
        status = control.request(Device([reply(6, 0), reply(7, 0)]), 0, sequence=7)
        self.assertEqual(status["result"], "ok")

    def test_pair_accepted_is_queued_not_connected(self):
        status = control.request(Device([reply(1, 3, 1)]), 3, 2)
        self.assertEqual(status["result"], "queued")
        self.assertFalse(status["connected"])

    def test_selector_error_is_reported(self):
        with self.assertRaisesRegex(RuntimeError, "physical switch"):
            control.request(Device([reply(1, 3, 3)]), 3, 2)

    def test_unsupported_firmware_is_reported(self):
        with self.assertRaisesRegex(RuntimeError, "does not support"):
            control.request(Device([bytes((0xFF,)) + bytes(31)]), 0)

    def test_short_write_is_not_treated_as_a_sent_request(self):
        with self.assertRaisesRegex(OSError, "write failed"):
            control.request(Device([], write_count=1), 0)


if __name__ == "__main__":
    unittest.main()
