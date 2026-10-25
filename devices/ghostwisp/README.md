# GhostWisp

**Project Little Spectre — GhostBlade's smaller sister device**

**Author: jayis1**

GhostWisp is a pocketable, microcontroller-first hacker companion that lives inside the GhostBlade repository as a quiet sister project. GhostBlade is a Linux computer and mobile lab; GhostWisp is deliberately simpler: instant-on, deterministic, inexpensive, and focused on playful physical interfaces. They are designed as one highly interconnected system: GhostBlade supplies compute, analysis, storage, networking, and rich UI while GhostWisp supplies detachable real-time sensing, radio/NFC/IR interaction, physical controls, and hardware I/O.

It is not a separate Git branch. The project lives at `devices/ghostwisp/` on the repository's default branch so its design cannot drift or disappear on a long-lived branch.

## Character

- **GhostBlade:** powerful computer, wideband SDR, Linux tools, local AI, large screen.
- **GhostWisp:** small appliance, no Linux, no package manager, no cloud requirement, physical controls, fast boot.
- **Together:** they form a mothership-and-edge pair. GhostWisp is GhostBlade's detachable hands, ears, and control surface; GhostBlade is GhostWisp's analysis engine, long-term memory, network gateway, and high-level orchestrator.

## Target capabilities

| Capability | Intended use |
|---|---|
| CC1101 sub-GHz | Receive, inspect, classify, and replay signals in an authorized lab |
| ST25R3916 NFC | Read tags, inspect protocols, emulate test fixtures, and exchange data with GhostBlade |
| IR transmitter/receiver | Learn and replay remotes for owned equipment |
| USB-C device/host | Serial console, HID test profiles, firmware update, and GhostBlade companion link |
| UART/SPI/I²C/GPIO header | Portable bus console, logic-level exploration, and hardware bring-up |
| microSD | Store captures, scripts, device profiles, and signed firmware packages |
| Small color display + buttons | Menu-driven operation without a phone or computer |
| RGB LEDs, buzzer, vibration | Fun, immediate feedback that can be disabled for quiet operation |

All active transmission, replay, emulation, or HID behavior must be explicit, visibly armed, bounded, and intended only for systems the operator owns or is authorized to test.

## Deliberate omissions

GhostWisp does **not** try to become another GhostBlade:

- no Linux SoC;
- no wideband SDR;
- no local LLM;
- no desktop-style multitasking;
- no invisible autonomous attacks;
- no assumption that every radio feature belongs in the first revision.

## Proposed Rev A hardware

- RP2350B primary MCU
- 16 MiB QSPI flash
- 2.4-inch 320×240 IPS display
- Five-way navigation control plus two action buttons
- CC1101 with a switchable regional front end and removable antenna
- ST25R3916 with rear NFC loop
- 940 nm IR LED, current-limited driver, and demodulating IR receiver
- USB-C with ESD protection and explicit host/device power control
- microSD over SPI
- 3.3 V expansion header exposing UART, SPI, I²C, GPIO, ground, and current-limited power
- 1200–1800 mAh protected Li-Po, fuel gauge, USB charging, and hard power switch
- Optional vibration motor, piezo buzzer, and addressable status LEDs

Component choices remain provisional until the architecture, power budget, pin allocation, and regional RF constraints agree.

## Software model

Firmware is a small event-driven appliance rather than a general-purpose OS:

1. signed boot and recovery path;
2. board-support and power management;
3. capability drivers;
4. a permission-aware action broker;
5. menu applications;
6. capture/profile storage;
7. a versioned GhostBlade companion protocol.

Risk-bearing actions require a physical confirmation gesture and show frequency, target mode, duration, and region/profile before execution. A global radio-disable setting and a receive-only mode are first-class features.

## GhostBlade interconnection

The interconnection is a primary product feature, not an optional accessory mode. USB-C is the baseline data, power, update, and recovery link; later revisions may add a mechanically keyed dock or short-range authenticated transport without changing the application protocol.

