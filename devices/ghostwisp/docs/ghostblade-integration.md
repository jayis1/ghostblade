# GhostBlade–GhostWisp Integration

**Author: jayis1**

GhostBlade and GhostWisp are two halves of one field system. GhostBlade is the compute, analysis, storage, networking, and orchestration half. GhostWisp is the detachable deterministic I/O, nearby-radio, NFC, IR, bus-access, and physical-consent half.

Detached GhostWisp operation exists for convenience and resilience, but the complete product experience assumes frequent, deep interconnection.

## Frozen GhostBlade baseline

GhostBlade does not change from its current product definition. GhostWisp integration is a new, optional, backward-compatible companion feature. Work must not redesign GhostBlade hardware, replace existing subsystems, rename or remove existing interfaces, alter established behavior for users without GhostWisp, or reopen completed GhostBlade features.

Permitted GhostBlade-side work is additive and namespaced: a companion daemon, new companion-facing library/API surfaces, shared schemas, UI panels, tests, documentation, and narrowly scoped compatibility hooks. With no GhostWisp attached, GhostBlade must behave exactly as it did before the companion feature was added.

## Responsibility split

| System responsibility | GhostBlade | GhostWisp |
|---|---|---|
| Rich UI and visualization | Primary | Compact status and confirmation UI |
| Compute-heavy decoding and correlation | Primary | Pre-filtering and timestamping |
| Long-term capture storage | Primary | Short rolling buffer and transfer queue |
| Network and remote integrations | Primary | No independent cloud requirement |
| Profile authoring and policy management | Primary | Validates and enforces received bounded profiles |
| Sub-GHz/NFC/IR timing | Supervises | Primary real-time executor |
| UART/SPI/I²C/GPIO physical access | Displays and analyzes | Primary electrical interface |
| Active-operation preparation | Builds a bounded request | Displays exact request and requires local confirmation |
| Audit history | Canonical merged history | Tamper-evident local queue until synchronized |
| Firmware update | Verifies compatibility and delivers signed image | Verifies signature and controls A/B rollback |

## Physical and transport relationship

Rev A uses USB-C as the baseline interconnect:

- USB vendor bulk endpoints for framed control and capture transfer;
- USB CDC ACM for bounded diagnostics and recovery logs;
- negotiated USB power direction with explicit current limits;
- device-mode operation by default, with any host-mode use separately gated;
- ESD protection and attach/detach detection on both devices.

The application protocol is transport-neutral so a keyed dock or authenticated short-range transport can be added later. Alternate transports must preserve identity, policy, consent, replay protection, and audit semantics.

## Shared connection state

Both devices expose the same state machine:

```text
detached → pairing → connected → degraded
                ↘ recovery
connected → armed → executing → result → connected
```

- **detached:** reduced local GhostWisp features remain available.
- **pairing:** identities and protocol compatibility are established.
- **connected:** capabilities, clock, policy, and indexes are synchronized.
- **degraded:** link or subsystem failure is visible; stale data is never shown as live.
- **armed:** GhostBlade has prepared a bounded action and GhostWisp is showing it for confirmation.
- **executing:** time-bounded physical operation is in progress.
- **result:** outcome and measurements are committed to both audit queues.
- **recovery:** signed update and diagnostics only; active interfaces remain disabled.

## Session establishment

A connected session should perform these steps in order:

1. exchange protocol versions, stable device identities, nonces, and capability manifests;
2. authenticate the paired device and reject identity changes unless the user explicitly repairs;
3. negotiate maximum frame and transfer sizes;
4. synchronize UTC time and monotonic-offset metadata;
5. reconcile region and safety policy, choosing the stricter effective rule;
6. reconcile profile, capture, firmware, and audit indexes;
7. advertise live subsystem health;
8. enter `connected` only after both devices agree on the effective session contract.

Pairing and session authentication details remain to be selected with the secure-storage components. Secrets must never be embedded in repository artifacts or ordinary profiles.

