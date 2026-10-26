# Cross-compiled probe footprint

Observations, not requirements. The specification states gates; the numbers a
gate was set from live here, with the conditions that produced them
(`EN-03.2`, `EN-06.7`). Nothing here is normative, and a stale figure is a
stale figure rather than a broken promise.

Re-measure by rebuilding the run below. A number without its build type and
toolchain cannot be compared against anything.

The rig is `log_probe.cpp` beside this file; `README.md` says how to build it.

`tests/firmware/log_probe.cpp` logs one of every operand type through a
`RingSink`, so its image is what a consumer pays to link this library and use
all of it. It is the image the footprint gate measures.

| Condition | Value |
|---|---|
| Date | 2026-10-29 |
| pico-logging | 0.1.0, the tree of the commit that adds this record |
| pico-util | 0.1.0 at `604e9fc2`, fetched through the pinned dependency |
| Toolchain | `arm-none-eabi-gcc` 14.2.1 (`14.2.rel1-1`, newlib 4.6.0.20260123) |
| Pico SDK | 2.3.0, configured only; the probe links no SDK library |
| Target | `PICO_BOARD=pico2_w`: `cortex-m33`, thumb, `v8-m.main+fp/softfp` |
| Build type | `Release` (`-O3`) |
| Configure and build | `python3 dev.py build --tree firmware`, into `build-firmware/` |
| Measured with | `arm-none-eabi-size`, `arm-none-eabi-nm -C --size-sort -S`, the `footprint` gate |

| Float formatter | Berkeley `text` | `.text` + `.rodata` | Static RAM | `LogLine::write_float` |
|---|---|---|---|---|
| `detail::format_float` | 53,292 | 52,648 | 2,756 | 2,164 |
| `std::to_chars` | 211,448 | 210,292 | 2,756 | — |

The difference is 158,156 bytes of `text`: the `std::to_chars` image is almost
four times the size. It is not the formatter's own code: every `ryu::` symbol in
the `std::to_chars` image totals 137,179 bytes, of which four lookup tables —
`POW10_SPLIT_2` (75,192), `POW10_SPLIT` (29,376), `DOUBLE_POW5_INV_SPLIT` (5,472)
and `DOUBLE_POW5_SPLIT` (5,216) — account for 115,256. libstdc++'s
shortest-round-trip path reaches Ryu's fixed-notation renderer, so the tables are
referenced and survive `--gc-sections`.

The `std::to_chars` row is produced by replacing the body of
`LogLine::write_float` with a `std::to_chars` call into the same buffer and
rebuilding; it is a throwaway patch, not a configuration.

The 64 KiB flash budget the firmware job enforces is set above the first row on
purpose: it exists to catch a step change of the size in the second, not to
police a few hundred bytes. The 8 KiB static-RAM budget is loose for the same
reason.
