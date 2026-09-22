# GhostWisp Architecture

**Author: jayis1**

## Design rule

GhostWisp is a bounded instrument, not a small computer. Every feature must fit an RP2350B-class event-driven system, have an explicit user-facing state, and fail safely when storage, radio, or the GhostBlade link is unavailable. The normal high-capability operating model is nevertheless a tightly coupled two-device system rather than two independent products.

## System-of-two architecture

GhostBlade owns compute-heavy analysis, rich visualization, networking, large storage, profile authoring, fleet history, and orchestration. GhostWisp owns deterministic timing, physical controls, nearby RF/NFC/IR interaction, bus access, and final local confirmation of active operations. Neither silently assumes the other's responsibilities.

GhostBlade is a frozen baseline. Companion support must be additive and backward-compatible: new service/modules, namespaced APIs, schemas, UI surfaces, and tests may be added, but existing GhostBlade functions and physical design must continue to operate unchanged when GhostWisp is absent.

The shared system must expose connection state as `detached`, `pairing`, `connected`, `degraded`, `armed`, or `recovery`. Applications must never display a stale connected result as live.

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
- authenticated device pairing and persistent device identity;
- time, region-policy, profile-index, and audit-record synchronization;
- resumable transfer with explicit disconnect/reconnect behavior;
- explicit arm/confirm/execute state machine for active operations;
- request preparation on GhostBlade plus physical confirmation on GhostWisp;
- no arbitrary memory access, shell, or native-code upload;
- signed firmware update flow separate from normal commands.

The application protocol must remain transport-neutral, with USB-C vendor bulk plus CDC diagnostics as the Rev A baseline. The detailed frame format is defined in `ghostblade-integration.md` and must evolve together with the corresponding GhostBlade host service and tests.

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
