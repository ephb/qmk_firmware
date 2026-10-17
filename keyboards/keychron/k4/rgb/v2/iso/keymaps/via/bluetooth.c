// Copyright 2025 QMK
// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from SonixQMK/qmk_firmware PR #486.
#include "quantum.h"
#include "gpio.h"
#include "timer.h"
#include "iton_bt.h"
#include "keychron_bt.h"
#include "connection.h"
#include "host.h"
#include "eeconfig.h"
#include <hal.h>

#ifdef ITON_BT_DEBUG
#    include "print.h"
#    define BT_LOG(...) uprintf(__VA_ARGS__)
#else
#    define BT_LOG(...) do {} while (0)
#endif

#define BT_PROFILE_RESET_HOLD_MS 3000
#define BT_LED_BASE_INDEX 20
#define BT_LED_CONNECTED_MS 2500
#define BT_BATT_QUERY_MS 60000
#define BT_BATT_SHOW_MS 5000
#define BT_RECONNECT_MS 10000
#define BT_PAIRING_WINDOW_MS 120000UL
#define BT_PROFILE_EEPROM_MAGIC 0x4B345600UL

static bool bt_ready, bt_mode, bt_mode_sent, mac_mode;
static uint8_t bt_profile;
static uint16_t bt_held_keycode, bt_held_timer, bt_mode_timer;
static bool bt_pair_requested;
static volatile bool bt_pairing, bt_reconnect_due;
static volatile uint32_t bt_pairing_since;
static uint16_t bt_reconnect_timer;
static volatile uint8_t bt_led_state, bt_batt_level;
static volatile uint16_t bt_led_timer;
static volatile bool bt_batt_due;
static uint16_t bt_batt_timer, bt_batt_show_timer;
static bool bt_batt_show;
static uint16_t mac_fkeys;
static bool mac_fn_rcmd;

// Private VIA extension: FE 'K' '4' version sequence opcode argument.
// Standard VIA packets continue through QMK's normal handler.
enum { BT_REMOTE_STATUS = 0, BT_REMOTE_ACKS, BT_REMOTE_PROFILE, BT_REMOTE_PAIR,
       BT_REMOTE_RESTART, BT_REMOTE_WAKE, BT_REMOTE_BATTERY };
static uint8_t remote_pending, remote_argument;
static void k4v2_remote_task(void);

static void k4v2_save_profile(void) {
    uint32_t value = BT_PROFILE_EEPROM_MAGIC | bt_profile;
    if (eeconfig_read_user() != value) eeconfig_update_user(value);
}

static void k4v2_select_profile(uint8_t profile) {
    // Release held keys on the old host before changing Bluetooth profiles.
    clear_keyboard();
    bt_profile = profile;
    bt_pairing = false;
    bt_led_state = 0;
    k4v2_save_profile();
    iton_bt_switch_profile(bt_profile);
    bt_reconnect_timer = timer_read();
}

static void k4v2_wake(void) {
    iton_bt_mode_bt();
    iton_bt_switch_profile(bt_profile);
    bt_reconnect_timer = timer_read();
}

static void k4v2_pair(void) {
    bt_pairing = true;
    bt_pairing_since = timer_read32();
    bt_led_state = 1;
    bt_led_timer = timer_read();
    iton_bt_enter_pairing();
}

static const uint16_t PROGMEM mac_fn_defaults[] = {
    KC_SLCT, KC_PAUS, KC_APP, KC_F5, RM_VALD, RM_VALU,
    KC_MPRV, KC_MPLY, KC_MNXT, KC_MUTE, KC_VOLD, KC_VOLU,
};

static void k4v2_apply_dip_mode(bool cable_active) {
    bt_mode = !cable_active;
    BT_LOG("[k4v2] mode=%s profile=%u\n", bt_mode ? "BT" : "USB", bt_profile + 1);
    if (bt_mode) {
        keychron_bt_init();
        BT_LOG("[k4v2] keychron_bt_init -> initialized=%u\n", keychron_bt_is_initialized());
        k4v2_wake();
        iton_bt_enable_sleep();
        bt_batt_due = true;
        connection_set_host_noeeprom(CONNECTION_HOST_BLUETOOTH);
    } else {
        connection_set_host_noeeprom(CONNECTION_HOST_USB);
        if (keychron_bt_is_initialized()) {
            iton_bt_mode_usb();
            keychron_bt_deinit();
            BT_LOG("[k4v2] keychron_bt_deinit -> SPI/INT stopped\n");
        }
        bt_held_keycode = 0;
        bt_led_state = 0;
        bt_batt_show = false;
        bt_pairing = false;
        bt_reconnect_due = false;
    }
}

