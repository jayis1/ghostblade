# Security Policy

## Supported Versions

| Version | Supported |
| ------- | --------- |
| 0.1.x   | ✅ Active development |
| < 0.1   | ❌ Pre-release, not supported |

GhostBlade is currently in active development (pre-1.0). Security fixes are
applied to the latest `main` branch only.

## Reporting a Vulnerability

GhostBlade is an **open-source pentesting hardware platform**. We take security
vulnerabilities seriously, especially those that could:

- Allow unauthorized access to the device or connected systems
- Compromise the integrity of security assessments performed with GhostBlade
- Expose the device to remote exploitation
- Bypass hardware security mechanisms (secure boot, JTAG lock, etc.)

### How to Report

**Please do not report security vulnerabilities through public GitHub issues.**

Instead, report them via one of these channels:

1. **GitHub Security Advisory** (preferred):
   Use [GitHub's private vulnerability reporting](https://github.com/jayis1/ghostblade/security/advisories/new)
   to submit a coordinated disclosure.

2. **Email**: Send a PGP-encrypted email to the project maintainers if GitHub
   Security Advisory is unavailable.

### What to Include

- A description of the vulnerability and its potential impact
- Steps to reproduce or a proof-of-concept
- The affected component(s) (firmware, kernel driver, DTS, hardware design)
- Any mitigations or workarounds you have identified

### Response Timeline

| Stage | Target |
|-------|--------|
| Acknowledgment | Within 48 hours |
| Initial assessment | Within 5 business days |
| Fix development | Depends on severity (critical: 7 days, high: 14 days, medium: 30 days) |
| Advisory publication | After fix is merged and users have had time to update |

## Responsible Disclosure

As a pentesting tool, GhostBlade occupies a unique position. We ask that
researchers:

- **Do not** publicly disclose vulnerabilities before a fix is available
- **Do** report issues that could compromise the device or its operator
- **Do** consider the impact on operators conducting authorized security assessments
- **Do not** use reported vulnerabilities against devices you do not own or have
  authorization to test

## Security Features

GhostBlade includes several hardware and software security mechanisms:

- **Secure Boot**: RK3576 supports verified boot chain (to be enabled in production)
- **JTAG Lock**: RP2350B JTAG can be disabled via bootrom fuses
- **SPI Bridge Authentication**: CRC-64 header integrity on all SPI frames
- **Watchdog Timer**: Hardware watchdog prevents firmware hang exploitation
- **Brownout Detection**: Voltage monitoring prevents unstable operation
- **RF Shutdown**: All RF transmitters can be disabled via hardware control

## Scope

### In Scope

- Firmware vulnerabilities (RP2350B, boot chain)
- Kernel driver vulnerabilities (apex_bridge SPI driver)
- SPI protocol security (CRC bypass, frame injection)
- Device tree misconfigurations that expose attack surface
- Hardware design flaws that compromise device security

### Out of Scope

- Theoretical vulnerabilities without proof of concept
- Denial of service via physical access (the device is designed to be used
  by the operator with physical access)
- Vulnerabilities in third-party components not shipped with GhostBlade
- Social engineering attacks against project maintainers

## Firmware Signing

Production firmware images will be signed using ECDSA-P256. The signing key
is not stored in the repository. If you need to verify firmware signatures,
use the public verification key published in the release artifacts.