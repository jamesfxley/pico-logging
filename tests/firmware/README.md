# Cross-compiled probes

The cross tree builds three images, and none of them is run. They exist so the
linker has to resolve the paths a gate then inspects. The tree is configured
through the Pico SDK, but no probe links an SDK library, so each image contains
this library and newlib:

```sh
python3 dev.py build --tree firmware
```

| Image | Role |
|---|---|
| `log_probe.cpp` | Logs one of every operand type through a `RingSink`, so the image contains every formatting path. Subject of the three gates below |
| `allocating_fixture.cpp` | Negative control for the allocation gate. Deliberately reaches newlib's allocating float formatter, and the gate must reject it |
| `heap_call_fixture.cpp` | Negative control for the heap gate. Calls the allocator directly, and the gate must reject its objects |

`docs/` holds a fourth target, `pico_logging_firmware_doc_examples`: the
harnesses that compile the documents' firmware examples (`EN-03.5`). It is
compiled and never linked, and reaches the SDK only through
`pico_stdlib_headers` and `pico_sync_headers`, the headers those examples
include.

Three gates read the probe: two symbol-set checks, `allocating-formatter` over
the image and `c-heap-alloc` over its objects, and the footprint budget.
`pico.toml` holds their budgets and symbol sets; `python3 dev.py check --list`
names the gates.

Each symbol gate has a negative control, one of the two fixtures above,
declared in `pico.toml` as a `[[symbols]]` table with `expect = "refused"`.
pico-tooling's own suite shows that the footprint gate rejects an image over
either budget.

`measurements.md` beside this file records what the probe measured and under
what conditions. It is a run record and not a requirement: if the toolchain or
the formatter changes, re-measure and do not cite it.
