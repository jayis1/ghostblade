/* SPDX-License-Identifier: GPL-2.0-or-later
 * Author: jayis1
 * Link the production GPIO implementation, backing only register accesses.
 * Reference addresses: pico-sdk 2.2.0 RP2350 addressmap.h and sio.h:
 * https://github.com/raspberrypi/pico-sdk/tree/2.2.0/src/rp2350/hardware_regs/include/hardware/regs
 * Memory backing records writes; it does not emulate atomic SET/CLR aliases,
 * mux/pad initialization, barriers, electrical levels, or board operation.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rp2350b_init.h"

static uint32_t sio[0x28 / 4];
static unsigned int accesses;

volatile uint32_t *rp2350b_gpio_test_reg(uintptr_t address)
{
    accesses++;
    assert((address & 3U) == 0);
    assert(address >= UINT32_C(0xd0000000));
    assert(address < UINT32_C(0xd0000028));
    return &sio[(address - UINT32_C(0xd0000000)) / 4];
}

static void check_write(unsigned int offset, uint32_t mask)
{
    assert(accesses == 1);
    for (unsigned int i = 0; i < sizeof(sio) / sizeof(sio[0]); i++)
        assert(sio[i] == (i == offset / 4 ? mask : 0));
    memset(sio, 0, sizeof(sio));
    accesses = 0;
}

int main(void)
{
    for (unsigned int pin = 0; pin < 48; pin++) {
        unsigned int bank = pin < 32 ? 0 : 4;
        /* Independent 64-bit reference avoids copying the production mask. */
        uint32_t mask = (uint32_t)((UINT64_C(1) << pin) >> (pin < 32 ? 0 : 32));

        rp2350b_gpio_set((uint8_t)pin, true);
        check_write(0x18 + bank, mask);
        rp2350b_gpio_set((uint8_t)pin, false);
        check_write(0x20 + bank, mask);

        /* A set bit in the other bank must not affect this pin. */
        sio[(0x04 + (bank ^ 4)) / 4] = mask;
        assert(!rp2350b_gpio_get((uint8_t)pin));
        assert(accesses == 1);
        sio[(0x04 + bank) / 4] = mask;
        assert(rp2350b_gpio_get((uint8_t)pin));
        assert(accesses == 2);
        sio[(0x04 + bank) / 4] = ~mask;
        assert(!rp2350b_gpio_get((uint8_t)pin));
        assert(accesses == 3);
        memset(sio, 0, sizeof(sio));
        accesses = 0;
    }
    for (unsigned int pin = 48; pin <= UINT8_MAX; pin++) {
        rp2350b_gpio_set((uint8_t)pin, true);
        rp2350b_gpio_set((uint8_t)pin, false);
        assert(!rp2350b_gpio_get((uint8_t)pin));
    }
    assert(accesses == 0);
    for (unsigned int i = 0; i < sizeof(sio) / sizeof(sio[0]); i++)
        assert(sio[i] == 0);
    puts("RP2350B GPIO production MMIO: pins 0..47 and invalid pins 48..255 passed");
    return 0;
}
