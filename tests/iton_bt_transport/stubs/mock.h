// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MCU_SN32
#define PAL_USE_CALLBACKS 1
#define ITON_BT_RUNTIME_ACK
#define ITON_BT_ENABLE_LED_STATE
#define BLUETOOTH_ITON_BT
#define A0 0
#define A1 1
#define SPI_READY 1
#define SPI_ACTIVE 2
#define SPI_DATA_LENGTH(n) (n)
#define PAL_EVENT_MODE_BOTH_EDGES 0
#define TIME_US2I(n) (n)

typedef unsigned systime_t;
typedef struct {
    struct { bool BUSY, TX_FULL, RX_EMPTY; } STAT_b;
    struct { bool FRESET; } CTRL0_b;
    struct { bool RXFIFOTHIC; } IC_b;
    uint8_t DATA;
    uint32_t DFDLY;
} FakeSPI;
typedef struct SPIDriver {
    int state;
    size_t idx, count;
    FakeSPI *spi;
    const uint8_t *txbuf;
    uint8_t *rxbuf;
} SPIDriver;
typedef struct {
    bool slave;
    void (*data_cb)(SPIDriver *);
    unsigned ctrl0, clkdiv;
} SPIConfig;
typedef struct { uint8_t mods, reserved, keys[6]; } report_keyboard_t;
typedef struct { uint8_t report_id, mods, bits[30]; } report_nkro_t;
typedef struct { uint8_t buttons; } report_mouse_t;

static FakeSPI hardware = {.STAT_b = {.RX_EMPTY = true}};
static SPIDriver SPID0 = {.state = SPI_READY, .spi = &hardware};
static bool pins[2];
static unsigned ticks;
static uint8_t sent[16];
static size_t sent_len;

static inline void chSysLock(void) {}
static inline void chSysUnlock(void) {}
static inline void chSysLockFromISR(void) {}
static inline void chSysUnlockFromISR(void) {}
static inline unsigned chVTGetSystemTimeX(void) { return ticks++; }
static inline unsigned chVTGetSystemTime(void) { return ticks += 10000; }
static inline uint16_t timer_read(void) { return ticks++; }
static inline uint16_t timer_elapsed(uint16_t t) { return (uint16_t)(ticks++ - t); }
static inline void wait_us(unsigned n) { ticks += n; }
static inline bool gpio_read_pin(unsigned p) { return pins[p]; }
static inline void gpio_write_pin_low(unsigned p) { pins[p] = false; }
static inline void gpio_write_pin_high(unsigned p) { pins[p] = true; }
static inline void gpio_set_pin_output(unsigned p) { (void)p; }
static inline void gpio_set_pin_input(unsigned p) { (void)p; }
static inline void palSetLineCallback(unsigned p, void (*cb)(void *), void *arg) { (void)p; (void)cb; (void)arg; }
static inline void palEnableLineEvent(unsigned p, unsigned mode) { (void)p; (void)mode; }
static inline void palDisableLineEvent(unsigned p) { (void)p; }
static inline void spiStart(SPIDriver *s, const SPIConfig *c) { (void)c; s->state = SPI_READY; }
static inline void spiStop(SPIDriver *s) { s->state = 0; }
static inline void spiStopTransferI(SPIDriver *s, void *n) { (void)n; s->state = SPI_READY; }
static inline void spiStopTransfer(SPIDriver *s, void *n) { spiStopTransferI(s, n); }
static inline void spiStartReceiveI(SPIDriver *s, size_t n, void *b) {
    s->rxbuf = b; s->count = n; s->idx = 0; s->state = SPI_ACTIVE;
}
static inline void spiStartExchangeI(SPIDriver *s, size_t n, const void *tx, void *rx) {
    s->txbuf = tx; s->rxbuf = rx; s->count = n; s->idx = 0; s->state = SPI_ACTIVE;
    assert(n <= sizeof(sent));
    memcpy(sent, tx, n); sent_len = n;
}
