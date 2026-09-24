# GhostWisp Boot Contract

**Author: jayis1**
**Revision: A (2026-09-24)**
**Status: Defined; host tests pass; hardware validation pending**

This document is the authoritative boot contract for the GhostWisp RP2350B
firmware. It defines ordered startup, clock/memory/reset/watchdog/peripheral
policy, fail-closed recovery, observable diagnostics, and testable acceptance
criteria. GhostBlade files are explicitly excluded — nothing in this contract
modifies frozen GhostBlade hardware, firmware, or drivers.

---

## 1. Scope and boundaries

### In scope

- RP2350B primary MCU on GhostWisp (Project Little Spectre, Rev A)
- Phases 0–9 of `boot_complete()` as implemented in
  `firmware/rp2350b/src/ghostwisp_boot.c`
- Host-testable behavior: reset-cause interpretation, strap selection,
  startup ordering, watchdog policy, GPIO muxing, diagnostic state transitions,
  recovery mode, and fail-closed signature policy

### Explicitly excluded

- GhostBlade RK3576 coprocessor and its boot sequence
- `firmware/rp2350b/rp2350b_memmap.ld` (GhostBlade linker script — must not be reused)
- Any change to frozen GhostBlade source, configuration, or hardware files
- Rev A hardware items listed in Section 6 (unresolved)

---

## 2. Startup ordering (Phases 0–9)

Phases execute in strict sequential order. A phase failure halts the sequence
and calls `boot_enter_recovery()` for the failure classes listed below.

| Phase | ID | Name | Policy on failure |
|-------|----|------|-------------------|
| 0 | BOOT_PHASE_RESET (0) | Reset capture + strap sample | Fatal — cannot continue |
| 1 | BOOT_PHASE_CLOCKS (1) | System clocks (150/48/133 MHz) | Fatal |
| 2 | BOOT_PHASE_FPU (2) | ARM Cortex-M33 FPU enable | Fatal |
| 3 | BOOT_PHASE_GPIO (3) | GPIO pin muxing | Fatal |
| 4 | BOOT_PHASE_UART (4) | UART1 debug console (115200 8N1) | Fatal |
| 5 | BOOT_PHASE_USB (5) | USB CDC enumeration | Fatal |
| 6 | BOOT_PHASE_WATCHDOG (6) | Watchdog arm (5 s timeout) | Fatal |
| 7 | BOOT_PHASE_BATTERY (7) | Battery/fuel gauge check | Non-fatal if VBUS present; fatal otherwise |
| 8 | BOOT_PHASE_POST (8) | Peripheral self-test | Fatal only for QSPI flash failure |
| 9 | BOOT_PHASE_PARTITION (9) | A/B selection + signature verify | Fatal → recovery on sig failure |

**FACTORY_RESET strap (0x03) bypasses phases 1–9** and enters recovery immediately.

---

## 3. Clock policy

| Domain | Frequency | Source |
|--------|-----------|--------|
| System core | 150 MHz | PLL_SYS |
| Peripheral | 48 MHz | PLL_USB |
| XIP flash | 133 MHz | PLL_SYS ÷ 2 |

Reference: RP2350B Datasheet §2 (Clocks).

---

## 4. Reset-cause interpretation

Reset reason is captured at Phase 0 from the watchdog REASON register and
scratch registers. The mapping is:

| Condition | Mapped reason |
|-----------|---------------|
| `WD_REASON_TIMER` bit set | `RESET_REASON_WATCHDOG` |
| `WD_REASON_FORCE` bit set (TIMER takes priority) | `RESET_REASON_FORCE` |
| scratch7 == `WD_SCRATCH_BOD_MAGIC` (0xB047B00F) | `RESET_REASON_BROWNOUT` |
| None of the above | `RESET_REASON_POWERON` |

Brownout count accumulates in scratch1. scratch7 is cleared after reading.

---

## 5. Watchdog policy

- Timeout: **5000 ms** (5 000 000 µs loaded into WD_LOAD)
- Bark interrupt: 1000 ms before timeout (early warning)
- Debug pause: enabled (watchdog pauses in SWD debug)
- `boot_kick_watchdog()` reloads the same 5 000 000 µs value
- Watchdog is **enabled before battery phase** — if any later phase hangs
  the board will reset rather than brick

---

## 6. GPIO pin muxing contract

All pins must be configured in Phase 3 before any peripheral is used.

| Group | Pins | Function | Direction | Pull |
|-------|------|----------|-----------|------|
| CC1101 SPI | 6,7,8 | SPI | - | - |
| CC1101 CSn | 9 | SIO | Output | - |
| CC1101 GDO0/2 | 10,11 | SIO | Input | None |
| NFC SPI | 12,13,14 | SPI | - | - |
| NFC CSn | 15 | SIO | Output | - |
| NFC IRQ | 16 | SIO | Input | Pull-up |
| SD SPI | 17,18,19 | SPI | - | - |
| SD CSn | 20 | SIO | Output | - |
| SD CD | 21 | SIO | Input | Pull-up |
| Display SCK/TX | 22,23 | PIO0 | - | - |
| Display CSn/DC/RST/BL | 24,25,26,27 | SIO | Output | - |
| Buttons (7) | 28–34 | SIO | Input | Pull-up |
| IR TX | 2 | SIO | Output | - |
| IR RX | 3 | SIO | Input | None |
| USB VBUS sense | 4 | SIO | Input | None |
| USB host enable | 5 | SIO | Output | - |
| Battery I2C SDA/SCL | 0,1 | I2C | - | Pull-up |
| Expansion UART | 36,37 | UART1 | - | - |
| Expansion GPIO | 35,38,39 | SIO | Input | None |
| RGB LED | 40 | PIO1 | - | - |
| Buzzer | 41 | PWM | - | - |
| Vibration | 42 | SIO | Output | - |
| Power switch | 43 | SIO | Input | None |
| Charge status | 44 | SIO | Input | Pull-up |
| Radio disable | 45 | SIO | Output | None |
| Boot straps | 46,47 | SIO | Input | Pull-down |

