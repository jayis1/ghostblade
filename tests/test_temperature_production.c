/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 jayis1
 * Exercise production ADC paths with mapped register backing, not hardware.
 * This does not emulate conversion timing, W1C bits, or electrical behavior.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include "battery_monitor.h"

#include "../firmware/rp2350b/src/battery_monitor.c"

static void set_sample(uint16_t raw)
{
    REG32(RP2350B_ADC_BASE + ADC_CS) = ADC_CS_READY;
    REG32(RP2350B_ADC_BASE + ADC_RESULT) = raw;
}

int main(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size > 0);
    void *registers = mmap((void *)RP2350B_ADC_BASE, (size_t)page_size,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                          -1, 0);
    assert(registers != MAP_FAILED);
    assert(registers == (void *)RP2350B_ADC_BASE);

    for (uint16_t raw = 0; raw <= 4095; raw++) {
        double expected = 270.0 -
            ((double)raw * 3300000.0 / 4095.0 - 706000.0) * 10.0 / 1721.0;
        set_sample(raw);
        battery_monitor_update();
        int16_t temperature = battery_monitor_get_temp_c_x10();
        assert(fabs((double)temperature - expected) <= 1.01);
        assert(temperature_read_dc10() == temperature);
    }

    /* Clearly above the 105 C shutdown threshold, previously about 35 C. */
    set_sample(700);
    int16_t hot = temperature_read_dc10();
    assert(hot > 1050);
    assert(temperature_is_overtemp(hot));
    assert(temperature_is_critical(hot));
    power_state_set(POWER_STATE_ACTIVE);
    assert(power_state_update(3800, hot, true) == POWER_STATE_SHUTDOWN);

    set_sample(876);
    assert(!temperature_is_overtemp(temperature_read_dc10()));
    assert(munmap(registers, (size_t)page_size) == 0);
    puts("Production temperature conversion and thermal shutdown tests passed");
    return 0;
}
