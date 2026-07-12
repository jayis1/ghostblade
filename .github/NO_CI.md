# DO NOT ADD CI/CD WORKFLOWS TO THIS DIRECTORY

This project **does not use GitHub Actions or any CI/CD automation**.

This is an explicit project decision by the maintainers. Do not add
`.github/workflows/` files, do not reference CI in documentation, and
do not add "CI must pass" statements to contribution guidelines.

## Rationale

GhostBlade is an open-source hardware project with cross-compilation
toolchains (ARM bare-metal for RP2350B, AArch64 for RK3576, x86 for
host tools) and hardware-in-the-loop testing that cannot run in cloud
CI environments. The maintainers test changes locally before merging.

## What to Do Instead

- Run `make check` locally to verify toolchain availability
- Run `make tests` locally to execute unit tests
- Run `make validate` locally to validate DTS syntax
- Use `tools/check_internal_links.py` to verify markdown links
- Follow the testing checklist in `CONTRIBUTING.md`

See [CONTRIBUTING.md](../CONTRIBUTING.md) for the full contribution workflow.