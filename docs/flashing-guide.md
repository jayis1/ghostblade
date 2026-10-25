<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 jayis1 -->
<!-- Author: jayis1 -->

# Flashing and target deployment

Author: jayis1

These are deployment templates, not a verified board procedure. No editable
schematic or routed PCB is tracked; host tests do not prove that USB BOOTSEL,
SWD, reset routing, target boot configuration, or peripheral power works.
See [hardware readiness](hardware-export-readiness.md) before using a prototype.
Do not run target commands on the workstation.

## Preflight: do not write until these are known

1. Identify the exact board revision and target processor. Review its schematic,
   flash configuration, power/reset requirements, and debug connection.
2. Select reviewed firmware/kernel/SDK revisions and record tool versions. Build
   artifacts from those sources; the repository does not supply a validated
   whole-device operating-system image.
3. Back up the current firmware where readable, boot configuration, installed
   DTB/overlays, and module. Verify a recovery path independent of the image
   being replaced. Stop if no recovery path is available.
4. Record SHA-256 hashes of artifacts before transfer and compare them on the
   target. Hash equality detects transfer changes, not authenticity or hardware
   suitability; trust must come from the reviewed source/release.
5. Confirm which USB device or SSH host is the prototype. Disconnect other debug
   targets when tool selection is ambiguous. Do not guess a block-device name,
   UF2 mountpoint, or flash offset.

Build instructions and prerequisite checks are in
[Build Instructions](build-instructions.md). RP2350B firmware and the RK3576
Linux module are separate targets, not interchangeable images.

## RP2350B firmware

Expected build artifacts are `firmware/rp2350b/build/ghostblade.uf2` and
`ghostblade.elf`. These names come from the CMake target; their presence alone
is not a completed validation. On the workstation, record the selected image:

```sh
sha256sum firmware/rp2350b/build/ghostblade.uf2
```

### BOOTSEL or picotool

Use the reviewed board procedure to enter the RP2350 boot ROM's USB mode.
Check enumeration with `picotool info` and the host's USB/mount tools before
writing. The volume name and mountpoint must be discovered locally; do not
assume the RP2040 `RPI-RP2` path applies to an RP2350 board.

After confirming the selected device, the write template is:

```sh
picotool load -v firmware/rp2350b/build/ghostblade.uf2 &&
  picotool reboot
```

Use a version supporting RP2350 and check `picotool help load` for its verify
option. `-v` requests verification; do not reboot after a failed load/verify.
Alternatively, copy the UF2 to the confirmed boot-ROM volume using its actual
mountpoint. This overwrites firmware and may reboot the MCU automatically.
Neither method validates board configuration or runtime behavior.

### OpenOCD / SWD