GhostBlade remains frozen in its current form and behavior. Integration work adds a backward-compatible companion feature around it; it must not redesign, remove, rename, or regress existing GhostBlade hardware, firmware, drivers, APIs, tools, or workflows.

When connected, the pair can:

- expose sanitized captures and device metadata;
- accept signed profiles and firmware updates;
- perform timing-sensitive I/O while GhostBlade handles analysis;
- act as a detachable control surface;
- stream sub-GHz/NFC/IR observations into GhostBlade tools and storage;
- let GhostBlade correlate observations, prepare bounded actions, and return human-readable explanations;
- synchronize clock, device profiles, region policy, capture indexes, and audit records;
- route GhostWisp data into GhostBlade's `libapex`/Python-facing tooling through a dedicated companion service;
- use a versioned, length-bounded protocol with integrity checks, authenticated pairing, explicit capability negotiation, reconnect/resume, and visible degraded-state handling.

GhostWisp retains a reduced offline toolkit when detached, but its full workflow, analysis depth, storage, profile management, and cross-capability automation come from its close connection to GhostBlade. Risk-bearing actions use dual consent: GhostBlade may prepare and request an action, but GhostWisp requires local physical confirmation before execution.

See [GhostBlade integration](docs/ghostblade-integration.md) for the shared architecture and interface contract.

## Repository layout

```text
devices/ghostwisp/
├── README.md
├── Makefile                (host test build + automated check suite)
├── check.sh                (isolation, header, and cross-compile checks)
├── docs/
│   ├── architecture.md
│   ├── boot-contract.md    (boot ordering, watchdog, GPIO, recovery, HIL requirements)
│   ├── ghostblade-integration.md
│   ├── pcb-layout-review.md
│   ├── peripheral-drivers.md
│   ├── rf-engineering.md
│   └── roadmap.md
├── firmware/
│   └── rp2350b/
│       ├── CMakeLists.txt  (Pico SDK cross-build: produces ghostwisp.uf2/.elf/.hex)
│       ├── include/
│       │   ├── boards/ghostwisp.h
│       │   ├── ghostwisp_boot.h
│       │   ├── ghostwisp_gpio.h
│       │   ├── ghostwisp_i2c.h
│       │   ├── ghostwisp_pins.h
│       │   ├── ghostwisp_protocol.h
│       │   ├── ghostwisp_spi.h
│       │   └── ghostwisp_uart.h
│       └── src/
│           ├── ghostwisp_boot.c
│           ├── ghostwisp_platform.c
│           ├── ghostwisp_gpio.c
│           ├── ghostwisp_i2c.c
│           ├── ghostwisp_spi.c
│           ├── ghostwisp_uart.c
│           └── main.c
├── protocol/
│   ├── __init__.py
│   ├── frame.py            (Python codec — canonical reference implementation)
│   ├── schema.py           (payload schemas for all message families)
│   └── golden_vectors.py   (frozen wire-format test vectors)
└── tests/
    ├── test_ghostwisp_boot.c  (157 host-side boot contract assertions — run with `make test`)
    └── test_protocol.py       (36 unit tests: CRC, round-trip, error handling, safety invariants)
```

**Boot checks:** `cd devices/ghostwisp && make check`
runs host boot tests, GhostBlade isolation checks, header sanity checks, and
all host Python protocol/profile tests. Python 3 is required; override its
executable with `PYTHON=/path/to/python3`. Run only the build-free Python suite
with `make -C devices/ghostwisp protocol-test` from the repository root.
Set `PICO_SDK_PATH=...` to also probe the cross-compile.

The frame-boundary regressions exercise the length-bounded, integrity-checked
companion contract in the interconnection requirements above: every truncated
prefix of empty, ordinary, and maximum-size frames, CRC-valid trailing data,
and every single-bit corruption of a representative frame. CRC results are
cross-checked against Python's independent `binascii.crc_hqx` implementation
with the CCITT-FALSE initial value. These tests access no hardware and do not
establish pairing, authenticated integrity, stream reassembly, or physical
confirmation enforcement.
See [boot-contract.md](docs/boot-contract.md) for what requires physical-board HIL verification.

