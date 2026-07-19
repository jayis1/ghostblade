/*
 * main.c — GhostBlade RP2350B Firmware Entry Point
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * This is the top-level entry point for the RP2350B coprocessor firmware.
 * It orchestrates the boot sequence:
 *
 *   1. Hardware initialization (clocks, GPIO, SPI, ADC, PIO)
 *   2. Watchdog configuration
 *   3. SPI protocol handler initialization
 *   4. Battery monitor startup
 *   5. SDR DMA ring buffer initialization
 *   6. CC1101 sub-GHz radio initialization
 *   7. ST25R3916 NFC controller initialization
 *   8. Core 1 launch (SDR IQ DMA engine)
 *   9. Main loop: process SPI commands, update telemetry, kick watchdog
 *
 * Build target: RP2350B (ARM Cortex-M33F + RISC-V Hazard3)
 * This firmware runs on the ARM Cortex-M33 core at 150 MHz.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ── Pico SDK headers ──────────────────────────────────────────────────────── */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/binary_info.h"
#include "hardware/watchdog.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

/* ── Project headers ──────────────────────────────────────────────────────── */
#include "board_pins.h"
#include "rp2350b_init.h"
#include "spi_protocol.h"
#include "sdr_dma.h"
#include "cc1101_init.h"
#include "st25r3916_init.h"
#include "battery_monitor.h"
#include "watchdog.h"
#include "sleep_wake.h"
#include "peripheral_power.h"

/* ── Binary info for picotool ──────────────────────────────────────────────── */
bi_decl(bi_program_name("GhostBlade Firmware"))
bi_decl(bi_program_version_string("1.0.0"))
bi_decl(bi_program_description("GhostBlade RP2350B coprocessor firmware"))
bi_decl(bi_program_build_date(__DATE__))

bi_decl(bi_1pin_with_name(PIN_SPI0_RX,  "SPI0 RX  (from RK3576)"))
bi_decl(bi_1pin_with_name(PIN_SPI0_CSN, "SPI0 CSn (from RK3576)"))
bi_decl(bi_1pin_with_name(PIN_SPI0_SCK, "SPI0 SCK (from RK3576)"))
bi_decl(bi_1pin_with_name(PIN_SPI0_TX,  "SPI0 TX  (to RK3576)"))
bi_decl(bi_1pin_with_name(PIN_INT_REQ,  "INT_REQ  (to RK3576)"))
bi_decl(bi_1pin_with_name(PIN_HOST_RDY, "HOST_RDY (from RK3576)"))

/* ── Configuration constants ───────────────────────────────────────────────── */

#define MAIN_LOOP_DELAY_MS        1       /* Main loop tick interval */
#define TELEMETRY_INTERVAL_MS     100     /* Send telemetry every 100 ms */
#define WATCHDOG_TIMEOUT_MS       5000    /* Watchdog bark after 5 s */
#define BATTERY_UPDATE_INTERVAL_MS 1000   /* Read battery every 1 s */
#define BARK_INTERRUPT_PRIORITY    0x40    /* Watchdog bark ISR priority */

/* ── Module state tracking ─────────────────────────────────────────────────── */

static struct {
    bool hw_initialized;        /* rp2350b_init() completed */
    bool spi_ready;             /* SPI protocol handler initialized */
    bool sdr_dma_ready;         /* SDR DMA engine initialized */
    bool cc1101_ready;          /* CC1101 initialized */
    bool st25r3916_ready;       /* ST25R3916 initialized */
    bool battery_monitor_ready; /* Battery monitor initialized */
    bool core1_launched;        /* Core 1 (DMA engine) launched */
    uint32_t loop_count;        /* Main loop iteration counter */
    uint32_t last_telem_ms;    /* Last telemetry send timestamp */
    uint32_t last_batt_ms;     /* Last battery read timestamp */
} g_state;

/* ── Core 1 entry point: SDR DMA engine ────────────────────────────────────── */

