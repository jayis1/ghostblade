/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 GhostBlade Project
 * Author: jayis1
 *
 * GPIO data access uses SIO, not IO_BANK0's mux/interrupt registers.
 * Reference: pico-sdk 2.2.0, src/rp2350/hardware_regs/include/hardware/regs/
 * addressmap.h and sio.h. RP2350 low/high banks are interleaved; GPIO32..47
 * use bits 0..15 of the high bank, not shifts of a 32-bit word by 32 or more.
 */
#include "rp2350b_init.h"

#define RP2350B_GPIO_COUNT 48U
#define RP2350B_SIO_BASE UINT32_C(0xd0000000)
#define SIO_GPIO_IN_OFFSET 0x04U
#define SIO_GPIO_OUT_SET_OFFSET 0x18U
#define SIO_GPIO_OUT_CLR_OFFSET 0x20U

/* Tests replace only register access and the ARM barrier. The address/bank
 * selection and bit arithmetic below remain the production implementation.
 */
#ifdef RP2350B_GPIO_MMIO_TEST
extern volatile uint32_t *rp2350b_gpio_test_reg(uintptr_t address);
#define GPIO_REG(address) (*rp2350b_gpio_test_reg(address))
#define GPIO_BARRIER() ((void)0)
#else
#define GPIO_REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define GPIO_BARRIER() __asm__ volatile ("dmb" ::: "memory")
#endif

void rp2350b_gpio_set(uint8_t pin, bool value)
{
    if (pin >= RP2350B_GPIO_COUNT)
        return;

    uint32_t offset = value ? SIO_GPIO_OUT_SET_OFFSET : SIO_GPIO_OUT_CLR_OFFSET;
    if (pin >= 32U)
        offset += 4U;
    GPIO_REG(RP2350B_SIO_BASE + offset) = UINT32_C(1) << (pin & 31U);
    GPIO_BARRIER();
}

bool rp2350b_gpio_get(uint8_t pin)
{
    if (pin >= RP2350B_GPIO_COUNT)
        return false;

    uint32_t offset = SIO_GPIO_IN_OFFSET + (pin >= 32U ? 4U : 0U);
    GPIO_BARRIER();
    return (GPIO_REG(RP2350B_SIO_BASE + offset) &
            (UINT32_C(1) << (pin & 31U))) != 0;
}
