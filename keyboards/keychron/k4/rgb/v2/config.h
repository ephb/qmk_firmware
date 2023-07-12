// Copyright 2023 PythonDeployer (@PythonDeployer)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/*
 * Feature disable options
 *  These options are also useful to firmware size reduction.
 */


/* LED Status indicators */

#include <config_led.h>


#define DIP_SWITCH_PINS { D5, D6 }
#define LED_CAPS_LOCK_PIN B12
#define LED_NUM_LOCK_PIN  D4
#define LED_PIN_ON_STATE  1
#define RGB_MATRIX_LED_COUNT 100

#define MATRIX_ROWS 6
#define MATRIX_COLS 19

