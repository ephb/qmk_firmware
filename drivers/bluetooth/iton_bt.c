// Copyright 2024 1Conan (@1Conan) <me@1conan.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string.h>
#include <hal.h>
#include "gpio.h"
#include "config.h"
#include "timer.h"
#include "wait.h"
#include "iton_bt.h"

#ifdef ITON_BT_DEBUG
#    include "print.h"

// Interrupt handlers only enqueue/copy bytes: USB printing can block.
typedef struct {
    char kind;
    uint8_t len;
    uint8_t bytes[12];
} iton_bt_debug_event_t;
static iton_bt_debug_event_t debug_events[8];
static volatile uint8_t debug_head, debug_tail;
static volatile uint16_t debug_sends, debug_done, debug_timeouts;
static volatile uint16_t debug_int_hi, debug_int_lo, debug_aborted, debug_dropped;
static volatile uint16_t debug_fifo_drained;

// Called with the ChibiOS system lock held, in either thread or ISR context.
static void iton_bt_debug_event(char kind, const uint8_t *bytes, size_t len) {
    uint8_t next = (debug_head + 1) % 8;
    if (next == debug_tail) {
        debug_dropped++;
        return;
    }
    iton_bt_debug_event_t *event = &debug_events[debug_head];
    event->kind = kind;
    event->len = len < sizeof(event->bytes) ? len : sizeof(event->bytes);
    memcpy(event->bytes, bytes, event->len);
    debug_head = next;
}
#endif

#ifndef ITON_BT_SPI_PORT
#    define ITON_BT_SPI_PORT SPID0
#endif

#ifndef ITON_BT_IRQ_LINE
#    define ITON_BT_IRQ_LINE A0
#endif

#ifndef ITON_BT_INT_LINE
#    define ITON_BT_INT_LINE A1
#endif

#ifndef ITON_BT_BUFFER_LEN
#    define ITON_BT_BUFFER_LEN 16
#endif

/**
 * Driver Macros
 */
#define HIGH_BITS(x) ((uint8_t)(x >> 8))
#define LOW_BITS(x) ((uint8_t)(x & 0x00FF))

/**
 * Function definitions
 */
void iton_bt_data_cb(SPIDriver *spip);
static void iton_bt_preload_tx(size_t frame_len);

// SN32's FIFO reset is asynchronous. Do not arm the next transfer until
// reset has released, or its first staged byte may be flushed.
static void iton_bt_stop_transfer_i(void) {
    spiStopTransferI(&ITON_BT_SPI_PORT, NULL);
#if defined(MCU_SN32)
    for (uint16_t w = 0; ITON_BT_SPI_PORT.spi->CTRL0_b.FRESET && w < 1000; w++) {}
    ITON_BT_SPI_PORT.spi->IC_b.RXFIFOTHIC = true;
#endif
}

/**
 * Callbacks
 */
__attribute__((weak)) void iton_bt_battery_voltage_low(void) {}
__attribute__((weak)) void iton_bt_battery_exit_low_battery_mode(void) {}
__attribute__((weak)) void iton_bt_battery_low_power_shutdown(void) {}
__attribute__((weak)) void iton_bt_battery_level(uint8_t level) {}

__attribute__((weak)) void iton_bt_connection_successful(void) {}
__attribute__((weak)) void iton_bt_entered_pairing(void) {}
__attribute__((weak)) void iton_bt_disconnected(void) {}
__attribute__((weak)) void iton_bt_enters_connection_state(void) {}

/**
 * Driver variables
 */
volatile bool iton_bt_is_connected = false;
#ifdef ITON_BT_ENABLE_LED_STATE
volatile uint8_t iton_bt_led_state = 0x00;
#endif

static uint8_t iton_bt_buffer[ITON_BT_BUFFER_LEN];
// Framed SPI transport adapted from SonixQMK/qmk_firmware PR #485.
// Keep a listening window armed for unsolicited module notifications.
static uint8_t iton_bt_rx_buffer[96];
static volatile bool iton_bt_tx_active = false;
static volatile bool iton_bt_tx_pending = false;
static uint8_t iton_bt_last_tx_len = 0;
static volatile systime_t iton_bt_last_bus_time = 0;
uint8_t        iton_bt_send_kb_last_key = 0x00;

