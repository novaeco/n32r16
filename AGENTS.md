# Repository Contribution Guidelines

## Scope
These rules apply to the entire repository.

## Coding Standards
- Use ISO C17 for all C source files.
- Follow the provided `.clang-format` configuration once it is added. Until then, use LLVM style with 4-space indentation.
- All new C files must compile without warnings under ESP-IDF v5.5 with `-Wall -Wextra`.
- Do not leave TODO, FIXME, or placeholder comments in committed code.

## Quality Gates
- Ensure `idf.py build` succeeds for both `sensor_node` and `hmi_node` applications before opening a PR.
- Provide unit tests for critical protocol and driver modules when feasible.
- Run `clang-tidy` using the embedded profile `-checks=esp-idf-*` on touched files.

## Documentation
- Keep `README.md` up to date with build, flash, and wiring instructions.
- Document any new configuration options introduced in `sdkconfig.defaults`.

## Commit Policy
- Use conventional commit style messages.
- Keep commits focused and self-contained.

## Pull Requests
- Update the PR checklist before submission:
  1. [ ] `idf.py build` succeeds for both targets
  2. [ ] Static analysis (`clang-tidy`) run on modified sources
  3. [ ] Unit tests pass
  4. [ ] Documentation updated

