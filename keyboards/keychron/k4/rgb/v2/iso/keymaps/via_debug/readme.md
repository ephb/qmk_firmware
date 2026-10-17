# K4v2 ISO VIA Bluetooth debug build

```sh
qmk compile -kb keychron/k4/rgb/v2/iso -km via_debug -j 8
```

Flash `keychron_k4_rgb_v2_iso_via_debug.bin` from the repository root using
the Sonix flasher. This build retains VIA, Bluetooth and framebuffer RGB.
It enables the QMK HID console and Bluetooth diagnostics. Keyboard/media
reports share a USB endpoint to fit alongside VIA and the console. Stack
sizes follow upstream K4v2 debug PR #487 to fit the chip's 8 KiB SRAM.

## Capture logs on this Windows machine

Python 3.12 and its `hidapi` dependency are installed. From PowerShell in
`C:\Projects\sonix_qmk_firmware`, run:

```powershell
& 'C:\Python312\python.exe' '.\util\k4_bluetooth_console.py' --log '.\k4-bluetooth.log'
```

The reader waits for the console, reconnects after unplugging, and saves
timestamped output. Stop with Ctrl+C. QMK Toolbox's console or `qmk console`
on a host with direct USB access can also read it; WSL requires USB passthrough.

1. Keep USB plugged into the PC throughout the test.
2. Start in cable mode (`init=0`), then move the switch to Bluetooth.
   Look for `keychron_bt_init -> initialized=1` and `init=1` in the status.
3. Check the log shows `ACKs=1` (enabled by default in build 4).
4. Hold Fn+2 for four seconds, then release both keys and connect on the phone.
   Each hold sends just one pairing request.
5. Look for `MODULE CONNECTED` and `connected=1`, then type a few letters.
6. Stop capture and share `k4-bluetooth.log`.

Also test Bluetooth -> cable -> Bluetooth with the USB cable still attached.
Cable mode should log `keychron_bt_deinit -> SPI/INT stopped` and `init=0`;
returning to Bluetooth should initialize again. A cold boot in Bluetooth
mode should log `keychron_bt_init at boot (BT switch selected)`.

ACKs are enabled on each reboot; Fn+A toggles them once per keypress even if
the existing VIA EEPROM mapping predates this build. The setting is not
persisted. ACK handling applies to connection/disconnection/wake and unknown
battery notifications, not directly to the pairing request.

## Interpreting output

- `[k4v2]`: DIP inputs, selected transport/profile, pairing requests and ACK state.
- `[iton] T`: command queued to SPI, for example `A6 51 89` (enter pairing).
- `[iton] C`: SPI transfer completion and captured bytes, not command acceptance.
- `[iton] R`: module notification frame received on the INT line.
- `[iton] K`: keyboard report passed to SPI (key press/release payload).
- `MODULE CONFIRMED PAIRING`: decoded `B6 51 77` notification from the module.
- `timeout`: previous request's IRQ did not clear within 50 ms.
- `int_hi/int_lo`: module interrupt edges; `abort`: interrupted transfers.
- `dropped`: diagnostic ring overflow; not necessarily lost Bluetooth packets.
- `fifo`: bytes drained from hardware RX FIFO at the end of notifications.
- `state`/`idx`: current SPI driver state and receive-window position.
- `[iton] X`: bytes received without a complete recognized notification header.

Logging is deferred out of interrupt context and frame dumps are bounded.
It still changes timing, so compare observations with the normal build too.

## USB remote control (transport build 3 and newer)

Keep USB connected. Close VIA while sending diagnostic requests so it does
not consume the same Raw HID replies. The logging reader uses a separate
USB HID interface and can stay open.

From PowerShell in the repository root:

```powershell
& 'C:\Python312\python.exe' '.\util\k4_bluetooth_control.py' status
& 'C:\Python312\python.exe' '.\util\k4_bluetooth_control.py' acks on
& 'C:\Python312\python.exe' '.\util\k4_bluetooth_control.py' profile 2
& 'C:\Python312\python.exe' '.\util\k4_bluetooth_control.py' pair 2
& 'C:\Python312\python.exe' '.\util\k4_bluetooth_control.py' watch --seconds 30
```

Other commands: `restart` stops/restarts the MCU-side ITON driver and sends
Bluetooth-mode/profile commands; `wake` sends those commands without
restarting SPI; `battery` requests battery status; `acks off` disables ACKs.
Restarting this driver is not a power reset of the Bluetooth module.

Commands that talk to the Bluetooth module require the physical switch to
select Bluetooth. ACK settings can be changed in either mode, but rebooting
still restores the default. Pairing/restart commands return `queued` and
execute in keyboard housekeeping. `status` then shows current state; command
acceptance is not proof of successful Bluetooth pairing.

The private protocol is a 32-byte VIA Raw HID report with prefix
`FE 4B 34 01`, a sequence byte, operation byte and argument byte. Standard
VIA commands are unaffected. Diagnostic responses are explicitly sent over
the USB driver even when keyboard reports are routed to Bluetooth.

### Transport changes in build 3

- Rearm RX on `SPI_READY`, matching this checkout's ChibiOS completion macro.
- Capture remaining RX FIFO bytes before resetting SPI on the INT falling edge.
- Wait for FIFO reset before arming a new transfer.
- Preserve early notification bytes and skip command-tail padding only when
  a complete real notification header is present.
- Mark TX active only after the next command is actually being armed.

### Reconnect changes in build 4

- ACKs enabled by default; Fn+A still toggles them for diagnostics.
- Save the selected Bluetooth profile in user EEPROM and restore it at boot.
- Use the hardware-verified `mode_bt` + selected-profile sequence at startup
  and when retrying a disconnected link. First retry is one second after
  disconnect, with further retries spaced ten seconds apart.
- Suppress reconnect retries during a new pairing window (up to two minutes).