#ifdef ITON_BT_ENABLE_ACK
static volatile bool iton_bt_ack_enabled = true;
#else
static volatile bool iton_bt_ack_enabled = false;
#endif
#if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
// Notification callbacks run in an ISR. Defer ACK transmission until the
// receive handler has finished and rearmed SPI, rather than replacing its
// active transfer from inside the callback.
static uint8_t ack_queue[4][2];
static volatile uint8_t ack_head, ack_tail;
#endif

bool iton_bt_get_ack_enabled(void) {
    return iton_bt_ack_enabled;
}

void iton_bt_get_diagnostics(uint8_t data[10]) {
    memset(data, 0, 10);
#ifdef ITON_BT_DEBUG
    chSysLock();
    uint16_t values[] = {debug_sends, debug_done, debug_timeouts, debug_int_hi, debug_fifo_drained};
    chSysUnlock();
    for (uint8_t i = 0; i < 5; i++) {
        data[2 * i] = values[i] & 0xff;
        data[2 * i + 1] = values[i] >> 8;
    }
#endif
}

void iton_bt_set_ack_enabled(bool enabled) {
#if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
    chSysLock();
    iton_bt_ack_enabled = enabled;
    ack_tail = ack_head;
    chSysUnlock();
#else
    (void)enabled;
#endif
}

void iton_bt_task(void) {
#if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
    uint8_t b1, b2;
    chSysLock();
    if (!iton_bt_ack_enabled || ack_tail == ack_head) {
        chSysUnlock();
        return;
    }
    b1 = ack_queue[ack_tail][0];
    b2 = ack_queue[ack_tail][1];
    ack_tail = (ack_tail + 1) % 4;
    chSysUnlock();
    iton_bt_send2(control, b1, b2);
#endif
}

const SPIConfig iton_bt_spicfg = {
    .slave   = true,
    .data_cb = iton_bt_data_cb,
#if defined(MCU_SN32)
    .ctrl0 = SPI_DATA_LENGTH(8),
    // Disable the SN32 high-speed prefetch delay in slave mode.
    .clkdiv = 0xff,
#endif
};

/**
 * Callbacks
 */
#if PAL_USE_CALLBACKS || PAL_USE_WAIT
static inline void iton_bt_rx_battery_notif(uint8_t data) {
    switch (data) {
        case batt_voltage_low:
            iton_bt_battery_voltage_low();
            break;
        case batt_exit_low_battery_mode:
            iton_bt_battery_exit_low_battery_mode();
            break;
        case batt_low_power_shutdown:
            iton_bt_battery_low_power_shutdown();
            break;
        case batt_above_70:
        case batt_between_30_70:
        case batt_below_30:
            iton_bt_battery_level(data);
            break;
        case batt_wake_mcu:
#    if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
            if (iton_bt_ack_enabled) iton_bt_send_ack(control_bt, wake_ack);
#    endif
            break;
        case batt_unknown:
#    if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
            if (iton_bt_ack_enabled) iton_bt_send_ack(control_bt, unknown_ack);
#    endif
            break;
        case query_working_mode:
            break;
        case query_bt_name:
            break;
    }
}

static inline void iton_bt_rx_bluetooth_notif(uint8_t data) {
    switch (data) {
        case bt_connection_success:
            iton_bt_is_connected = true;

#    if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
            if (iton_bt_ack_enabled) iton_bt_send_ack(control_bt, connect_ack);
#    endif

            iton_bt_connection_successful();
            break;
        case bt_entered_pairing:
            iton_bt_entered_pairing();
            break;
        case bt_disconected:
            iton_bt_is_connected = false;

#    if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
            if (iton_bt_ack_enabled) iton_bt_send_ack(control_bt, disconnect_ack);
#    endif

            iton_bt_disconnected();
            break;
        case bt_enters_connection:
            iton_bt_enters_connection_state();
            break;
    }
}

