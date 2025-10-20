# Repository Agent Guidelines

## Scope
These rules apply to the entire repository unless a more specific `AGENTS.md` is present in a subdirectory.

## Code Style
- All C source must be C17 compliant and clang-format friendly.
- Include guards use `#pragma once`.
- Prefer `static inline` helpers for small utilities.
- No TODO/FIXME markers. Implement fully.

## Build
- Projects must build with ESP-IDF v5.5 using `idf.py` (CMake + Ninja).
- Keep target-specific defaults in `sdkconfig.defaults`.
- Partition tables live in `partitions/`.

## Quality
- Supply unit tests via `idf.py -T` when possible.
- Ensure clang-tidy and clang-format configs exist at repo root.

## Documentation
- Update the root README when altering build/flash instructions.
- PR checklist: formatting, static analysis, tests, documentation, version bumps as needed.

