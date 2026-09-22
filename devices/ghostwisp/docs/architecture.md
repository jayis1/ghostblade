# GhostWisp Architecture

**Author: jayis1**

## Design rule

GhostWisp is a bounded instrument, not a small computer. Every feature must fit an RP2350B-class event-driven system, have an explicit user-facing state, and fail safely when storage, radio, or the companion host is unavailable.

## Functional blocks

```text
Protected Li-Po
    │
 Charger / fuel gauge ── hard power switch
    │
 3.3 V rail ───────────────┬───────────────┬───────────────┐
    │                       │               │               │
 RP2350B                CC1101          ST25R3916       Display/UI
    │                       │               │               │
 QSPI flash + microSD   Sub-GHz RF      NFC loop       Buttons/feedback
    │
 USB-C + expansion header + IR TX/RX
```

## Trust boundaries

- **User confirmation boundary:** transmit, replay, emulation, and HID actions require physical confirmation.
- **Profile boundary:** imported profiles are data, never native code; parsers are length-bounded and versioned.
- **USB boundary:** companion commands are capability-scoped and cannot silently arm a radio or HID action.
- **Storage boundary:** corrupt or absent microSD media must not prevent safe boot or recovery.
- **RF boundary:** regional profiles constrain frequency and power; receive-only mode overrides every application.

## Firmware partitions

1. Immutable recovery metadata and public verification key.
2. A/B application images with rollback counter.
3. Settings with schema version and checksum.
4. Capture/profile storage on removable media.

## Application model

Applications request operations through an action broker. The broker owns radio state, USB role, confirmation requirements, timeouts, and audit metadata. Drivers never directly expose unbounded transmit loops to menu applications.

Initial applications:

- spectrum activity meter for CC1101-supported bands;
- sub-GHz packet viewer and authorized replay tool;
- NFC tag inspector and test-fixture emulator;
- IR learner and remote organizer;
- UART terminal and I²C/SPI probe;
- USB serial/HID test profiles;
- GhostBlade companion status and capture transfer.

## Companion protocol principles

- fixed sync word and protocol version;
- length-bounded frames;
- CRC for transport integrity;
- monotonically increasing request identifier;
- capability negotiation;
- explicit arm/confirm/execute state machine for active operations;
- no arbitrary memory access, shell, or native-code upload;
- signed firmware update flow separate from normal commands.

The detailed frame format is intentionally deferred until use cases, maximum payloads, and USB transport choice are frozen.

## Power states

| State | Expected behavior |
|---|---|
| Off | Hard switch isolates normal load; charger may remain active |
| Deep sleep | RTC/button wake, radios and display off |
| Standby | Display dimmed, receive tasks optional |
| Active | UI and selected peripheral powered |
| Armed | Time-bounded active operation awaiting final confirmation |
| Recovery | USB update only; radios disabled |

## Rev A engineering gates

Before schematic capture begins, freeze:

- power budget and battery-life target;
- RP2350B pin allocation;
- SPI bus sharing and chip-select strategy;
- USB host/device requirements;
- RF regional constraints and front-end variants;
- antenna separation and enclosure assumptions;
- NFC loop geometry;
- display and input part availability;
- safe update and recovery behavior.