static void iton_bt_rx_cb(void *arg) {
    if (gpio_read_pin(ITON_BT_INT_LINE)) {
#ifdef ITON_BT_DEBUG
        debug_int_hi++;
#endif
        chSysLockFromISR();
        if (iton_bt_tx_active) {
#ifdef ITON_BT_DEBUG
            debug_aborted++;
#endif
            iton_bt_stop_transfer_i();
            iton_bt_tx_active = false;
            iton_bt_tx_pending = true;
            gpio_write_pin_low(ITON_BT_IRQ_LINE);
        }
        if (ITON_BT_SPI_PORT.state == SPI_READY) {
            spiStartReceiveI(&ITON_BT_SPI_PORT, sizeof(iton_bt_rx_buffer), iton_bt_rx_buffer);
        }
        // Preserve bytes captured before this INT callback. Starting at idx
        // dropped the B6/B1 header when SPI won the interrupt race.
        chSysUnlockFromISR();
    } else {
#ifdef ITON_BT_DEBUG
        debug_int_lo++;
#endif
        chSysLockFromISR();
#if defined(MCU_SN32)
        // The GPIO falling-edge IRQ may run before the final SPI RX IRQ.
        // Capture queued bytes BEFORE spiStopTransferI resets the FIFOs.
        // Bound the loop so a bad peripheral cannot trap us in this ISR.
        for (uint8_t n = 0; n < 16 && !ITON_BT_SPI_PORT.spi->STAT_b.RX_EMPTY; n++) {
            uint8_t byte = ITON_BT_SPI_PORT.spi->DATA;
            if (ITON_BT_SPI_PORT.idx < sizeof(iton_bt_rx_buffer)) {
                iton_bt_rx_buffer[ITON_BT_SPI_PORT.idx] = byte;
            }
            ITON_BT_SPI_PORT.idx++;
#ifdef ITON_BT_DEBUG
            debug_fifo_drained++;
#endif
        }
        ITON_BT_SPI_PORT.spi->IC_b.RXFIFOTHIC = true;
#endif
        size_t end = ITON_BT_SPI_PORT.idx;
        iton_bt_stop_transfer_i();
        chSysUnlockFromISR();

#    if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
        // hack to make sure irq is low since acks messes with stuff
        if (iton_bt_ack_enabled) gpio_write_pin_low(ITON_BT_IRQ_LINE);
#    endif

        if (end > sizeof(iton_bt_rx_buffer)) {
            end = sizeof(iton_bt_rx_buffer);
        }
        size_t start = 0;
        // The always-armed listener can also receive zero padding clocked
        // after a command completes. Locate a complete real notification
        // header; do not interpret a headerless "51 76" as a connection.
        while (start < end) {
            if (iton_bt_rx_buffer[start] == led_state && end - start >= 2) break;
            if (iton_bt_rx_buffer[start] == notification && end - start >= 3 &&
                (iton_bt_rx_buffer[start + 1] == notif_battery || iton_bt_rx_buffer[start + 1] == notif_bluetooth)) break;
            start++;
        }
        uint8_t len = end - start;
        uint8_t *frame = &iton_bt_rx_buffer[start];

#ifdef ITON_BT_DEBUG
        chSysLockFromISR();
        if (len) {
            iton_bt_debug_event('R', frame, len);
        } else if (end) {
            iton_bt_debug_event('X', iton_bt_rx_buffer, end);
        }
        chSysUnlockFromISR();
#endif

        switch (len ? frame[0] : 0) {
            case led_state:
#    ifdef ITON_BT_ENABLE_LED_STATE
                if (len >= 2) {
                    iton_bt_led_state = frame[1];
                }
#    endif
                break;
            case notification:
                if (len >= 3) {
                    switch (frame[1]) {
                        case notif_battery:
                            iton_bt_rx_battery_notif(frame[2]);
                            break;
                        case notif_bluetooth:
                            iton_bt_rx_bluetooth_notif(frame[2]);
                            break;
                    }
                }
                break;
        }

        chSysLockFromISR();
        iton_bt_stop_transfer_i();
        if (iton_bt_tx_pending && iton_bt_last_tx_len) {
            iton_bt_tx_pending = false;
            iton_bt_tx_active = true;
            spiStartExchangeI(&ITON_BT_SPI_PORT, iton_bt_last_tx_len, iton_bt_buffer, iton_bt_rx_buffer);
            iton_bt_preload_tx(iton_bt_last_tx_len);
            gpio_write_pin_high(ITON_BT_IRQ_LINE);
        } else {
            spiStartReceiveI(&ITON_BT_SPI_PORT, sizeof(iton_bt_rx_buffer), iton_bt_rx_buffer);
        }
        chSysUnlockFromISR();
    }
}
#endif