### Profile header validation

Author: jayis1

The existing profile wire format in `protocol/profile.py` requires its reserved
byte and reserved 16-bit word to be zero. `ProfileHeader.decode()` rejects
nonzero values with `ProtocolError` before returning a parsed header, even when
the enclosing frame has a valid CRC. Conforming encodings and unknown profile
flag preservation are unchanged. Header decoding still permits following body
bytes; callers must validate the header with `ProfileValidator.validate()`
before accepting the body and separately verify body integrity and signatures.
CRC is not authentication.

Run `python3 -m unittest devices.ghostwisp.tests.test_profile_reserved -v` from
the repository root. These host regressions cover every nonzero reserved-byte
value, every reserved-word bit and the all-bits-set value, immutable and mutable
buffers, all truncated header prefixes, CRC-valid malformed profile envelopes,
and conforming headers with following body bytes. They do not verify physical
hardware, signature enforcement, or a complete profile-transfer service.

### Complete profile body integrity

Author: jayis1

After decoding and validating the header, callers can now use
`ProfileValidator.validate_body(header, body, now_utc_ms=...)` to check the
complete reassembled body. It returns a `ProfileValidationResult`: reject
`ok=False` without storing or using a partial profile. Header validation runs
first, then the exact declared length, then CRC-32/ISO-HDLC against the existing
`body_crc32` field. The 50 KiB header limit therefore applies before checksum
work. Empty bodies are valid when declared length and CRC are both zero.
Existing header validation and wire encodings are unchanged.

The checksum covers only the body, not its header. Its standard check value is
`0xCBF43926` for ASCII `123456789`; parameters are polynomial `0x04C11DB7`,
initial value `0xFFFFFFFF`, reflected input/output, and final XOR `0xFFFFFFFF`.
Callers must still bound receive buffers, authenticate the header and body,
verify signatures, and commit storage atomically. Do not mutate a `bytearray`
during validation or between validation and use. Supply synchronized UTC time
for expiry checking; the existing `now_utc_ms=0` convention skips that check.
This helper neither receives transfers nor authenticates, executes, or installs
a profile.

Run `python3 -m unittest devices.ghostwisp.tests.test_profile_body -v` from the
repository root, or run `make -C devices/ghostwisp check` for the full host suite.
Regressions include the independent checksum check value, empty and 50 KiB
bodies, every truncated prefix and single-bit corruption of the check string,
trailing data, wrong CRCs, expiry boundaries, and rejection before checksum
work for invalid headers or mismatched lengths. No hardware is accessed.

Build the RP2350B image with Pico SDK 2.0 or newer. The committed `ghostwisp`
board definition selects RP2350B, 16 MiB QSPI, and UART1 on GPIO36/GPIO37:

```sh
cmake -S devices/ghostwisp/firmware/rp2350b -B build/ghostwisp \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build build/ghostwisp
```

The build emits `ghostwisp.elf`, `.uf2`, `.hex`, `.bin`, and `.map`. Signature
verification is deliberately fail-closed until the signed-update module supplies
its verifier; unsigned images remain in radio-disabled USB recovery.

## Definition of success

A first usable revision should boot in under two seconds, provide a reduced detached UI, safely inspect at least one sub-GHz protocol, read an NFC tag, learn an IR remote, and provide a USB serial/bus-console mode. In connected mode it must pair with GhostBlade, negotiate capabilities, synchronize time and policy, stream a capture into GhostBlade storage, accept a bounded profile, survive disconnect/reconnect, and complete a dual-consent action request with an auditable result.

## License

Unless a file states otherwise, hardware follows the repository's CERN-OHL-S-2.0 terms, firmware follows GPL-2.0-or-later, and documentation follows CC-BY-SA-4.0.