bool dip_switch_update_kb(uint8_t index, bool active) {
    BT_LOG("[k4v2] DIP index=%u active=%u ready=%u\n", index, active, bt_ready);
    if (!dip_switch_update_user(index, active)) {
        return false;
    }
    // DIP input is inverted by QMK: active D5 means cable, active D6 means Mac.
    if (index == 0 && bt_ready) {
        // Suppress the deferred boot command once the switch selects a mode.
        bt_mode_sent = true;
        k4v2_apply_dip_mode(active);
    } else if (index == 1) {
        mac_mode = active;
        if (bt_ready) {
            clear_keyboard();
        }
        mac_fkeys = 0;
        mac_fn_rcmd = false;
        layer_clear();
        default_layer_set((layer_state_t)1 << (mac_mode ? K4V2_LAYER_MAC_BASE : K4V2_LAYER_WIN_BASE));
    }
    return true;
}

void keyboard_post_init_kb(void) {
    bt_ready = true;
    uint32_t saved = eeconfig_read_user();
    bt_profile = (saved & 0xFFFFFF00UL) == BT_PROFILE_EEPROM_MAGIC && (saved & 0xFF) < 3 ? saved & 0xFF : 0;
    // QMK bluetooth_init() is empty for the custom driver. Start the hardware
    // only if D5 selects BT, after QMK's connection/keyboard init has completed.
    bt_mode = gpio_read_pin(D5);
    if (bt_mode) {
        keychron_bt_init();
        BT_LOG("[k4v2] keychron_bt_init at boot (BT switch selected)\n");
    } else {
        BT_LOG("[k4v2] cable boot: ITON left uninitialized\n");
    }
    connection_set_host_noeeprom(bt_mode ? CONNECTION_HOST_BLUETOOTH : CONNECTION_HOST_USB);
    bt_mode_timer = timer_read();
    BT_LOG("[k4v2] transport build=4 USB-control=1 D5=%u D6=%u bt=%u (module command delayed)\n", (unsigned)gpio_read_pin(D5), (unsigned)gpio_read_pin(D6), bt_mode);
    BT_LOG("[k4v2] ACKs=%u (Fn+A toggles)\n", iton_bt_get_ack_enabled());
    keyboard_post_init_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    // Physical Fn+A works with existing VIA EEPROM keymaps too. Latch the
    // press so releasing Fn first cannot leak an unmatched A key release.
    static bool ack_toggle_held;
    if (record->event.key.row == 3 && record->event.key.col == 1 &&
        ((record->event.pressed && layer_state_is(K4V2_LAYER_FN)) || ack_toggle_held)) {
        ack_toggle_held = record->event.pressed;
        if (record->event.pressed) {
            iton_bt_set_ack_enabled(!iton_bt_get_ack_enabled());
            BT_LOG("[k4v2] ACKs=%u (toggled with Fn+A; resets on reboot)\n", iton_bt_get_ack_enabled());
        }
        return false;
    }
    if (!process_record_user(keycode, record)) {
        return false;
    }
    if (mac_mode) {
        if (keycode == MO(K4V2_LAYER_FN)) {
            if (record->event.pressed) {
                default_layer_set(0);
                layer_on(K4V2_LAYER_FN);
            } else {
                layer_off(K4V2_LAYER_FN);
                default_layer_set((layer_state_t)1 << K4V2_LAYER_MAC_BASE);
            }
            return false;
        }
        if (record->event.key.row == 0 && record->event.key.col >= 1 && record->event.key.col <= 12) {
            uint8_t index = record->event.key.col - 1;
            uint16_t expected = pgm_read_word(&mac_fn_defaults[index]);
            if (record->event.pressed && layer_state_is(K4V2_LAYER_FN) && keycode == expected) {
                mac_fkeys |= (uint16_t)1 << index;
                register_code(KC_F1 + index);
                return false;
            }
            if (!record->event.pressed && (mac_fkeys & ((uint16_t)1 << index))) {
                mac_fkeys &= ~((uint16_t)1 << index);
                unregister_code(KC_F1 + index);
                return false;
            }
        }
        if (record->event.key.row == 5 && record->event.key.col == 10) {
            if (record->event.pressed && layer_state_is(K4V2_LAYER_FN) && keycode == KC_RALT) {
                mac_fn_rcmd = true;
                register_code(KC_RGUI);
                return false;
            }
            if (!record->event.pressed && mac_fn_rcmd) {
                mac_fn_rcmd = false;
                unregister_code(KC_RGUI);
                return false;
            }
        }
    }
    if (keycode == KC_BT_BAT) {
        if (record->event.pressed && bt_mode) {
            bt_batt_show_timer = timer_read();
            bt_batt_show = true;
            iton_bt_query_battery_level();
        }
        return false;
    }
    if (bt_mode && keycode >= BT_PRF1 && keycode <= BT_PRF3) {
        if (record->event.pressed) {
            bt_pair_requested = false;
            bt_held_keycode = keycode;
            bt_held_timer = timer_read();
            k4v2_select_profile(keycode - BT_PRF1);
            BT_LOG("[k4v2] profile=%u PRESS -> switch_profile (saved)\n", bt_profile + 1);
        } else {
            BT_LOG("[k4v2] profile RELEASE held_ms=%u\n", timer_elapsed(bt_held_timer));
            bt_held_keycode = 0;
        }
        return false;
    }
    return true;
}

