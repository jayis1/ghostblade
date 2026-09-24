/*
 * main.c — GhostWisp RP2350B firmware entry point
 *
 * Author: jayis1
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>

#include "pico/stdlib.h"

#include "ghostwisp_boot.h"

int main(void)
{
    stdio_init_all();

    /* Give USB CDC a short, bounded opportunity to enumerate. UART logging is
     * available immediately and boot never depends on a host connection. */
    sleep_ms(25);

    const boot_result_t result = boot_complete();
    const boot_state_t *state = boot_get_state();

    printf("GhostWisp boot: result=%s reset=%s mode=%u time_ms=%lu\n",
           boot_result_name(result),
           boot_reset_reason_name(state->reset_reason),
           (unsigned)state->boot_strap,
           (unsigned long)state->boot_time_ms);

    if (result != BOOT_OK) {
        printf("GhostWisp boot failed in phase=%s; recovery mode active\n",
               boot_phase_name(state->current_phase));
        boot_enter_recovery();
    }

    for (;;) {
        boot_kick_watchdog();
        sleep_ms(250);
    }
}