/**
 * core1_entry — Entry point for RP2350B Core 1
 *
 * Core 1 is dedicated to the SDR DMA ring buffer engine. It continuously
 * polls DMA completion interrupts and feeds IQ data chunks into the
 * SPI protocol handler for transmission to the RK3576 host.
 *
 * Core 1 runs in an infinite loop. If the watchdog is not kicked by
 * Core 0 within the timeout window, both cores will be reset.
 */
void core1_entry(void)
{
    /* Signal that Core 1 is alive */
    multicore_fifo_push_blocking(0xAA55AA55U);

    /* Enter SDR DMA processing loop */
    while (true) {
        /* Wait for DMA completion interrupt or timeout */
        sdr_dma_process();

        /* Small delay to avoid busy-waiting when SDR is not streaming */
        tight_loop_contents();
    }
}

/* ── Watchdog bark interrupt handler ────────────────────────────────────────── */

/**
 * watchdog_bark_handler — Called when watchdog is about to expire
 *
 * This handler is triggered 1 tick before the watchdog resets the MCU.
 * It provides a last chance to log diagnostic information or attempt
 * a graceful shutdown of hardware peripherals.
 */
static void watchdog_bark_handler(void)
{
    /* Attempt to deassert INT_REQ to signal host we're going down */
    rp2350b_gpio_set(PIN_INT_REQ, true);  /* Active-low: deassert */

    /* Stop SDR DMA to prevent data corruption */
    sdr_dma_stop();

    /* Stop NFC polling */
    st25r3916_stop_polling();

    /* Put CC1101 in idle */
    cc1101_enter_idle();

    /* Feed the watchdog FIRST to give us time for a clean shutdown.
     * The bark fires 1 tick before reset, so we must feed immediately
     * to get a full timeout window for the remaining operations.
     * Without this feed, the peripheral shutdown code below may not
     * complete before the hardware reset occurs. */
    watchdog_kick();

    /* Log diagnostic info: bark handler was invoked.
     * The main loop will detect the stuck condition and the watchdog
     * will eventually reset if it remains stuck. */
}

/* ── Initialization sequence ───────────────────────────────────────────────── */

/**
 * init_peripherals — Initialize all hardware peripherals in order
 *
 * Returns: 0 on success, negative error code on failure.
 * Individual peripheral failures are logged but non-fatal — the firmware
 * continues with reduced functionality.
 */
static int init_peripherals(void)
{
    int ret;

    /* Step 1: Low-level hardware init (clocks, GPIO, SPI, ADC, PIO) */
    rp2350b_init();
    g_state.hw_initialized = true;

    /* Step 2: SPI protocol handler */
    spi_protocol_init();
    g_state.spi_ready = true;

    /* Step 3: Battery monitor (ADC) */
    ret = battery_monitor_init();
    if (ret == 0) {
        g_state.battery_monitor_ready = true;
    } else {
        printf("WARN: battery_monitor_init failed (%d), continuing\r\n", ret);
    }

    /* Step 4: SDR DMA ring buffer */
    ret = sdr_dma_init();
    if (ret == 0) {
        g_state.sdr_dma_ready = true;
    } else {
        printf("WARN: sdr_dma_init failed (%d), SDR unavailable\r\n", ret);
    }

    /* Step 5: CC1101 sub-GHz radio */
    ret = cc1101_init();
    if (ret == 0) {
        g_state.cc1101_ready = true;
    } else {
        printf("WARN: cc1101_init failed (%d), sub-GHz radio unavailable\r\n", ret);
    }

    /* Step 6: ST25R3916 NFC controller */
    ret = st25r3916_init();
    if (ret == 0) {
        g_state.st25r3916_ready = true;
    } else {
        printf("WARN: st25r3916_init failed (%d), NFC unavailable\r\n", ret);
    }

    /* Step 7: Sleep/wake state machine for power management */
    sleep_wake_init();

    /* Step 8: Peripheral power rails (enables SDR, NFC, sub-GHz rails) */
    peripheral_power_init();

    return 0;
}

/* ── Telemetry collection ─────────────────────────────────────────────────── */

