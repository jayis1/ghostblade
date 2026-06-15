# Contributing to GhostBlade

Thank you for your interest in contributing to the GhostBlade (Project NullSpectre) project! This document provides guidelines for contributing.

## Code of Conduct

Be respectful, constructive, and professional. We're all here to build something great.

## How to Contribute

### Hardware (Schematics, PCB, BOM)

1. Use KiCad 8+ for all schematic and PCB edits
2. Follow the naming conventions in `GhostBlade.mf`
3. Ensure all net names match the schematic netlist in Phase 2 documentation
4. Run ERC/DRC before submitting changes
5. Submit Gerber files + BOM for manufacturing review

### Firmware (RP2350B)

1. Build with CMake + Pico SDK (see `firmware/rp2350b/CMakeLists.txt`)
2. Follow the RP2350B SDK conventions (Pico SDK)
3. Use the register definitions in `firmware/rp2350b/include/`
4. All SPI transactions must use the framed protocol defined in `apex_bridge_regs.h`
5. Add CRC validation for all inter-processor communication
6. Test on hardware before submitting PRs

### Software (Linux Driver, Userspace)

1. Follow Linux kernel coding style — use `.clang-format` in the repo root
2. The driver must compile cleanly against kernel 6.6+
3. Add `MODULE_AUTHOR`, `MODULE_DESCRIPTION`, `MODULE_LICENSE` to all modules
4. Use kernel-doc comments for all public functions
5. Test with `CONFIG_DEBUG_FS`, `CONFIG_DYNAMIC_DEBUG` enabled
6. Userspace library (`libapex`) should follow the same style

### Device Tree

1. Keep `ghostblade-rk3576.dts` in sync with `GhostBlade.mf` manifest
2. Optional hardware goes in `ghostblade-options.dts` overlay
3. Verify DTS nodes/properties match `GhostBlade.mf` before submitting

### Documentation

1. Markdown format for all docs — lint with `.markdownlint.json` config
2. Include units for all measurements (mm, MHz, mA, etc.)
3. Keep `GhostBlade.mf` in sync with any spec changes
4. Reference component designators (U1, R5, C12, etc.) consistently
5. Verify internal markdown links are valid
6. Spellcheck with `codespell` using `.codespell.ignore`

## Pull Request Process

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Make your changes with clear, descriptive commit messages
4. Ensure all existing documentation is updated if your change affects specs
5. Push to your fork and open a Pull Request against `main`
6. Address review feedback promptly

## Commit Message Format

```
type(scope): brief description

Detailed explanation of the change, why it's needed, and any
trade-offs considered.

Fixes #123 (if applicable)
```

Types: `hw` (hardware), `fw` (firmware), `sw` (software), `docs`, `fix`, `refactor`

## License

By contributing, you agree that your work will be licensed under:
- Hardware: CERN-OHL-S v2
- Software: GPL-2.0-or-later
- Documentation: CC-BY-SA 4.0