void iton_bt_data_cb(SPIDriver *spip) {
    chSysLockFromISR();
    if (iton_bt_tx_active) {
#ifdef ITON_BT_DEBUG
        debug_done++;
        // Capture the response BEFORE rearming/clearing the exchange buffer.
        // 'C' means SPI completion, not confirmation the command was accepted.
        if (iton_bt_buffer[0] == control) {
            iton_bt_debug_event('C', iton_bt_rx_buffer, spip->idx);
        }
#endif
        iton_bt_last_bus_time = chVTGetSystemTimeX();
        iton_bt_tx_active = false;
        gpio_write_pin_low(ITON_BT_IRQ_LINE);
    }
    // In this checkout __spi_isr_complete_code sets SPI_READY *before*
    // invoking data_cb. Testing for SPI_COMPLETE left the RX window dead
    // after every completed command, losing unsolicited notifications.
    if (spip->state == SPI_READY) {
        spiStartReceiveI(spip, sizeof(iton_bt_rx_buffer), iton_bt_rx_buffer);
    }
    chSysUnlockFromISR();
}

/**
 * Driver Functions
 */
void iton_bt_init(void) {
    iton_bt_tx_active = false;
    iton_bt_tx_pending = false;
    iton_bt_last_tx_len = 0;
    iton_bt_send_kb_last_key = 0;
    iton_bt_is_connected = false;
    memset(iton_bt_rx_buffer, 0, sizeof(iton_bt_rx_buffer));
    gpio_set_pin_output(ITON_BT_IRQ_LINE);
    gpio_set_pin_input(ITON_BT_INT_LINE);

    gpio_write_pin_low(ITON_BT_IRQ_LINE);

#if PAL_USE_CALLBACKS || PAL_USE_WAIT
    palSetLineCallback(ITON_BT_INT_LINE, iton_bt_rx_cb, NULL);
    palEnableLineEvent(ITON_BT_INT_LINE, PAL_EVENT_MODE_BOTH_EDGES);
#endif

    spiStart(&ITON_BT_SPI_PORT, &iton_bt_spicfg);
#if defined(MCU_SN32)
    // The SN32 LLD only sets DFDLY for fast clocks; it does not clear a
    // previous setting when clkdiv changes. Enforce the slave setting.
    ITON_BT_SPI_PORT.spi->DFDLY = 0;
#endif
    chSysLock();
    spiStartReceiveI(&ITON_BT_SPI_PORT, sizeof(iton_bt_rx_buffer), iton_bt_rx_buffer);
    chSysUnlock();
}

#ifndef ITON_BT_IRQ_WAIT_TIMEOUT_MS
#    define ITON_BT_IRQ_WAIT_TIMEOUT_MS 50
#endif
#ifndef ITON_BT_TX_QUIET_US
#    define ITON_BT_TX_QUIET_US 10000
#endif
#ifndef ITON_BT_TX_QUIET_TIMEOUT_MS
#    define ITON_BT_TX_QUIET_TIMEOUT_MS 30
#endif

static void iton_bt_wait_bus_idle(void) {
    uint16_t start = timer_read();
    while ((systime_t)(chVTGetSystemTime() - iton_bt_last_bus_time) < TIME_US2I(ITON_BT_TX_QUIET_US)) {
        if (timer_elapsed(start) > ITON_BT_TX_QUIET_TIMEOUT_MS) {
            return;
        }
        wait_us(100);
    }
}

static void iton_bt_fifo_reset(void) {
    uint16_t start = timer_read();
    while (ITON_BT_SPI_PORT.spi->STAT_b.BUSY) {
        if (timer_elapsed(start) > 1) {
            break;
        }
    }
    spiStopTransfer(&ITON_BT_SPI_PORT, NULL);
    start = timer_read();
    while (ITON_BT_SPI_PORT.spi->CTRL0_b.FRESET) {
        if (timer_elapsed(start) > 1) {
            break;
        }
    }
}