/**
 * collect_telemetry — Read sensor values and update the protocol handler
 *
 * Called periodically from the main loop. Reads battery voltage,
 * temperature, and RSSI values from available subsystems.
 */
static void collect_telemetry(void)
{
    uint16_t vbat_mv = 0;
    int16_t temp_c_x10 = 0;
    uint16_t rssi_dbm_x10 = 0;
    uint16_t cc_rssi_x10 = 0;
    uint16_t nfc_field_mv = 0;

    /* Battery and temperature from ADC */
    if (g_state.battery_monitor_ready) {
        battery_monitor_update();
        vbat_mv = battery_monitor_get_vbat_mv();
        temp_c_x10 = battery_monitor_get_temp_c_x10();
    }

    /* SDR RSSI (would come from LMS7002M driver; placeholder) */
    /* TODO: Read LMS7002M RSSI register when SDR driver is integrated */

    /* CC1101 RSSI — get_rssi_x10 reads the register internally */
    if (g_state.cc1101_ready) {
        cc_rssi_x10 = (uint16_t)cc1101_get_rssi_x10();
    }

    /* NFC field strength */
    if (g_state.st25r3916_ready) {
        nfc_field_mv = st25r3916_get_field_strength_mv();
    }

    /* Update protocol handler telemetry cache */
    spi_protocol_update_telemetry(rssi_dbm_x10,
                                   (uint16_t)temp_c_x10,
                                   vbat_mv,
                                   cc_rssi_x10,
                                   nfc_field_mv);
}

/* ── Main entry point ──────────────────────────────────────────────────────── */

