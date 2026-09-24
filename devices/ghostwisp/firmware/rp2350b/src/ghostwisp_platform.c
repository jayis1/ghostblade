/*
 * ghostwisp_platform.c — Pico SDK platform hooks for the GhostWisp boot core
 *
 * Author: jayis1
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <stdint.h>

#include "pico/time.h"

uint32_t ghostwisp_get_uptime_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/* Signature verification is deliberately fail-closed until the signed-update
 * module and immutable public key are present. A strong implementation in that
 * module overrides this weak hook at link time. */
__attribute__((weak)) bool ghostwisp_verify_firmware_signature(bool partition_b)
{
    (void)partition_b;
    return false;
}