void housekeeping_task_kb(void) {
    k4v2_remote_task();
    if (!bt_mode_sent && timer_elapsed(bt_mode_timer) >= 1000) {
        bt_mode_sent = true;
        k4v2_apply_dip_mode(!bt_mode);
    }
    if (bt_mode && bt_held_keycode && !bt_pair_requested && timer_elapsed(bt_held_timer) >= BT_PROFILE_RESET_HOLD_MS) {
        // One request per hold. Reissuing every 100 ms repeatedly restarted
        // pairing and flooded the debug ring while waiting for the phone.
        bt_pair_requested = true;
        BT_LOG("[k4v2] PAIR REQUEST profile=%u (not yet confirmed by module)\n", bt_profile + 1);
        k4v2_pair();
    }
    if (bt_reconnect_due) {
        bt_reconnect_due = false;
        // Allow the queued disconnect ACK to go first, then retry after 1s.
        bt_reconnect_timer = timer_read() - (BT_RECONNECT_MS - 1000);
    }
    if (bt_pairing && timer_elapsed32(bt_pairing_since) >= BT_PAIRING_WINDOW_MS) {
        bt_pairing = false;
        bt_led_state = 0;
    }
    if (bt_mode && bt_mode_sent && !iton_bt_is_connected && !bt_pairing &&
        !bt_held_keycode && timer_elapsed(bt_reconnect_timer) >= BT_RECONNECT_MS) {
        BT_LOG("[k4v2] reconnect profile=%u\n", bt_profile + 1);
        k4v2_wake();
    }
    if (bt_mode && (bt_batt_due || (iton_bt_is_connected && timer_elapsed(bt_batt_timer) >= BT_BATT_QUERY_MS))) {
        bt_batt_due = false;
        bt_batt_timer = timer_read();
        iton_bt_query_battery_level();
    }
#ifdef ITON_BT_DEBUG
    iton_bt_debug_task();
    static uint16_t debug_status_timer;
    if (timer_elapsed(debug_status_timer) >= 1000) {
        debug_status_timer = timer_read();
        BT_LOG("[k4v2] D5=%u D6=%u bt=%u mac=%u host=%u connected=%u profile=%u batt=%02X ACKs=%u init=%u\n",
               (unsigned)gpio_read_pin(D5), (unsigned)gpio_read_pin(D6), bt_mode, mac_mode, connection_get_host(),
               iton_bt_is_connected, bt_profile + 1, bt_batt_level, iton_bt_get_ack_enabled(), keychron_bt_is_initialized());
    }
#endif
    housekeeping_task_user();
}

bool via_command_kb(uint8_t *data, uint8_t length) {
    if (length != 32 || data[0] != 0xfe || data[1] != 'K' || data[2] != '4' || data[3] != 1) {
        return false;
    }
    uint8_t operation = data[5], argument = data[6], result = 0;
    if (operation > BT_REMOTE_BATTERY || (operation == BT_REMOTE_ACKS && argument > 1) ||
        ((operation == BT_REMOTE_PROFILE || operation == BT_REMOTE_PAIR) && (argument < 1 || argument > 3))) {
        result = 2; // invalid operation/argument
    } else if (operation == BT_REMOTE_ACKS) {
        iton_bt_set_ack_enabled(argument);
        BT_LOG("[usbctl] ACKs=%u\n", iton_bt_get_ack_enabled());
    } else if (operation != BT_REMOTE_STATUS) {
        if (!bt_mode) {
            result = 3; // physical selector is in cable mode
        } else if (remote_pending) {
            result = 4; // another request is queued
        } else {
            remote_pending = operation;
            remote_argument = argument;
            result = 1; // accepted, execute in housekeeping
        }
    }
    memset(&data[6], 0, length - 6);
    data[6] = result;
    data[7] = remote_pending;
    data[8] = bt_mode;
    data[9] = keychron_bt_is_initialized();
    data[10] = iton_bt_is_connected;
    data[11] = iton_bt_get_ack_enabled();
    data[12] = bt_profile + 1;
    data[13] = gpio_read_pin(D5);
    data[14] = gpio_read_pin(D6);
    data[15] = connection_get_host();
    if (keychron_bt_is_initialized()) {
        data[16] = SPID0.state;
        data[17] = gpio_read_pin(A0);
        data[18] = gpio_read_pin(A1);
        data[19] = SPID0.idx > 255 ? 255 : SPID0.idx;
    }
    iton_bt_get_diagnostics(&data[20]);
    data[30] = 4; // diagnostic build revision
    // QMK routes raw_hid_send() to the selected keyboard transport. Send
    // this USB management reply explicitly over USB even while typing to BT.
    host_driver_t *usb = host_get_driver();
    if (usb && usb->send_raw_hid) usb->send_raw_hid(data, length);
    return true;
}

