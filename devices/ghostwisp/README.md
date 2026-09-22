# GhostWisp

**Project Little Spectre — GhostBlade's smaller sister device**

**Author: jayis1**

GhostWisp is a pocketable, microcontroller-first hacker companion that lives inside the GhostBlade repository as a quiet sister project. GhostBlade is a Linux computer and mobile lab; GhostWisp is deliberately simpler: instant-on, deterministic, inexpensive, and focused on playful physical interfaces.

It is not a separate Git branch. The project lives at `devices/ghostwisp/` on the repository's default branch so its design cannot drift or disappear on a long-lived branch.

## Character

- **GhostBlade:** powerful computer, wideband SDR, Linux tools, local AI, large screen.
- **GhostWisp:** small appliance, no Linux, no package manager, no cloud requirement, physical controls, fast boot.
- **Together:** GhostWisp can act as a pocket remote, real-time I/O probe, and field accessory for GhostBlade.

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

## GhostBlade companion mode

Over USB, GhostWisp can:

- expose sanitized captures and device metadata;
- accept signed profiles and firmware updates;
- perform timing-sensitive I/O while GhostBlade handles analysis;
- act as a detachable control surface;
- stream low-rate sub-GHz/NFC/IR observations;
- use a versioned, length-bounded protocol with integrity checks and explicit capability negotiation.

GhostWisp must remain useful when GhostBlade is absent.

## Repository layout

```text
devices/ghostwisp/
├── README.md
└── docs/
    ├── architecture.md
    └── roadmap.md
```

Future work should add `hardware/`, `firmware/`, `tests/`, and `tools/` only when each contains buildable or verifiable artifacts.

## Definition of success

A first usable revision should boot in under two seconds, navigate without a host, safely inspect at least one sub-GHz protocol, read an NFC tag, learn an IR remote, provide a USB serial/bus-console mode, save captures to microSD, and exchange a versioned status message with GhostBlade.

## License

Unless a file states otherwise, hardware follows the repository's CERN-OHL-S-2.0 terms, firmware follows GPL-2.0-or-later, and documentation follows CC-BY-SA-4.0.
