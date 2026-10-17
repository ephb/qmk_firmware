#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read the K4v2 QMK USB debug console on Windows (requires pip install hidapi)."""

import argparse
from contextlib import ExitStack
from datetime import datetime
from pathlib import Path
import sys
import time

import hid


def console_devices():
    return [
        device for device in hid.enumerate(0x3434, 0xFE07)
        if device.get("usage_page") == 0xFF31 and device.get("usage") == 0x74
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List matching USB console interfaces and exit")
    parser.add_argument("--seconds", type=float, default=0, help="Stop after this many seconds; 0 waits until Ctrl+C")
    parser.add_argument("--log", type=Path, help="Also append timestamped output to this file")
    args = parser.parse_args()
    if args.list:
        for device in console_devices():
            print(device["path"].decode(errors="replace"))
        return

    with ExitStack() as stack:
        logfile = stack.enter_context(args.log.open("a", encoding="utf-8")) if args.log else None

        def emit(text):
            line = f"{datetime.now().isoformat(timespec='milliseconds')} {text}"
            print(line, flush=True)
            if logfile:
                logfile.write(line + "\n")
                logfile.flush()

        emit("Waiting for K4v2 USB console (3434:FE07). Ctrl+C to stop.")
        started = time.monotonic()
        device = None
        pending = ""
        try:
            while not args.seconds or time.monotonic() - started < args.seconds:
                if device is None:
                    devices = console_devices()
                    if not devices:
                        time.sleep(0.5)
                        continue
                    device = hid.device()
                    try:
                        device.open_path(devices[0]["path"])
                    except OSError as error:
                        device.close()
                        device = None
                        emit(f"Could not open console: {error}")
                        time.sleep(1)
                        continue
                    emit("Console connected. Keep USB plugged in while testing Bluetooth.")
                try:
                    packet = device.read(64, 200)
                except OSError:
                    device.close()
                    device = None
                    if pending:
                        emit(pending)
                        pending = ""
                    emit("Console disconnected; waiting for reconnect.")
                    continue
                if not packet:
                    continue
                pending += bytes(packet).replace(b"\x00", b"").decode("utf-8", errors="replace")
                while "\n" in pending:
                    line, pending = pending.split("\n", 1)
                    emit(line.rstrip("\r"))
        except KeyboardInterrupt:
            pass
        finally:
            if pending:
                emit(pending)
            if device is not None:
                device.close()


if __name__ == "__main__":
    sys.exit(main())
