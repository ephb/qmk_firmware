/* Copyright 2021 Dimitris Mantzouranis
 * Copyright 2023 PythonDeployer (@PythonDeployer) & ephb
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Based on SonixQMK/qmk_userspace_via's K4v2 ISO VIA keymap and
 * SonixQMK/qmk_firmware PR #486 (Windows/Fn/Mac layers and Bluetooth).
 * Raw matrices preserve the PCB positions used by via_json/k4_iso.json.
 */
#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [K4V2_LAYER_WIN_BASE] = {
        {KC_ESC, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7, KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_DEL, KC_HOME, KC_END, KC_PGUP, KC_PGDN, RM_NEXT},
        {KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_NO, KC_NUM, KC_PSLS, KC_PAST, KC_PMNS},
        {KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_NO, KC_NO, KC_P7, KC_P8, KC_P9, KC_PPLS},
        {KC_CAPS, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_NUHS, KC_ENT, KC_NO, KC_P4, KC_P5, KC_P6, KC_NO},
        {KC_LSFT, KC_NUBS, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_NO, KC_RSFT, KC_UP, KC_P1, KC_P2, KC_P3, KC_PENT},
        {KC_LCTL, KC_LGUI, KC_LALT, KC_NO, KC_NO, KC_NO, KC_SPC, KC_NO, KC_NO, KC_NO, KC_RALT, MO(K4V2_LAYER_FN), KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT, KC_P0, KC_PDOT, KC_NO}
    },
    [K4V2_LAYER_FN] = {
        {QK_BOOT, KC_SLCT, KC_PAUS, KC_APP, _______, RM_VALD, RM_VALU, KC_MPRV, KC_MPLY, KC_MNXT, KC_MUTE, KC_VOLD, KC_VOLU, _______, KC_PSCR, _______, _______, KC_SLEP, RM_TOGG},
        {_______, BT_PRF1, BT_PRF2, BT_PRF3, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_NO, _______, _______, _______, RM_HUEU},
        {_______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_NO, _______, _______, _______, RM_SPDU},
        {_______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, _______, KC_NO, _______, _______, _______, KC_NO},
        {_______, _______, _______, _______, _______, _______, KC_BT_BAT, _______, _______, _______, _______, _______, KC_NO, _______, _______, _______, _______, _______, RM_SATU},
        {_______, KC_LALT, KC_LGUI, KC_NO, KC_NO, KC_NO, _______, KC_NO, KC_NO, KC_NO, _______, MO(K4V2_LAYER_FN), _______, _______, _______, _______, _______, _______, KC_NO}
    },
    [K4V2_LAYER_MAC_BASE] = {
        {KC_ESC, KC_BRID, KC_BRIU, KC_MCTL, KC_LPAD, RM_VALD, RM_VALU, KC_MPRV, KC_MPLY, KC_MNXT, KC_MUTE, KC_VOLD, KC_VOLU, KC_DEL, KC_HOME, KC_END, KC_PGUP, KC_PGDN, RM_NEXT},
        {KC_GRV, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7, KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_NO, KC_NUM, KC_PSLS, KC_PAST, KC_PMNS},
        {KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U, KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_NO, KC_NO, KC_P7, KC_P8, KC_P9, KC_PPLS},
        {KC_CAPS, KC_A, KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_SCLN, KC_QUOT, KC_NUHS, KC_ENT, KC_NO, KC_P4, KC_P5, KC_P6, KC_NO},
        {KC_LSFT, KC_NUBS, KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_NO, KC_RSFT, KC_UP, KC_P1, KC_P2, KC_P3, KC_PENT},
        {KC_LCTL, KC_LOPT, KC_LCMD, KC_NO, KC_NO, KC_NO, KC_SPC, KC_NO, KC_NO, KC_NO, KC_RCMD, MO(K4V2_LAYER_FN), KC_RCTL, KC_LEFT, KC_DOWN, KC_RGHT, KC_P0, KC_PDOT, KC_NO}
    }
};
