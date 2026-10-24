/* SPDX-License-Identifier: GPL-2.0-or-later
 * Author: jayis1
 * Exercise the production GPIO hardware path with register-backed memory.
 * Expected addresses/bits are from pico-sdk 2.2.0 rp2350 hardware/regs:
 * https://github.com/raspberrypi/pico-sdk/tree/2.2.0/src/rp2350/hardware_regs/include/hardware/regs
 * This checks addressing and masks, not electrical behavior or bus ordering.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ghostwisp_gpio.h"

static uint32_t sio[0x48 / 4];
static uint32_t pads[49];
static uint32_t io[0x260 / 4];
static unsigned int accesses;
static unsigned int callback_count;
static uint8_t callback_pin;
static uint32_t callback_events;

static void on_irq(uint8_t pin, uint32_t events)
{
    callback_count++;
    callback_pin = pin;
    callback_events = events;
}

volatile uint32_t *gpio_mmio_test_reg(uintptr_t address)
{
    accesses++;
    assert((address & 3U) == 0);
    if (address >= 0xd0000000U && address < 0xd0000048U)
        return &sio[(address - 0xd0000000U) / 4];
    if (address >= 0x40028000U && address < 0x40028260U)
        return &io[(address - 0x40028000U) / 4];
    assert(address >= 0x40038000U && address < 0x400380c4U);
    return &pads[(address - 0x40038000U) / 4];
}

static void check_only_write(unsigned int offset, uint32_t mask)
{
    for (unsigned int i = 0; i < sizeof(sio) / sizeof(sio[0]); i++)
        assert(sio[i] == (i == offset / 4 ? mask : 0));
    memset(sio, 0, sizeof(sio));
}

static void check_irq(uint8_t pin)
{
    assert(gpio_init() == GPIO_OK);
    memset(io, 0, sizeof(io));
    /* Independent expected eight-pin grouping, including pin 47. */
    unsigned int shift = (pin % 8) * 4;
    unsigned int inte = (0x248 + (pin / 8) * 4) / 4;
    unsigned int intr = (0x230 + (pin / 8) * 4) / 4;
    uint32_t events = UINT32_C(8) << shift;
    io[inte] = 0x55555555U;
    assert(gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, true) == GPIO_OK);
    assert(io[inte] == (0x55555555U | events));
    assert(io[intr] == 0);
    assert(gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, false) == GPIO_OK);
    assert(io[inte] == 0x55555555U);
    assert(gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, true) == GPIO_OK);
    io[intr] = events;
    callback_count = 0;
    gpio_set_irq_callback(on_irq);
    gpio_handle_irq();
    assert(callback_count == 1);
    assert(callback_pin == pin);
    assert(callback_events == GPIO_IRQ_EDGE_RISE);
    /* Backing memory captures the W1C write; it does not emulate W1C. */
    assert(io[intr] == (UINT32_C(12) << shift));
    assert(gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE, false) == GPIO_OK);
    callback_count = 0;
    gpio_handle_irq();
    assert(callback_count == 0);
}

int main(void)
{
    assert(gpio_init() == GPIO_OK);
    for (uint8_t pin = 0; pin < 48; pin++) {
        unsigned int bank = pin < 32 ? 0 : 4;
        /* Use a separate 64-bit expression for the reference mask. */
        uint32_t mask = (uint32_t)((UINT64_C(1) << pin) >> (pin < 32 ? 0 : 32));

        assert(gpio_put(pin, true) == GPIO_OK);
        check_only_write(0x18 + bank, mask);
        assert(gpio_put(pin, false) == GPIO_OK);
        check_only_write(0x20 + bank, mask);
        assert(gpio_toggle(pin) == GPIO_OK);
        check_only_write(0x28 + bank, mask);
        pads[pin + 1] = 0x1ffU;
        assert(gpio_set_dir(pin, GPIO_DIR_OUTPUT) == GPIO_OK);
        check_only_write(0x38 + bank, mask);
        assert(pads[pin + 1] == 0x17fU);
        assert(gpio_set_dir(pin, GPIO_DIR_INPUT) == GPIO_OK);
        check_only_write(0x40 + bank, mask);
        assert(pads[pin + 1] == 0x1ffU);

        assert(gpio_set_pull(pin, GPIO_PULL_UP) == GPIO_OK);
        assert(pads[pin + 1] == 0x1fbU);
        assert(gpio_get_pull(pin) == GPIO_PULL_UP);
        assert(gpio_set_pull(pin, GPIO_PULL_DOWN) == GPIO_OK);
        assert(pads[pin + 1] == 0x1f7U);
        assert(gpio_get_pull(pin) == GPIO_PULL_DOWN);
        assert(gpio_set_pull(pin, GPIO_PULL_NONE) == GPIO_OK);
        assert(pads[pin + 1] == 0x1f3U);
        assert(gpio_get_pull(pin) == GPIO_PULL_NONE);
        assert(gpio_set_pull(pin, GPIO_PULL_BUSKEEP) == GPIO_OK);
        assert(pads[pin + 1] == 0x1ffU);
        assert(gpio_get_pull(pin) == GPIO_PULL_BUSKEEP);
        assert(gpio_set_function(pin, GPIO_FUNC_SIO) == GPIO_OK);
        assert(io[(4 + pin * 8) / 4] == GPIO_FUNC_SIO);
        assert(gpio_get_function(pin) == GPIO_FUNC_SIO);

        /* Opposite bank alone must not affect this pin. */
        sio[(0x30 + (bank ^ 4)) / 4] = mask;
        assert(gpio_get_dir(pin) == GPIO_DIR_INPUT);
        sio[(0x30 + bank) / 4] = mask;
        assert(gpio_get_dir(pin) == GPIO_DIR_OUTPUT);
        memset(sio, 0, sizeof(sio));
        sio[(0x04 + (bank ^ 4)) / 4] = mask;
        assert(!gpio_get(pin));
        sio[(0x04 + bank) / 4] = mask;
        assert(gpio_get(pin));
        memset(sio, 0, sizeof(sio));
        check_irq(pin);
    }
    unsigned int before = accesses;
    for (unsigned int pin = 48; pin <= UINT8_MAX; pin++) {
        assert(gpio_put((uint8_t)pin, true) == GPIO_ERR_INVALID_PIN);
        assert(gpio_toggle((uint8_t)pin) == GPIO_ERR_INVALID_PIN);
        assert(gpio_set_dir((uint8_t)pin, GPIO_DIR_OUTPUT) == GPIO_ERR_INVALID_PIN);
        assert(gpio_get_dir((uint8_t)pin) == -1);
        assert(!gpio_get((uint8_t)pin));
        assert(gpio_set_function((uint8_t)pin, GPIO_FUNC_SIO) == GPIO_ERR_INVALID_PIN);
        assert(gpio_get_function((uint8_t)pin) == GPIO_FUNC_NONE);
        assert(gpio_set_pull((uint8_t)pin, GPIO_PULL_UP) == GPIO_ERR_INVALID_PIN);
        assert(gpio_get_pull((uint8_t)pin) == GPIO_PULL_NONE);
        assert(gpio_set_irq_enabled((uint8_t)pin, 8, true) == GPIO_ERR_INVALID_PIN);
        assert(gpio_acknowledge_irq((uint8_t)pin) == GPIO_ERR_INVALID_PIN);
    }
    assert(accesses == before);
    puts("GPIO production MMIO: all 48 pins and invalid pins 48..255 passed");
    return 0;
}