Only after checking probe voltage, ground, SWD wiring, reset behavior, and target
selection against the reviewed revision:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "program firmware/rp2350b/build/ghostblade.elf verify reset exit"
```

This template assumes a CMSIS-DAP probe and an RP2350-capable OpenOCD installation.
Use the correct probe configuration for the actual setup. Programming overwrites
flash; `verify` checks written data, not electrical safety or correct behavior.

After a successful write, inspect serial logs and the documented handshake/reset
signals with appropriate instruments. Keep RF transmit paths disabled until
power, clocks, pin assignments, and frontend configuration have been reviewed.

## Device-tree deployment

A normal local build without `rk3576.dtsi` produces optional overlays only and
explicitly skips the base DTB:

```sh
make -C software/dts all validate
```

For a base build, supply the reviewed kernel's SoC include and include roots as
shown in [Device Tree build instructions](build-instructions.md#device-tree).
Before deployment, verify the actual base output exists and record its hash:

```sh
test -s software/dts/ghostblade-rk3576.dtb
sha256sum software/dts/ghostblade-rk3576.dtb
```

Do not treat an old file left in the build directory as a current build. Clean
before switching kernel/include/tool selections. Validate bindings, exported
symbols, overlay application, and mutually compatible optional configurations
against the exact intended base tree; overlay syntax compilation alone is not
sufficient.

There is no universal `/boot/dtbs/` install path or configfs overlay interface.
Choose the target bootloader/distribution's documented selection mechanism,
back up its files/configuration, and preserve a bootable recovery entry. Merely
copying a DTB into `/boot` does not select it. Runtime overlay support is
kernel-dependent and must not be assumed. No automatic DTB/overlay installation
command is supplied until that target configuration is established.

## RK3576 kernel module

Build `software/linux-drivers/apex_bridge.ko` against the configured target kernel
build tree, architecture, and module settings, not the workstation kernel.
For a cross-built module, configure an SSH host alias `ghostblade-target` for the
prototype and transfer the artifact as the target user:

```sh
sha256sum software/linux-drivers/apex_bridge.ko
scp software/linux-drivers/apex_bridge.ko ghostblade-target:apex_bridge.ko
ssh ghostblade-target 'sha256sum "$HOME/apex_bridge.ko"; uname -r; modinfo -F vermagic "$HOME/apex_bridge.ko"'
```

Compare hashes and kernel versions before installation. This transfer overwrites
`$HOME/apex_bridge.ko`; choose another staging filename if it already holds a
backup. The release check below is necessary but not sufficient: matching
vermagic does not prove symbol-version, signing-policy, or driver compatibility.
After backup and review, install on the target:

```sh
ssh ghostblade-target 'set -eu
module="$HOME/apex_bridge.ko"
release=$(uname -r)
test -s "$module"
vermagic=$(modinfo -F vermagic "$module")
test "${vermagic%% *}" = "$release"
sudo install -d "/lib/modules/$release/extra"
sudo install -m 0644 "$module" "/lib/modules/$release/extra/apex_bridge.ko"
sudo depmod -a "$release"
'
```

The single-quoted SSH command evaluates `uname -r` on the target, not on the
workstation. `set -eu` stops before installation on missing inputs or a release
mismatch. Installation replaces an existing module at that path; it does not
unload a running driver. Load only after the device tree and wiring are reviewed,
and stop userspace clients before unloading or replacing a loaded driver.

On the target, loading changes kernel state:

```sh
sudo modprobe apex_bridge
dmesg | tail -50
ls -l /dev/apex_bridge0 /sys/class/apex/apex_bridge0
```

A successful load is not proof of a successful probe; inspect logs and device
nodes. If kernel module signing is enforced, use the target's trusted signing
process rather than disabling it. Restore the backup and run `depmod` for the
target release if rollback is needed.

## Troubleshooting and verification limits

- No USB boot device: check the reviewed reset/BOOTSEL procedure, a data-capable
  cable, supply rails, and debug access. Do not short undocumented pins.
- No base DTB: the external SoC include was not selected or the build failed;
  do not deploy an unrelated or stale output.
- Overlay application fails: check base symbols, bindings, and supported overlay
  mechanisms/combinations. Do not repeatedly apply guessed fragments.
- Invalid module format: compare the target release, architecture, vermagic,
  symbol versions, and signing policy. Rebuild for that target rather than
  copying into the workstation's kernel directory.
- Missing bridge device: inspect probe errors, enabled SPI nodes, selected DTB,
  and reviewed GPIO polarity/reset/firmware. Do not infer correct wiring from
  a passing text validator.

Host-side checks such as `make -C tests run`, `validate_dts.py`, and
`validate_netlist.py` do not flash or exercise the prototype. Physical USB/SWD,
module loading, boot selection, and RF operation remain unverified by this guide.

For local documentation regressions:

```sh
python3 tests/test_deployment_docs.py -v
```

The five tests syntax-check the shell blocks in this guide and the getting-started
page, then execute this guide's exact SSH installation block with mocked tools.
They check target-side release selection and rejection before installation when
the module is missing, its release differs, or inspection fails. No SSH connection,
privileged command, real module installation, or physical flashing occurs in those
tests. They are also included in `make -C tests run`.

The [upstream picotool documentation](https://github.com/raspberrypi/picotool#load)
describes load verification; check the installed version's help before use.

## Related docs

- [Getting Started](getting-started.md)
- [Build Instructions](build-instructions.md)
- [SPI Protocol & Timing](spi-protocol-timing.md)
- [FAQ & Troubleshooting](faq-troubleshooting.md)