**Radio disable (pin 45) is asserted HIGH during boot** (safe state — radios off).

---

## 7. Recovery mode and fail-closed policy

Recovery mode is entered when:
- `BOOT_STRAP_RECOVERY` or `BOOT_STRAP_FACTORY_RESET` is detected at Phase 0
- `BOOT_ERR_SIGN_VERIFY`, `BOOT_ERR_PARTITION`, or `BOOT_ERR_HARDWARE` occurs

In recovery mode:
- All radios remain disabled (radio disable pin stays high)
- Only USB update and diagnostics are available
- `boot_state.signature_verified` is `false`
- `boot_state.boot_strap` is forced to `BOOT_STRAP_RECOVERY`

**Signature verification is fail-closed**: unsigned or unverifiable images cannot
enter normal operation. Both A and B partitions failing verification triggers recovery.

Battery phase is **skipped** in recovery mode (USB power assumed).

---

## 8. Diagnostic state

`boot_state_t` fields observable after boot:

| Field | Type | Meaning |
|-------|------|---------|
| `current_phase` | `boot_phase_t` | Last phase reached |
| `result` | `boot_result_t` | `BOOT_OK` (0) or error code |
| `reset_reason` | `reset_reason_t` | Why the device booted |
| `boot_strap` | `uint8_t` | Sampled strap value |
| `partition_b` | `bool` | True if booting from B partition |
| `signature_verified` | `bool` | True if image signature passed |
| `post_passed` | `bool` | True if POST completed without critical failure |
| `boot_time_ms` | `uint32_t` | Total boot duration in ms |
| `phase_times_ms[]` | `uint32_t[10]` | Per-phase elapsed time |
| `brownout_count` | `uint32_t` | Cumulative brownout events from scratch |

---

## 9. Acceptance criteria

### 9.1 Host/build checks (automated — `make check`)

All of the following are verified by `devices/ghostwisp/tests/test_ghostwisp_boot.c`
(157 assertions) without physical hardware:

- All compile-time `_Static_assert` checks (phases, errors, constants)
- Reset-cause mapping for all combinations (power-on, watchdog, brownout, force)
- Brownout count accumulation and scratch7 clearing
- Boot strap detection for all 4 values
- Phase ordering: 0→9 in `boot_complete()`
- Factory reset strap bypasses to recovery immediately
- Watchdog loaded to exactly 5 000 000 µs and enabled
- `boot_kick_watchdog()` reloads the same value
- GPIO muxing for all pins in the table above
- Recovery mode: battery phase skipped, signature skipped, boot_strap set to RECOVERY
- Name lookups for all phase/result/reason enums return non-NULL strings
- GhostBlade isolation: no `rk3576`, `blade_boot`, `blade_main`, `rp2350b_memmap`
  symbols in the test binary (checked by `check.sh isolation`)
- No GhostBlade `#include` directives in GhostWisp headers (`check.sh headers`)

Run with:
```
cd devices/ghostwisp
make check
```

### 9.2 HIL checks (require physical GhostWisp board + SWD/UART access)

The following cannot be verified without hardware and are **NOT automated**:

- Actual system clock frequencies (150/48/133 MHz) measured on oscilloscope
- Watchdog fires after exactly 5 s under hardware reset conditions
- UART1 output on GPIO 36/37 at 115200 baud
- USB CDC enumerates on a real host (lsusb / dmesg)
- POST SPI reads return correct JEDEC/chip-ID for CC1101, ST25R3916, QSPI flash
- Brownout detection fires under actual voltage drop below threshold
- Signature verification passes with a real signed image
- Both partitions failing signature triggers recovery with radio disable held high
- Radio disable pin (GPIO 45) measures HIGH throughout boot until application releases
- End-to-end boot time < 2 s (phases 0–6 < 1 s)
- RP2350B ARM Secure entry point confirmed by `picotool` inspection
- `ghostwisp` board definition accepted by Pico SDK (UART1 pins 36/37, 16 MiB QSPI)

---

## 10. Unresolved Rev A hardware assumptions

The following items are documented but **not yet resolved for Rev A**:

1. A/B partition geometry is not frozen — offsets and sizes are placeholders
2. Immutable-key policy and OTP/eFuse configuration for signature verification
3. Brownout circuit design and verified threshold voltage on the 3.3 V rail
4. Final safe-state polarities for radio disable and power control pins
5. Validated SPI bus assignment: three named buses (CC1101/NFC/microSD) share
   two hardware SPI controllers; the actual routing must be verified on board
6. Rev A PCB not yet available — all hardware validation is on hold until first
   build and bring-up

These items are tracked in `docs/pcb-layout-review.md` and will gate the
`GhostWisp firmware — core boot sequence` implementation task.
