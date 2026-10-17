# ITON host-side regression checks

Run from the repository root on a Linux host or QMK WSL with GCC:

```sh
gcc -std=c11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined \
    -I tests/iton_bt_transport/stubs tests/iton_bt_transport/transport.c \
    -o /tmp/iton_bt_transport_test
/tmp/iton_bt_transport_test
python3 -m unittest discover -s tests/iton_bt_transport -p 'test_*.py'
```

The C harness includes the actual ITON driver and generic Bluetooth dispatch
against a minimal HAL/GPIO/timer mock. It covers the ChibiOS READY callback
regression seen on hardware, early/padded/malformed notifications, deferred
ACK dispatch and sixth-key bitmap bounds. Sanitizers catch memory violations.
The mock does not emulate hardware FIFO timing, RF behavior or pairing.

Python checks cover USB packet serialization, response correlation, queued
versus connected state, firmware errors and short writes. They need neither
hidapi nor a connected keyboard.
