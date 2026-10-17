// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include "../../drivers/bluetooth/iton_bt.c"
#include "../../drivers/bluetooth/bluetooth_drivers.c"

// Model the SPI ISR capturing bytes before the INT-high handler gets CPU.
// Padding models command reply tails received by the always-armed listener.
static void notification_frame(uint8_t code, unsigned early_bytes, unsigned padding) {
    const uint8_t frame[] = {notification, notif_bluetooth, code};
    for (unsigned i = 0; i < padding; i++) SPID0.rxbuf[SPID0.idx++] = 0;
    for (unsigned i = 0; i < early_bytes; i++) SPID0.rxbuf[SPID0.idx++] = frame[i];
    pins[A1] = true;
    iton_bt_rx_cb(NULL);
    for (unsigned i = early_bytes; i < sizeof(frame); i++) SPID0.rxbuf[SPID0.idx++] = frame[i];
    pins[A1] = false;
    iton_bt_rx_cb(NULL);
}

static void complete_tx(void) {
    // ChibiOS __spi_isr_complete_code sets READY before invoking data_cb.
    SPID0.state = SPI_READY;
    SPID0.idx = SPID0.count;
    iton_bt_data_cb(&SPID0);
}

int main(void) {
    hardware.DFDLY = 1;
    iton_bt_init();
    assert(hardware.DFDLY == 0);
    iton_bt_tx_active = true;
    SPID0.count = 3;
    pins[A0] = true;
    complete_tx();
    assert(!pins[A0]);
    assert(SPID0.state == SPI_ACTIVE && SPID0.idx == 0 && SPID0.count == sizeof(iton_bt_rx_buffer));

    for (unsigned early = 0; early <= 3; early++) {
        iton_bt_init();
        iton_bt_set_ack_enabled(false);
        notification_frame(bt_connection_success, early, 0);
        assert(iton_bt_is_connected);
        notification_frame(bt_disconected, early, 0);
        assert(!iton_bt_is_connected);
        assert(SPID0.state == SPI_ACTIVE && SPID0.idx == 0);
    }
    iton_bt_init();
    notification_frame(bt_connection_success, 1, 5);
    assert(iton_bt_is_connected);
    notification_frame(bt_disconected, 2, 9);
    assert(!iton_bt_is_connected);

    // Neither a headerless nor a truncated frame may invent a connection.
    iton_bt_init();
    SPID0.rxbuf[0] = notif_bluetooth;
    SPID0.rxbuf[1] = bt_connection_success;
    SPID0.idx = 2;
    pins[A1] = false;
    iton_bt_rx_cb(NULL);
    assert(!iton_bt_is_connected);
    SPID0.rxbuf[0] = notification;
    SPID0.rxbuf[1] = notif_bluetooth;
    SPID0.idx = 2;
    iton_bt_rx_cb(NULL);
    assert(!iton_bt_is_connected);

    // ACK survives RX rearming and is sent by the generic driver's task.
    iton_bt_init();
    iton_bt_set_ack_enabled(true);
    sent_len = 0;
    notification_frame(bt_connection_success, 1, 0);
    assert(iton_bt_is_connected && sent_len == 0);
    bluetooth_task();
    assert(sent_len == 3 && sent[0] == control && sent[1] == control_bt && sent[2] == connect_ack);

    // Boundary and international usages in slot six must not corrupt memory.
    const uint8_t usages[] = {119, 120, 135, 255};
    for (unsigned i = 0; i < sizeof(usages); i++) {
        report_keyboard_t report = {0};
        iton_bt_init();
        report.keys[5] = usages[i];
        iton_bt_send_keyboard(&report);
        assert(sent_len == 16 && sent[0] == report_nkro);
        for (unsigned byte = 1; byte < 16; byte++) {
            assert(sent[byte] == (usages[i] == 119 && byte == 15 ? 0x80 : 0));
        }
        complete_tx();
        report.keys[5] = 0;
        iton_bt_send_keyboard(&report);
        assert(sent[0] == report_nkro);
        for (unsigned byte = 1; byte < 16; byte++) assert(sent[byte] == 0);
    }
    puts("PASS: RX completion, early/padded/malformed notifications, generic ACK task, sixth-key bounds");
}
