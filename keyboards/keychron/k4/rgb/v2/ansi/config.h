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
#define RGB_MATRIX_LED_COUNT 100


#define ITON_BT_ENABLE_ACK
#define ITON_BT_IRQ_LINE A0