# pico-logging orientation

Read [architecture.md](architecture.md) for the statement path and design;
[technical-specification.md](technical-specification.md) owns every guarantee.
The [pinned family guide](../infra/agents/AGENTS.md) routes shared policy.

## Commands

```sh
python3 dev.py check
python3 dev.py build
python3 dev.py lint
python3 dev.py gate consumer
```

Build enabled trees before linting. The firmware tree needs `PICO_SDK_PATH`;
`HOUSE_MATRIX_SANITIZE=ON` enables the sanitizer tree. Budgets and symbols
checks are declared in `pico.toml`.

## Layout and traps

- `src/logging/` holds the compiled formatter, sinks and level state;
  `src/testing/` supplies the host capture sink.
- The umbrella header is in both core and logging header file sets.
- Keep the ceiling in macros; moving it into a linked entity breaks OR-2.
  See the filtering explanation in [architecture.md](architecture.md).
- Interrupt callers use `RingSink`; see the sink/lock explanation in
  [architecture.md](architecture.md) and examples in
  [usage-guide.md](usage-guide.md).
- The formatting domain and truncation rules are in the specification;
  [architecture.md](architecture.md) explains the implementation.
- Device measurements live in `tests/device/`; those sources belong to their
  separate build and are absent from this tree's compile database.
- The TSan test uses `setarch -R` and needs permission for
  `personality(ADDR_NO_RANDOMIZE)`. Its container must allow that operation.
- The installed-package consumer proves exported targets; an in-tree alias
  build cannot establish that contract.