## Protocol envelope

The initial logical envelope is:

```text
magic | protocol_version | message_type | flags | request_id
payload_length | session_id | monotonic_timestamp | payload | CRC
```

Requirements:

- fixed-width little-endian header;
- bounded payload selected during negotiation;
- CRC for accidental corruption, not authentication;
- unique session identifier and monotonically increasing request identifiers;
- idempotency classification for every command;
- explicit response, timeout, cancellation, and partial-transfer behavior;
- unknown mandatory fields fail closed;
- version skew produces a visible compatibility result rather than silent downgrade.

Authenticated integrity belongs at the session/message security layer and must not be conflated with CRC.

## Message families

| Family | Examples |
|---|---|
| Discovery | identity, version, capabilities, health |
| Synchronization | time, region policy, profile index, capture index, audit cursor |
| Observation | sub-GHz packet, NFC metadata, IR frame, bus sample, button event |
| Transfer | capture chunks, profile chunks, signed firmware chunks |
| Control | display status, start bounded receive, stop, cancel, request confirmation |
| Active action | arm, confirm locally, execute, abort, result |
| Diagnostics | counters, reset cause, battery, temperatures, storage health |

No message family provides arbitrary shell execution, arbitrary memory access, or native-code upload.

## Dual-consent active operations

For RF transmission, replay, NFC emulation, or USB HID behavior:

1. GhostBlade prepares a fully bounded action containing mode, target context, duration, region/profile, and expected effect.
2. GhostWisp validates the action against local capability and safety policy.
3. GhostWisp shows the exact action on its own display.
4. The user confirms physically on GhostWisp before a short deadline.
5. GhostWisp executes within fixed time and resource bounds.
6. Either device may abort.
7. Result, measurements, and abort reason are written to both audit streams.

A GhostBlade request alone never constitutes confirmation.

## Shared software surfaces

Planned GhostBlade-side components:

- a new namespaced companion daemon for discovery, pairing, synchronization, transfers, and reconnect;
- a narrow additive `libapex`/Python companion interface for status, observations, capture retrieval, and bounded action preparation;
- optional UI panels showing connection, effective policy, GhostWisp health, live observations, pending confirmation, and transfer state;
- shared schema definitions and golden protocol vectors consumed by both host and firmware tests.

These additions must preserve existing ABI/API behavior and remain dormant when GhostWisp is absent.

Planned GhostWisp-side components:

- USB transport and session state machine;
- capability and health registry;
- bounded transfer queue;
- synchronized policy/profile store;
- action broker and physical-confirmation UI;
- signed update receiver and A/B rollback;
- durable audit queue that reconciles after reconnect.

## Required integration tests

- first pairing and explicit re-pair after identity change;
- compatible and incompatible protocol versions;
- disconnect during observation, profile transfer, firmware transfer, armed state, and execution;
- duplicate and out-of-order request identifiers;
- corrupted, oversized, truncated, and unknown messages;
- clock drift and conflicting region policy;
- capture-index and audit-cursor reconciliation;
- GhostBlade restart while GhostWisp remains attached;
- GhostWisp reset while GhostBlade remains active;
- dual-consent success, timeout, local rejection, and abort from either side;
- degraded UI state with no stale-success presentation.

## First vertical slice

The first complete integration slice should be intentionally narrow:

1. GhostWisp enumerates over USB and reports identity, version, battery, and capability metadata.
2. GhostBlade companion service authenticates the known device and synchronizes time and region policy.
3. GhostWisp records a receive-only sample or synthetic test observation.
4. The observation streams to GhostBlade and is stored with synchronized timestamps.
5. GhostBlade returns an analysis annotation.
6. Both devices show the same capture identifier and connected health state.
7. Disconnect/reconnect resumes without duplicating the capture.

Only after this slice is tested should remote preparation of active actions be implemented.