static void iton_bt_preload_tx(size_t frame_len) {
    size_t limit = frame_len < ITON_BT_BUFFER_LEN ? ITON_BT_BUFFER_LEN - frame_len + 1 : 1;
    size_t pre = 1;
    while (pre < frame_len && pre < limit && !ITON_BT_SPI_PORT.spi->STAT_b.TX_FULL) {
        ITON_BT_SPI_PORT.spi->DATA = iton_bt_buffer[pre++];
    }
    ITON_BT_SPI_PORT.idx = pre - 1;
    ITON_BT_SPI_PORT.count = frame_len + pre - 1;
}

static bool iton_bt_wait_irq_clear(void) {
    uint16_t start = timer_read();
    while (gpio_read_pin(ITON_BT_IRQ_LINE)) {
        if (timer_elapsed(start) > ITON_BT_IRQ_WAIT_TIMEOUT_MS) {
            gpio_write_pin_low(ITON_BT_IRQ_LINE);
            iton_bt_tx_active = false;
            spiStopTransfer(&ITON_BT_SPI_PORT, NULL);
#ifdef ITON_BT_DEBUG
            debug_timeouts++;
#endif
            return false;
        }
    }
    return true;
}

void iton_bt_deinit(void) {
    // Let the final mode_usb command finish, with the same bounded wait as TX.
    iton_bt_wait_irq_clear();
    chSysLock();
#if PAL_USE_CALLBACKS || PAL_USE_WAIT
    palDisableLineEvent(ITON_BT_INT_LINE);
    palSetLineCallback(ITON_BT_INT_LINE, NULL, NULL);
#endif
    iton_bt_stop_transfer_i();
    chSysUnlock();
    spiStop(&ITON_BT_SPI_PORT);
    gpio_set_pin_input(ITON_BT_IRQ_LINE);
    gpio_set_pin_input(ITON_BT_INT_LINE);
    iton_bt_tx_active = false;
    iton_bt_tx_pending = false;
    iton_bt_last_tx_len = 0;
    iton_bt_is_connected = false;
#ifdef ITON_BT_ENABLE_LED_STATE
    iton_bt_led_state = 0;
#endif
#if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
    ack_tail = ack_head;
#endif
}

void iton_bt_send(uint8_t cmd, uint8_t *data, uint8_t len) {
    if (len >= ITON_BT_BUFFER_LEN) {
        return;
    }
    if (!iton_bt_wait_irq_clear()) {
        return;
    }
    iton_bt_wait_bus_idle();
    iton_bt_fifo_reset();
    // Buffer preparation and arming are atomic with respect to RX callbacks.
    chSysLock();
    iton_bt_stop_transfer_i();
    memset(iton_bt_rx_buffer, 0, sizeof(iton_bt_rx_buffer));
    iton_bt_buffer[0] = cmd;
    memcpy(&iton_bt_buffer[1], data, len);
    memset(&iton_bt_buffer[len + 1], 0, ITON_BT_BUFFER_LEN - len - 1);
    iton_bt_last_tx_len = len + 1;
    iton_bt_tx_pending = false;
    iton_bt_tx_active = true;
    spiStartExchangeI(&ITON_BT_SPI_PORT, len + 1, iton_bt_buffer, iton_bt_rx_buffer);
    iton_bt_preload_tx(len + 1);
#ifdef ITON_BT_DEBUG
    debug_sends++;
    if (cmd == control) {
        iton_bt_debug_event('T', iton_bt_buffer, len + 1);
    } else if (cmd == report_hid) {
        iton_bt_debug_event('K', iton_bt_buffer, len + 1);
    }
#endif
    gpio_write_pin_high(ITON_BT_IRQ_LINE);
    chSysUnlock();
}

void iton_bt_send2(uint8_t cmd, uint8_t b1, uint8_t b2) {
    uint8_t data[] = {b1, b2};
    iton_bt_send(cmd, data, sizeof(data));
}

