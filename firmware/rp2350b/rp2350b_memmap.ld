/* rp2350b_memmap.ld — GhostBlade RP2350B Memory Map
 *
 * Copyright (C) 2026 GhostBlade Project
 * SPDX-License-Identifier: MIT
 *
 * Linker script for the RP2350B firmware on the GhostBlade board.
 *
 * RP2350B memory layout:
 *   - SRAM bank 0-5: 520 KB total (8 banks × 65 KB)
 *   - SRAM bank 6-7: 8 KB strip (boot ROM scratch)
 *   - PSRAM: 2 MB external (via QSPI, not cached)
 *   - XIP flash: Up to 16 MB (via QSPI)
 *
 * Memory allocation strategy:
 *   - .text + .rodata → Flash (XIP)
 *   - .data + .bss → SRAM bank 0-4 (fast access)
 *   - DMA ring buffers → SRAM bank 5 (no CPU contention)
 *   - Stack → SRAM bank 4 (M33 stack)
 *   - Core 1 stack → SRAM bank 3 (separate bank avoids contention)
 *   - PSRAM → Large IQ sample buffers (streamed to host)
 */

/* ── Memory Regions ──────────────────────────────────────────────────────── */

MEMORY
{
    /* Flash: XIP region for code and read-only data
     * RP2350B supports up to 16 MB QSPI flash.
     * Bootloader occupies first 4 KB (0x10000000 – 0x10000FFF).
     */
    FLASH (rx)  : ORIGIN = 0x10000100, LENGTH = 16384K - 256

    /* SRAM banks 0-4: Primary SRAM for data, stack, and critical code
     * Each bank is 4 KB (bank 0) + 4 KB (bank 1) + ... = 260 KB
     * We use a single region for simplicity; the Pico SDK manages
     * the actual bank allocation.
     */
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 260K

    /* SRAM bank 5: Dedicated DMA memory
     * This bank is reserved for DMA ring buffers and is NOT
     * used for regular allocations. It has no CPU contention
     * during DMA transfers, making it ideal for SDR IQ streaming.
     */
    DMA_RAM (rwx) : ORIGIN = 0x20041000, LENGTH = 4K

    /* SRAM banks 6-7: Boot ROM scratch (8 KB)
     * Reserved for boot ROM use — do not place user code here.
     */

    /* PSRAM: External 2 MB PSRAM via QSPI
     * Used for large SDR IQ sample buffers and overflow.
     * Access is slower than SRAM (~4x latency) but much larger.
     */
    PSRAM (rwx) : ORIGIN = 0x11000000, LENGTH = 2M
}

/* ── Entry Point ─────────────────────────────────────────────────────────── */

ENTRY(_entry_point)

/* ── Stack Configuration ─────────────────────────────────────────────────── */

/* Core 0 (M33) stack: 4 KB */
__STACK_SIZE_CORE0 = 4096;

/* Core 1 (Hazard3 / M33) stack: 4 KB */
__STACK_SIZE_CORE1 = 4096;

/* IRQ stack: 1 KB (shared by both cores) */
__STACK_SIZE_IRQ = 1024;

/* ── Section Definitions ─────────────────────────────────────────────────── */

