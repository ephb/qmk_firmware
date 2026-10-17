#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Control the K4v2 Bluetooth diagnostics over its USB VIA Raw HID interface."""

import argparse
import json
import sys
import time

import hid

MAGIC = bytes((0xFE, ord("K"), ord("4"), 1))
OPERATIONS = {"status": 0, "acks": 1, "profile": 2, "pair": 3, "restart": 4, "wake": 5, "battery": 6}
RESULTS = {0: "ok", 1: "queued", 2: "invalid request", 3: "set physical switch to Bluetooth", 4: "busy"}


def interfaces():
    return [d for d in hid.enumerate(0x3434, 0xFE07)
            if d.get("usage_page") == 0xFF60 and d.get("usage") == 0x61]


def decode(packet):
    return {
        "result": RESULTS.get(packet[6], f"error {packet[6]}"),
        "pending": packet[7], "bluetooth_mode": bool(packet[8]),
        "initialized": bool(packet[9]), "connected": bool(packet[10]),
        "acks": bool(packet[11]), "profile": packet[12],
        "D5": packet[13], "D6": packet[14], "host": packet[15],
        "spi_state": packet[16], "IRQ": packet[17], "INT": packet[18], "rx_index": packet[19],
        **{name: int.from_bytes(packet[offset:offset + 2], "little")
           for name, offset in (("tx", 20), ("done", 22), ("timeouts", 24), ("interrupts", 26), ("fifo_drained", 28))},
        "build": packet[30],
    }


def request(device, operation, argument=0, sequence=1):
    packet = bytearray(32)
    packet[:4] = MAGIC
    packet[4:7] = bytes((sequence, operation, argument))
    report = bytes((0,)) + packet
    if device.write(report) != len(report):
        raise OSError("USB write failed")
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        reply = bytes(device.read(32, 200))
        if len(reply) == 32 and reply[:4] == MAGIC and reply[4] == sequence and reply[5] == operation:
            result = decode(reply)
            if reply[6] > 1:
                raise RuntimeError(result["result"])
            return result
        if reply and reply[0] == 0xFF:
            raise RuntimeError("Firmware does not support USB control. Flash the new build.")
    raise TimeoutError("No USB control reply. Flash build 3 or newer and close VIA while using this tool.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List matching USB interfaces")
    sub = parser.add_subparsers(dest="command")
    sub.add_parser("status")
    watch = sub.add_parser("watch")
    watch.add_argument("--seconds", type=float, default=30)
    ack = sub.add_parser("acks")
    ack.add_argument("state", choices=("on", "off"))
    for name in ("profile", "pair"):
        command = sub.add_parser(name)
        command.add_argument("profile", type=int, choices=(1, 2, 3))
    for name in ("restart", "wake", "battery"):
        sub.add_parser(name)
    args = parser.parse_args()
    devices = interfaces()
    if args.list:
        for item in devices:
            print(item["path"].decode(errors="replace"))
        return
    if not devices:
        raise RuntimeError("K4v2 USB VIA interface not found. Keep USB connected to this PC.")
    if len(devices) > 1:
        raise RuntimeError("Multiple K4v2 interfaces found; connect only the keyboard being tested.")
    device = hid.device()
    try:
        device.open_path(devices[0]["path"])
        command = args.command or "status"
        if command == "watch":
            deadline = time.monotonic() + args.seconds
            sequence = 1
            while time.monotonic() < deadline:
                print(json.dumps(request(device, 0, sequence=sequence)), flush=True)
                sequence = sequence % 255 + 1
                time.sleep(1)
        else:
            argument = int(args.state == "on") if command == "acks" else getattr(args, "profile", 0)
            result = request(device, OPERATIONS[command], argument)
            print(json.dumps(result, indent=2), flush=True)
            if result["result"] == "queued":
                time.sleep(0.3)
                print("After execution:")
                print(json.dumps(request(device, 0, sequence=2), indent=2), flush=True)
    finally:
        device.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except (OSError, RuntimeError, TimeoutError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