void iton_bt_send_ack(uint8_t b1, uint8_t b2) {
#if defined(ITON_BT_ENABLE_ACK) || defined(ITON_BT_RUNTIME_ACK)
    chSysLockFromISR();
    uint8_t next = (ack_head + 1) % 4;
    if (next != ack_tail) {
        ack_queue[ack_head][0] = b1;
        ack_queue[ack_head][1] = b2;
        ack_head = next;
    }
    chSysUnlockFromISR();
#else
    (void)b1;
    (void)b2;
#endif
}

void iton_bt_send_fn(bool pressed) {
    uint8_t data = pressed ? 0x03 : 0x00;

    iton_bt_send(report_fn, &data, 1);
}

void iton_bt_send_system(uint16_t data) {
    iton_bt_send(report_system, (uint8_t *)&data, 1);
}

void iton_bt_send_consumer(uint16_t data) {
    iton_bt_send2(report_consumer, HIGH_BITS(data), LOW_BITS(data));
}

void iton_bt_send_keyboard(report_keyboard_t *report) {
    // iton module only reads 5 of the keys in the hid report
    // so we send the last key as an nkro report.
    if (report->keys[5] != iton_bt_send_kb_last_key) {
        uint8_t nkro_report[15] = {0};
        // This module's A2 bitmap covers only usages 0..119. International
        // and other extended keyboard usages must not index beyond it.
        if (report->keys[5] && report->keys[5] < sizeof(nkro_report) * 8) {
            nkro_report[report->keys[5] >> 3] |= (1 << (report->keys[5] & 7));
        }
        iton_bt_send_kb_last_key = report->keys[5];

        iton_bt_send(report_nkro, &nkro_report[0], 15);
    }

    iton_bt_send(report_hid, &report->mods, 8);
}

#ifdef ITON_BT_DEBUG
void iton_bt_debug_task(void) {
    static uint16_t last_drain, last_status;
    if (timer_elapsed(last_drain) >= 100) {
        last_drain = timer_read();
        // Bound logging work to avoid starving keyboard housekeeping.
        for (uint8_t i = 0; i < 2; i++) {
            iton_bt_debug_event_t event;
            chSysLock();
            if (debug_tail == debug_head) {
                chSysUnlock();
                break;
            }
            event = debug_events[debug_tail];
            debug_tail = (debug_tail + 1) % 8;
            chSysUnlock();
            uprintf("[iton] %c len=%u:", event.kind, event.len);
            for (uint8_t j = 0; j < event.len; j++) {
                uprintf(" %02X", event.bytes[j]);
            }
            uprintf("\n");
            if (event.kind == 'R' && event.len >= 3 && event.bytes[0] == notification && event.bytes[1] == notif_bluetooth) {
                switch (event.bytes[2]) {
                    case bt_entered_pairing: uprintf("[iton] MODULE CONFIRMED PAIRING\n"); break;
                    case bt_connection_success: uprintf("[iton] MODULE CONNECTED\n"); break;
                    case bt_disconected: uprintf("[iton] MODULE DISCONNECTED\n"); break;
                }
            }
        }
    }
    if (timer_elapsed(last_status) >= 1000) {
        last_status = timer_read();
        uint16_t sends, done, timeouts, hi, lo, aborted, dropped, drained;
        chSysLock();
        sends = debug_sends; done = debug_done; timeouts = debug_timeouts;
        hi = debug_int_hi; lo = debug_int_lo; aborted = debug_aborted; dropped = debug_dropped;
        drained = debug_fifo_drained;
        chSysUnlock();
        uprintf("[iton] tx=%u done=%u timeout=%u int_hi=%u int_lo=%u abort=%u dropped=%u fifo=%u state=%u idx=%u IRQ=%u INT=%u\n",
                sends, done, timeouts, hi, lo, aborted, dropped,
                drained, (unsigned)ITON_BT_SPI_PORT.state, (unsigned)ITON_BT_SPI_PORT.idx,
                (unsigned)gpio_read_pin(ITON_BT_IRQ_LINE), (unsigned)gpio_read_pin(ITON_BT_INT_LINE));
    }
}
#endif