int main(void)
{
    uint32_t last_telem_time = 0;
    uint32_t last_batt_time = 0;
    uint32_t now;

    /* ── Step 1: Initialize stdio (UART + USB CDC) ─────────────────────── */
    stdio_init_all();

    /* Small delay to allow USB CDC to enumerate on the host */
    sleep_ms(500);

    printf("\r\n");
    printf("╔════════════════════════════════════════════════════╗\r\n");
    printf("║  GhostBlade Firmware v1.0.0                       ║\r\n");
    printf("║  RP2350B Coprocessor — Project NullSpectre        ║\r\n");
    printf("║  Build: %s %s           ║\r\n", __DATE__, __TIME__);
    printf("╚════════════════════════════════════════════════════╝\r\n");
    printf("\r\n");

    /* ── Step 2: Initialize hardware peripherals ────────────────────────── */
    printf("[MAIN] Initializing peripherals...\r\n");
    if (init_peripherals() != 0) {
        printf("[MAIN] FATAL: Peripheral initialization failed, rebooting\r\n");
        /* Use WATCHDOG_TIMEOUT_MS for the emergency reset rather than
         * 1 ms — the Pico SDK watchdog_enable() takes (delay_ms, pause_on_debug).
         * A 1 ms timeout would reset before the printf buffer flushes.
         * Use the configured timeout to allow diagnostic output. */
        watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
        while (1) tight_loop_contents();
    }
    printf("[MAIN] Peripherals initialized\r\n");

    /* Check for brownout reset (detected via watchdog scratch register) */
    if (watchdog_check_brownout()) {
        printf("[MAIN] INFO: Recovered from brownout/undervoltage reset\r\n");
    }

    /* ── Step 3: Configure watchdog ────────────────────────────────────── */
    printf("[MAIN] Configuring watchdog (%d ms timeout)...\r\n",
           WATCHDOG_TIMEOUT_MS);
    {
        bool was_wd_reset = watchdog_init();  /* returns true if boot was from WD reset */
        if (was_wd_reset) {
            printf("[MAIN] Recovered from watchdog reset\r\n");
        }
    }
    watchdog_enable_bark();
    printf("[MAIN] Watchdog active\r\n");

    /* ── Step 4: Launch Core 1 for SDR DMA engine ──────────────────────── */
    printf("[MAIN] Launching Core 1 (SDR DMA engine)...\r\n");
    multicore_launch_core1(core1_entry);

    /* Wait for Core 1 to signal it's alive (with 2-second timeout) */
    uint32_t core1_start = time_us_32();
    while (!multicore_fifo_rvalid()) {
        if ((time_us_32() - core1_start) > 2000000) {
            printf("[MAIN] WARN: Core 1 launch timeout, continuing anyway\r\n");
            break;
        }
        tight_loop_contents();
    }
    if (multicore_fifo_rvalid()) {
        uint32_t sig = multicore_fifo_pop_blocking();
        if (sig == 0xAA55AA55U) {
            g_state.core1_launched = true;
            printf("[MAIN] Core 1 launched successfully\r\n");
        } else {
            printf("[MAIN] WARN: Core 1 sent unexpected signal 0x%08X\r\n", sig);
        }
    }

    /* ── Step 5: Signal host that MCU is ready ──────────────────────────── */
    /* Deassert INT_REQ (it's active-low, so set HIGH = deasserted) */
    rp2350b_gpio_set(PIN_INT_REQ, true);
    printf("[MAIN] MCU ready, INT_REQ deasserted\r\n");

    /* ── Main loop ──────────────────────────────────────────────────────── */
    printf("[MAIN] Entering main loop\r\n");

    while (true) {
        /* Kick watchdog at the start of each loop iteration */
        watchdog_kick();

        /* Process sleep/wake state machine for power management.
         * This checks for SPI inactivity and transitions through
         * IDLE → LIGHT → DEEP sleep states, or wakes on activity. */
        enum sleep_state slp = sleep_wake_process();
        if (slp != SLEEP_IDLE) {
            /* In a non-idle sleep state, reduce telemetry frequency
             * and skip battery monitoring to save power. The watchdog
             * still gets kicked to prevent reset. */
            if (slp == SLEEP_DEEP) {
                sleep_ms(10);  /* Longer delay in deep sleep */
                continue;
            }
        }

        /* Process SPI commands from RK3576 */
        spi_protocol_process();

        /* Update system tick (drives uptime counter in protocol handler) */
        spi_protocol_tick();

        /* Periodic: Read battery and temperature (every 1 s) */
        now = to_ms_since_boot(get_absolute_time());
        if ((now - last_batt_time) >= BATTERY_UPDATE_INTERVAL_MS) {
            collect_telemetry();
            last_batt_time = now;
        }

        /* Periodic: Send telemetry to host (every 100 ms) */
        if ((now - last_telem_time) >= TELEMETRY_INTERVAL_MS) {
            spi_protocol_send_telemetry();
            last_telem_time = now;
        }

        /* Periodic: Brownout detection via battery monitor */
        if (g_state.battery_monitor_ready) {
            uint16_t vbat = battery_monitor_get_vbat_mv();
            /* If ADC has not been read yet, vbat will be 0. Skip
             * brownout detection on the first iteration to avoid
             * false positives. */
            if (vbat == 0) goto skip_brownout;
            static bool brownout_active = false;
            if (battery_is_brownout(vbat, &brownout_active)) {
                /* Mark brownout in watchdog scratch register so we
                 * can detect it after the inevitable reset */
                watchdog_mark_brownout();
                /* Also set the brownout flag in protocol handler */
                spi_protocol_set_brownout(true);
                /* Deassert INT_REQ (active-high) to signal host
                 * that MCU is entering brownout state. The host
                 * kernel driver will see the LOW_BATTERY flag in
                 * telemetry and the brownout_count sysfs attribute
                 * increment. */
                rp2350b_gpio_set(PIN_INT_REQ, false);
            } else if (!brownout_active) {
                spi_protocol_set_brownout(false);
                /* Reassert INT_REQ once brownout condition clears */
                rp2350b_gpio_set(PIN_INT_REQ, true);
            }
        skip_brownout:;
        }

        /* Yield to avoid 100% CPU usage in idle state */
        sleep_ms(MAIN_LOOP_DELAY_MS);

        g_state.loop_count++;
    }

    /* Unreachable — firmware runs until watchdog reset or power cycle */
    return 0;
}