// Copyright 2021 1Conan (@1Conan)
// SPDX-License-Identifier: GPL-2.0-or-later

/**
 * SPI driver settings
 */

#ifdef BLUETOOTH_ITON_BT
#    define SN32_HAS_SPI0 TRUE
// needs to be removed after pr is merged https://github.com/SonixQMK/ChibiOS-Contrib/pull/52
#    define SN32_SPI_USE_SPI0 TRUE
#endif

#include_next <mcuconf.h>
