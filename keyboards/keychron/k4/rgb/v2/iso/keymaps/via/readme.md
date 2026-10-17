# Keychron K4v2 RGB ISO: VIA + Bluetooth

Build from the repository root in the QMK environment:

```sh
qmk compile -kb keychron/k4/rgb/v2/iso -km via -j 8
```

From Windows PowerShell using the existing QMK WSL installation:

```powershell
wsl -d QMK --cd /mnt/c/Projects/sonix_qmk_firmware -- qmk compile -kb keychron/k4/rgb/v2/iso -km via -j 8
```

Output: `keychron_k4_rgb_v2_iso_via.bin` in the repository root.
Use the Sonix flasher described in the keyboard's readme.

## Features and controls

- VIA: three dynamic layers (0 Windows base, 1 Fn, 2 Mac base).
  Connect over USB to configure VIA. If a definition is needed, import
  `keyboards/keychron/k4/rgb/v2/via_json/k4_iso.json` using VIA's Design tab.
- Framebuffer RGB: Typing Heatmap and Digital Rain, alongside the effects
  already enabled by this keyboard. Cycle effects using the lighting key.
- Physical cable/Bluetooth selector controls the active transport.
  Following 1Conan's commit `802b461`, `BLUETOOTH_DRIVER = custom` leaves
  QMK's automatic `bluetooth_init()` empty. Board code calls
  `keychron_bt_init()` only when D5 selects Bluetooth. Returning to cable
  mode finishes the USB-mode command, disables the INT callback, stops SPI,
  and restores the IRQ/INT pins to inputs through `keychron_bt_deinit()`.
- Fn+1/2/3 selects a Bluetooth profile; hold for three seconds to pair.
  A hold sends one pairing request; release before connecting on the host.
  The corresponding number key flashes blue during pairing.
  The selected profile is saved in QMK's user EEPROM and restored at boot.
  The firmware retries the existing connection after a disconnect and every
  ten seconds while disconnected, without interrupting a new pairing window.
- Fn+B displays the battery-level range on the number row.
- Physical Fn+A toggles notification ACKs on/off at runtime. They start on
  after every reboot. The debug build logs the current setting as `ACKs=0/1`.
- The Mac/Windows switch selects the corresponding base layer.
  Mac/iOS mode maps the left Windows key to Option, left Alt to Command,
  and right Alt to right Command. Language layout (QWERTZ/QWERTY) is host-side.

## Bluetooth source and verification

The VIA layout is based on SonixQMK/qmk_userspace_via. Bluetooth transport
and board integration are adapted from unmerged SonixQMK/qmk_firmware
PRs #485 and #486, without console diagnostics or Fn-layer RGB highlighting.
The required SPI receive bounds/tail fix from ChibiOS-Contrib PR #70 is
already present in this checkout's submodule.

`ITON_BT_ENABLE_ACK` is defined, so ACKs start enabled. Runtime ACK
support (`ITON_BT_RUNTIME_ACK`) allows testing with Fn+A. When enabled,
connection/disconnection/wake/unknown-notification ACKs are queued in the ISR
and sent by `bluetooth_task()` after the receive handler has finished.

The driver author's February/October 2025 Discord discussion explains that
ACKs were not a universal ITON default because the original target was not
a Keychron. A K6 user reported improved sleep/wake reconnection with ACKs;
this is evidence for testing, not confirmation of K4v2 behaviour.

Pairing and typing on an iPhone, valid connection notifications and ACKs,
and reconnection using `mode_bt` plus profile selection were verified on
the user's K4v2 with debug build 3. Build 4 automates that reconnect sequence,
enables ACKs by default and saves profile selection; reboot, automatic
reconnect and long-idle sleep/wake behavior need hardware verification.

For USB diagnostic logging, see the adjacent `via_debug/readme.md`.
Both builds also expose the USB diagnostic control protocol documented there.

## Review and userspace follow-up

The `keymap.c`, three-layer `config.h`, VIA `rules.mk`, and keymap readme are
based on or fill the role of files in
[`SonixQMK/qmk_userspace_via`](https://github.com/SonixQMK/qmk_userspace_via/tree/main/keyboards/keychron/k4/rgb/v2/iso/keymaps/via).
They are tracked here temporarily so this branch can reproduce the working
firmware without an external userspace checkout. These are candidates for
a later userspace change, not an already-published userspace update.

Before migrating, extract the shared board integration currently kept in
`via/bluetooth.c`, `via/halconf.h` and `via/mcuconf.h` into an appropriate
board-level location, or carry those dependencies explicitly. The local
`via_debug` keymap includes `../via` files and also needs adapting if the
VIA keymap moves. Its debug-only build settings can remain in firmware.
Do not copy the modified `rules.mk` to userspace without resolving these
source paths. The generic ITON driver, common Keychron lifecycle wrapper,
host utilities and regression checks belong in the firmware repository.

Review checks cover ChibiOS completion-state handling, notification framing,
generic/custom ACK dispatch, bitmap bounds and USB command/reply matching.
The module's six-key workaround has a 120-bit bitmap: an extended usage in
slot six cannot be represented there; it is safely omitted from that bitmap
instead of writing beyond it. The ordinary HID report is still sent.

Hardware testing confirmed iPhone typing, connections on Windows/iPad
profiles and the existing-pairing reconnect sequence. Occasional malformed module status
notifications were observed, including a connected host while the MCU still
reported disconnected. Those frames are not treated as valid connections.
Full battery-only reboot/profile retention and long-idle sleep/wake testing
remain outstanding; successful compilation is not a substitute for them.
