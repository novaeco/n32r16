# Contributing Guide

Thank you for improving the novaeco/n32r16 platform. This document summarises the expectations for patches, reviews, and releases.

## Workflow

1. Fork the repository and create a topic branch (`feat/…`, `fix/…`, or `docs/…`).
2. Keep commits focused and use [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `docs:`…).
3. Before pushing run the quality gates listed below.
4. Open a pull request against `main`. Reference GitHub issues where possible and update the PR checklist.

## Coding Standards

- Toolchain: ESP-IDF v5.5.1 (`. $IDF_PATH/export.sh`).
- Formatting: `clang-format -i` with the repository `.clang-format`.
- Static analysis: `clang-tidy -p build` scoped to touched files.
- C language level: ISO C17, warnings as errors.
- Avoid TODO/FIXME in committed code. Use GitHub issues for follow-ups.

## Quality Gates

Execute these commands for both sensor and HMI projects unless the change is obviously isolated.

```bash
# Sensor node
idf.py -C sensor_node fullclean build
idf.py -C sensor_node -T

# HMI node
idf.py -C hmi_node fullclean build
idf.py -C hmi_node -T

# Common component unit tests (native)
idf.py -C common/net test
```

When TLS or WebSocket behaviour changes, run the integration diagnostics:

```bash
python tools/ws_diagnostic.py <HOSTNAME> --token <CONFIG_HMI_WS_AUTH_TOKEN>
```

## Security Material

- Replace bearer tokens, SRP salts/verifiers, and OTA URLs before submitting a production build.
- Shared WebSocket secrets (`*_WS_CRYPTO_SECRET_BASE64`) must be 32 random bytes encoded as Base64.
- Never commit real credentials. Use placeholder strings in the defaults and supply secrets through CI variables or
  `sdkconfig.ci` overlays.

## Documentation

- Update `README.md` and `documentations/` when user-facing behaviour changes.
- Capture wiring or UI screenshots using the workflow described in `documentations/demo_capture.md`.

## Releases

1. Bump the version in `CHANGELOG.md` and add a git tag (`git tag -s vX.Y.Z`).
2. Attach build artifacts (`idf.py build` output) to the GitHub release.
3. Regenerate documentation (if Doxygen is used) and upload updated assets.