static void k4v2_remote_task(void) {
    if (!remote_pending) return;
    uint8_t operation = remote_pending, argument = remote_argument;
    remote_pending = 0;
    if (!bt_mode) return; // switch may have moved after request acceptance
    bt_mode_sent = true;
    bt_held_keycode = 0;
    BT_LOG("[usbctl] operation=%u argument=%u\n", operation, argument);
    if (operation == BT_REMOTE_RESTART) {
        bt_pairing = false;
        keychron_bt_deinit();
        keychron_bt_init();
        k4v2_wake();
        bt_led_state = 0;
        bt_batt_due = true;
    } else if (operation == BT_REMOTE_WAKE) {
        bt_pairing = false;
        k4v2_wake();
    } else if (operation == BT_REMOTE_PROFILE || operation == BT_REMOTE_PAIR) {
        k4v2_select_profile(argument - 1);
        if (operation == BT_REMOTE_PAIR) {
            bt_pair_requested = true;
            k4v2_pair();
        }
    } else if (operation == BT_REMOTE_BATTERY) {
        iton_bt_query_battery_level();
    }
}

// Notification callbacks run in interrupt context: defer module sends to housekeeping.
void iton_bt_entered_pairing(void) {
    bt_pairing = true;
    bt_pairing_since = timer_read32();
    bt_led_state = 1;
    bt_led_timer = timer_read();
}
void iton_bt_connection_successful(void) {
    bt_pairing = false;
    bt_reconnect_due = false;
    bt_led_state = 2;
    bt_led_timer = timer_read();
    bt_batt_due = true;
}
void iton_bt_disconnected(void) {
    if (!bt_pairing) bt_led_state = 0;
    bt_reconnect_due = true;
}
void iton_bt_battery_level(uint8_t level) { bt_batt_level = level; }
void iton_bt_battery_voltage_low(void) { bt_batt_level = batt_below_30; }
void iton_bt_battery_low_power_shutdown(void) { bt_batt_level = batt_below_30; }
void iton_bt_battery_exit_low_battery_mode(void) { bt_batt_due = true; }

bool rgb_matrix_indicators_kb(void) {
    if (!rgb_matrix_indicators_user()) {
        return false;
    }
    if (!bt_mode) {
        return true;
    }
    if (bt_led_state) {
        if (bt_led_state == 2 && timer_elapsed(bt_led_timer) >= BT_LED_CONNECTED_MS) {
            bt_led_state = 0;
            return true;
        }
        rgb_matrix_set_color_all(0, 0, 0);
        if (bt_led_state == 2 || timer_elapsed(bt_led_timer) % 500 < 250) {
            rgb_matrix_set_color(BT_LED_BASE_INDEX + bt_profile, 0, 0, 255);
        }
        return true;
    }
    if (bt_batt_show) {
        uint16_t elapsed = timer_elapsed(bt_batt_show_timer);
        if (elapsed < BT_BATT_SHOW_MS) {
            uint8_t leds = 10, red = 0, green = 0, blue = 0;
            switch (bt_batt_level) {
                case batt_below_30: leds = 3; red = 180; break;
                case batt_between_30_70: leds = 7; red = 180; green = 72; break;
                case batt_above_70: green = 180; break;
                default: blue = 80; break;
            }
            rgb_matrix_set_color_all(1, 1, 1);
            for (uint8_t i = 0; i < 10; i++) {
                bool lit = i < leds && i < elapsed / 120 + 1;
                rgb_matrix_set_color(BT_LED_BASE_INDEX + i, lit ? red : 0, lit ? green : 0, lit ? blue : 0);
            }
            return true;
        }
        bt_batt_show = false;
    }
    if (bt_batt_level == batt_below_30 && timer_read() % 4000 < 600) {
        rgb_matrix_set_color(BT_LED_BASE_INDEX + bt_profile, 255, 0, 0);
    }
    return true;
}
