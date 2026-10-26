# Device measurements

A standalone CMake project. The top-level build never references this
directory, so every configuration CI builds still links no SDK sources — this
is the only part of pico-logging that links the Pico SDK, and the lint gate skips
it via the root `.lintignore` because its sources are not in this tree's
compile database.

## `ar2_heap_watermark`

Settles AR-2: whether the log path through `StderrSink` reaches the allocator.
It counts calls to `_malloc_r` — the function `__smakebuf_r` calls — across a
burst through the default sink, against a deliberate allocation in the same run
that the counter must observe. `measurements.md` beside it describes the run
and records each result with its conditions; no run on 0.1.0 is recorded yet.
Re-run it rather than assuming a result if the libc, its stdio buffering, or
the SDK version changes.

```sh
PICO_SDK_PATH=/path/to/pico-sdk \
  cmake -S tests/device -B build-device -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DFETCHCONTENT_SOURCE_DIR_PICO_UTIL=/path/to/pico-util
cmake --build build-device          # -> build-device/*.uf2
```

`FETCHCONTENT_SOURCE_DIR_PICO_UTIL` names the pico-util checkout to measure
against. The sibling-checkout lookup resolves from the top-level source
directory, which for this project is `tests/device/`, so it does not find a
checkout beside the repository; without the override the configure fetches
pico-util's pinned release instead.

Flash the board from a library checkout with the family device CLI:
`python3 -m infra.pico_tooling device flash --serial <serial> --console --until '<heading>' --timeout 90 <image>`.
See [the device guide](../../infra/guides/device.md) for setup and operation.
The image prints its report once a second, forever, so the console may attach
at any time. It still waits up to 30 s for DTR before taking its first sample;
the proxy sets DTR when opening the console.

## `cr4_ring_sink_costs`

Measures what a log statement costs the context it runs in: how deep into the
stack a statement reaches through a `RingSink` behind the SDK's
`critical_section` and through the default `StderrSink`, and how long
`RingSink::write` holds the lock, which is how long interrupts stay masked on
that core. Each figure has a negative control in the same run — a frame of
known size for the stack, a hold of known length for the lock — and the report
says so when a control was not observed.

The `StderrSink` rows run with every interrupt but USB's disabled, so the USB
driver has transfer events to dispatch, and they mean something only with a
USB host attached: without one the driver is never called, and the report
prints `STDERR PATH NOT REACHED` instead of their result.

Built by the same configure as the image above; flash
`build-device/cr4_ring_sink_costs.uf2` the same way. `AR-5`'s record covers
three build types, each its own configure, and stdio over USB alone and over
USB and UART. The UART variant routes the rig's stdio to both drivers; its
report is still read over USB, so no UART adapter is needed:

```sh
PICO_SDK_PATH=/path/to/pico-sdk \
  cmake -S tests/device -B build-device-uart -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DPICO_LOGGING_DEVICE_STDIO_UART=ON \
        -DFETCHCONTENT_SOURCE_DIR_PICO_UTIL=/path/to/pico-util
cmake --build build-device-uart --target cr4_ring_sink_costs
```

`measurements.md` describes the run and records each result with its
conditions; no run on 0.1.0 is recorded yet.
