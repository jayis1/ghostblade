/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 jayis1
 * Exercise production I2C entry points without operating hardware.
 * DRIVER_HOST_TEST replaces bus access, not address validation.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ghostwisp_i2c.h"

extern void i2c_test_reset(void);
extern void i2c_test_add_slave(uint8_t addr);

static void check_invalid_addresses(void)
{
    for (unsigned int addr = 0x80; addr <= UINT8_MAX; addr++) {
        uint8_t rx = 0xA5;
        uint16_t rx16 = 0xA55A;
        const uint8_t tx[] = { 0x10, 0x42 };
        uint32_t errors = i2c_get_error_count();

        assert(i2c_write_blocking(addr, tx, sizeof(tx)) == I2C_ERR_INVALID_ARG);
        assert(i2c_read_blocking(addr, &rx, 1) == I2C_ERR_INVALID_ARG);
        assert(i2c_write_blocking(addr, NULL, 0) == I2C_ERR_INVALID_ARG);
        assert(i2c_read_blocking(addr, NULL, 0) == I2C_ERR_INVALID_ARG);
        assert(i2c_write_register(addr, 0x10, 0x42) == I2C_ERR_INVALID_ARG);
        assert(i2c_read_register(addr, 0x10, &rx) == I2C_ERR_INVALID_ARG);
        assert(i2c_read_register16(addr, 0x10, &rx16) == I2C_ERR_INVALID_ARG);
        assert(i2c_probe(addr) == I2C_ERR_INVALID_ARG);
        assert(rx == 0xA5);
        assert(rx16 == 0xA55A);
        assert(i2c_get_error_count() == errors);
    }
}

int main(void)
{
    i2c_test_reset();
    /* Invalid input is rejected even before initialization. */
    check_invalid_addresses();
    assert(i2c_init(I2C_INSTANCE_0, 400000) == I2C_OK);
    /* Populate all valid addresses, including the endpoints. Invalid values
     * must not alias these devices or change their register contents. */
    for (unsigned int base = 0; base < 0x80; base += 8) {
        i2c_test_reset();
        assert(i2c_init(I2C_INSTANCE_0, 400000) == I2C_OK);
        for (unsigned int addr = base; addr < base + 8; addr++) {
            i2c_test_add_slave(addr);
            assert(i2c_write_register(addr, 0x10, 0x5A) == I2C_OK);
        }
        check_invalid_addresses();
        for (unsigned int addr = base; addr < base + 8; addr++) {
            uint8_t rx = 0;
            assert(i2c_probe(addr) == I2C_OK);
            assert(i2c_read_register(addr, 0x10, &rx) == I2C_OK);
            assert(rx == 0x5A);
        }
    }
    puts("GhostWisp I2C address validation passed");
    return 0;
}