SECTIONS
{
    /* ── Flash sections (XIP) ───────────────────────────────────────────── */

    .text : {
        /* Vector table must be first */
        *(.vectors)
        *(.vectors.stub)

        /* Reset handler and early init */
        *(.reset)

        /* Main code */
        *(.text .text.*)

        /* Glue code (ARM/Thumb interwork) */
        *(.glue_7t)
        *(.glue_7)

        /* Read-only data */
        *(.rodata .rodata.*)

        /* Pico SDK binary info */
        *(.binary_info)

        /* C++ constructors/destructors */
        . = ALIGN(4);
        PROVIDE(__init_array_start = .);
        *(.init_array .init_array.*)
        PROVIDE(__init_array_end = .);

        PROVIDE(__fini_array_start = .);
        *(.fini_array .fini_array.*)
        PROVIDE(__fini_array_end = .);

        /* ARM exception handling (if used) */
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)

    } > FLASH

    /* ── SRAM sections (critical, low-latency) ───────────────────────────── */

    /* Initialized data — copied from flash at startup */
    .data : {
        . = ALIGN(4);
        PROVIDE(__data_start__ = .);

        *(.data .data.*)

        /* Small constant data placed in SRAM for fast access */
        *(.sdata .sdata.*)

        . = ALIGN(4);
        PROVIDE(__data_end__ = .);

    } > SRAM AT > FLASH

    /* Uninitialized data */
    .bss : {
        . = ALIGN(4);
        PROVIDE(__bss_start__ = .);

        *(.bss .bss.*)
        *(COMMON)
        *(.sbss .sbss.*)

        . = ALIGN(4);
        PROVIDE(__bss_end__ = .);

    } > SRAM

    /* Heap starts after BSS */
    . = ALIGN(4);
    PROVIDE(__heap_start__ = .);

    /* Heap grows upward toward stack; total SRAM available minus stacks */
    PROVIDE(__heap_end__ = ORIGIN(SRAM) + LENGTH(SRAM)
             - __STACK_SIZE_CORE0 - __STACK_SIZE_CORE1 - __STACK_SIZE_IRQ);

    /* ── Stacks ─────────────────────────────────────────────────────────── */

    /* Core 0 (main) stack */
    .core0_stack (NOLOAD) : {
        . = ALIGN(8);
        PROVIDE(__core0_stack_bottom__ = .);
        . = . + __STACK_SIZE_CORE0;
        PROVIDE(__core0_stack_top__ = .);
    } > SRAM

    /* Core 1 stack */
    .core1_stack (NOLOAD) : {
        . = ALIGN(8);
        PROVIDE(__core1_stack_bottom__ = .);
        . = . + __STACK_SIZE_CORE1;
        PROVIDE(__core1_stack_top__ = .);
    } > SRAM

    /* IRQ stack */
    .irq_stack (NOLOAD) : {
        . = ALIGN(8);
        PROVIDE(__irq_stack_bottom__ = .);
        . = . + __STACK_SIZE_IRQ;
        PROVIDE(__irq_stack_top__ = .);
    } > SRAM

    /* ── DMA RAM section (SRAM bank 5) ───────────────────────────────────── */

    .dma_buffers (NOLOAD) : {
        . = ALIGN(256);  /* 256-byte alignment for DMA */
        PROVIDE(__dma_start__ = .);

        /* SDR IQ DMA ring buffers */
        *(.dma.sdr_rx)
        *(.dma.sdr_tx)

        /* SPI bridge DMA buffers */
        *(.dma.spi_rx)
        *(.dma.spi_tx)

        . = ALIGN(4);
        PROVIDE(__dma_end__ = .);

    } > DMA_RAM

    /* ── PSRAM sections (large buffers, slower access) ──────────────────── */

    .psram (NOLOAD) : {
        . = ALIGN(4096);  /* 4 KB page alignment */
        PROVIDE(__psram_start__ = .);

        /* Large SDR IQ sample capture buffers */
        *(.psram.iq_capture)

        /* NFC tag data buffers */
        *(.psram.nfc_data)

        /* CC1101 packet buffers */
        *(.psram.cc1101_data)

        /* General PSRAM heap */
        . = ALIGN(4);
        PROVIDE(__psram_heap_start__ = .);
        PROVIDE(__psram_heap_end__ = ORIGIN(PSRAM) + LENGTH(PSRAM));

    } > PSRAM

    /* ── Discarded sections ──────────────────────────────────────────────── */

    /DISCARD/ : {
        *(.ARM.attributes)
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}

/* ── Exported symbols for runtime ─────────────────────────────────────────── */

/* Flash layout addresses (used by boot2 and UF2 writer) */
PROVIDE(__flash_binary_start = ORIGIN(FLASH));
PROVIDE(__flash_binary_end = ORIGIN(FLASH) + LENGTH(FLASH));

/* SRAM boundaries */
PROVIDE(__ram_start = ORIGIN(SRAM));
PROVIDE(__ram_end = ORIGIN(SRAM) + LENGTH(SRAM));

/* DMA buffer boundaries */
PROVIDE(__dma_ram_start = ORIGIN(DMA_RAM));
PROVIDE(__dma_ram_end = ORIGIN(DMA_RAM) + LENGTH(DMA_RAM));

/* PSRAM boundaries */
PROVIDE(__psram_start = ORIGIN(PSRAM));
PROVIDE(__psram_end = ORIGIN(PSRAM) + LENGTH(PSRAM));

/* Stack canary addresses */
PROVIDE(__stack_chk_guard = __core0_stack_top__ - 4);