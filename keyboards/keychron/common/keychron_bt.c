// Copyright 2024 1Conan (@1Conan) <me@1conan.com>
// SPDX-License-Identifier: GPL-2.0-or-later
// Based on 1Conan/qmk_firmware commit 802b4616961f8aaae289b34f96d8b3b0b1d767e9.
// Board code owns the module lifecycle; QMK's automatic init stays empty.
#include "bluetooth.h"
#include "iton_bt.h"
#include "keychron_bt.h"

static bool initialized;

bool keychron_bt_is_initialized(void) {
    return initialized;
}

void keychron_bt_init(void) {
    if (!initialized) {
        iton_bt_init();
        initialized = true;
    }
}

void keychron_bt_deinit(void) {
    if (initialized) {
        initialized = false;
        iton_bt_deinit();
    }
}

void bluetooth_init(void) {}

void bluetooth_task(void) {
    if (initialized) iton_bt_task();
}

bool bluetooth_is_connected(void) {
    return initialized && iton_bt_is_connected;
}

void bluetooth_send_keyboard(report_keyboard_t *report) {
    if (initialized) iton_bt_send_keyboard(report);
}

void bluetooth_send_mouse(report_mouse_t *report) {}

void bluetooth_send_consumer(uint16_t usage) {
    if (initialized) iton_bt_send_consumer(usage);
}

void bluetooth_send_system(uint16_t usage) {
    if (initialized) iton_bt_send_system(usage);
}

uint8_t bluetooth_keyboard_leds(void) {
#ifdef ITON_BT_ENABLE_LED_STATE
    return initialized ? iton_bt_led_state : 0;
#else
    return 0;
#endif
}
