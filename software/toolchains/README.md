<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2026 GhostBlade Project -->

# Cross-Compilation Toolchain Files

## Files

| File | Purpose |
|------|---------|
| `rk3576-aarch64.cmake` | CMake toolchain file for RK3576-side aarch64/Linux builds |
| `../../firmware/rp2350b/toolchain-arm-none-eabi.cmake` | CMake toolchain file for RP2350B firmware builds |

## Typical Use

### RK3576 userspace builds

```bash
cmake -S some-project -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/ghostblade/software/toolchains/rk3576-aarch64.cmake
```

### RP2350B firmware

```bash
cmake -S firmware/rp2350b -B firmware/rp2350b/build \
  -DCMAKE_TOOLCHAIN_FILE=firmware/rp2350b/toolchain-arm-none-eabi.cmake \
  -DPICO_SDK_PATH=$HOME/pico-sdk
```
