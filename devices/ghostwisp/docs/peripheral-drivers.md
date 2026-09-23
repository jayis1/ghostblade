# GhostWisp Peripheral Driver Stack

Developer reference for the GhostWisp RP2350B peripheral driver stack:
GPIO, I2C, SPI, and UART. This document consolidates the public API of
the four drivers that live under
`devices/ghostwisp/firmware/rp2350b/`.

All drivers follow the same conventions:

- **Namespaced to GhostWisp.** Sources and headers are prefixed
  `ghostwisp_*` and live under the GhostWisp firmware tree; they do not
  touch or alter any GhostBlade code.
- **Host-testable.** When compiled with `-DDRIVER_HOST_TEST`, each driver
  swaps its register-level MMIO for an in-process hardware simulation so
  the logic runs and is unit-tested on the build host. Without the macro,
  the driver compiles to real RP2350B register accesses.
- **Explicit result codes.** Every fallible entry point returns a typed
  result enum (`*_OK == 0`, negative on error) rather than `errno`.
- **Blocking transfers.** Bus reads/writes are blocking with a bounded
  microsecond timeout; there is no implicit DMA in this layer.

See `architecture.md` for the SoC overview and `../firmware/rp2350b/include/ghostwisp_pins.h`
for the authoritative pin map.

---

## GPIO — `ghostwisp_gpio.{h,c}`

SIO pin control, function muxing, pull configuration, edge IRQ dispatch,
and debounced reads for the seven user buttons.

| Function | Purpose |
|----------|---------|
| `gpio_init(void)` | Initialize the GPIO block and button inputs. |
| `gpio_set_function(pin, func)` | Select pin function (SIO, SPI, I2C, UART, PWM, …). |
| `gpio_set_dir(pin, dir)` / `gpio_get_dir(pin)` | Set/query input vs. output. |
| `gpio_set_pull(pin, pull)` | None / pull-up / pull-down. |
| `gpio_put(pin, value)` / `gpio_get(pin)` / `gpio_toggle(pin)` | Drive / read / toggle a pin. |
| `gpio_set_irq_enabled(pin, events, enable)` | Enable/disable edge or level IRQ sources. |
| `gpio_set_irq_callback(cb)` / `gpio_handle_irq()` / `gpio_acknowledge_irq(pin)` | Register a callback, dispatch pending IRQs, ack a pin. |
| `gpio_button_pressed(button)` | Debounced logical button state. |
| `gpio_button_raw(button)` / `gpio_button_pin(button)` | Undebounced state / physical pin for a button. |
| `gpio_is_initialized(void)` | Init guard. |

Buttons: `BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_CENTER, BTN_A,
BTN_B` (`BTN_COUNT == 7`).

---

## I2C — `ghostwisp_i2c.{h,c}`

I2C0 master on the DesignWare (Synopsys) controller. Default target is
the MAX17048 fuel gauge at 7-bit address `0x36`
(`I2C_ADDR_FUEL_GAUGE`), 400 kHz Fast Mode (`I2C0_DEFAULT_BAUD`). Boot
phase 7 (battery check) reads the fuel gauge through this driver.

| Function | Purpose |
|----------|---------|
| `i2c_init(instance, baud_hz)` | Configure and enable I2C0 (only `I2C_INSTANCE_0`). |
| `i2c_write_blocking(addr, data, len)` | Write `len` bytes to a 7-bit address. |
| `i2c_read_blocking(addr, data, len)` | Read `len` bytes from a 7-bit address. |
| `i2c_write_register(addr, reg, val)` | Write one 8-bit register. |
| `i2c_read_register(addr, reg, out)` | Read one 8-bit register. |
| `i2c_read_register16(addr, reg, out16)` | Read a 16-bit big-endian register (MAX17048 style). |
| `i2c_probe(addr)` | Address-only probe; `I2C_OK` if the slave ACKs. |
| `i2c_deinit(void)` | Disable the controller. |
| `i2c_is_initialized()` / `i2c_get_baud()` / `i2c_get_error_count()` | Introspection. |

