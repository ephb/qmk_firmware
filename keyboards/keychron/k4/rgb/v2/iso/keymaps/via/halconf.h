// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#define HAL_USE_SPI TRUE
#define PAL_USE_CALLBACKS TRUE
#define SPI_SELECT_MODE SPI_SELECT_MODE_LLD
#include_next <halconf.h>