Result codes: `I2C_OK`, `I2C_ERR_INVALID_ARG`, `I2C_ERR_NOT_INIT`,
`I2C_ERR_NACK`, `I2C_ERR_TIMEOUT`, `I2C_ERR_BUSY`, `I2C_ERR_TOO_LONG`,
`I2C_ERR_BUS_FAULT`.

---

## SPI — `ghostwisp_spi.{h,c}`

Three independent SPI master buses with software chip-select:

| Instance | Peripheral | Pins (SCK/MOSI/MISO/CSn) | Max clock |
|----------|-----------|--------------------------|-----------|
| `SPI_INSTANCE_0` | CC1101 sub-GHz radio | 6 / 7 / 8 / 9 | 10 MHz |
| `SPI_INSTANCE_1` | ST25R3916 NFC | 12 / 13 / 14 / 15 | 10 MHz |
| `SPI_INSTANCE_2` | microSD card | 17 / 18 / 19 / 20 | 20 MHz |

| Function | Purpose |
|----------|---------|
| `spi_init(instance, mode, baud_hz, ...)` | Configure a bus (SPI modes 0–3). |
| `spi_write_blocking(instance, data, len)` | Write only. |
| `spi_read_blocking(instance, data, len)` | Read only. |
| `spi_transfer_blocking(instance, ...)` | Full-duplex transfer. |
| `spi_write_register(instance, reg, val)` / `spi_read_register(instance, reg, out)` | Register access. |
| `spi_cs_select(instance)` / `spi_cs_deselect(instance)` | Manual software CS. |
| `spi_set_baud(instance, baud_hz)` / `spi_get_baud(instance)` | Clock control. |
| `spi_is_initialized(instance)` / `spi_deinit(instance)` | Lifecycle. |
| `spi_get_instance_base(instance)` / `spi_get_instance_cs_pin(instance)` | Introspection. |

---

## UART — `ghostwisp_uart.{h,c}`

UART0 with ring-buffered interrupt RX and blocking TX. Default config is
115200 8N1 (`uart_init_default`). The debug console is routed to the
expansion-header pins (TX 35 / RX 36) so the fuel-gauge I2C0 bus stays
available.

`uart_config_t` fields: `baud_hz`, `word_len` (7/8), `parity`
(none/even/odd), `stop_bits` (1/2), `flow_control` (RTS/CTS).

| Function | Purpose |
|----------|---------|
| `uart_init(instance, config)` / `uart_init_default(instance)` | Configure UART0. |
| `uart_write_blocking(instance, data, len)` / `uart_read_blocking(instance, data, len)` | Byte-buffer TX/RX. |
| `uart_write_string(instance, str)` / `uart_write_char(instance, ch)` / `uart_read_char(instance, ch)` | Convenience TX/RX. |
| `uart_rx_available(instance)` / `uart_rx_flush(instance)` | RX ring-buffer state. |
| `uart_set_baud(instance, baud_hz)` / `uart_get_baud(instance)` | Baud control. |
| `uart_handle_rx_irq(instance)` | RX ISR entry point. |
| `uart_is_initialized(instance)` / `uart_deinit(instance)` / `uart_get_error_count(instance)` | Lifecycle / stats. |

---

## Building and testing

The drivers build into the RP2350B firmware image via
`devices/ghostwisp/firmware/rp2350b/CMakeLists.txt` (Pico SDK + the
`toolchain-arm-none-eabi.cmake` toolchain file).

Host-side unit tests live in `tests/` and require no hardware:

```sh
cd tests
make test_ghostwisp_peripherals && ./test_ghostwisp_peripherals   # 251 tests
make test_ghostwisp_boot        && ./test_ghostwisp_boot          # 141 tests
make check                                                        # full suite
```

The peripheral suite (`tests/test_ghostwisp_peripherals.c`) covers init,
invalid-argument handling, blocking transfers, register access, NACK
handling, software CS control, UART loopback, GPIO IRQ dispatch, button
debounce, and cross-driver integration.
