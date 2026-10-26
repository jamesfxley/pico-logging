cmake_minimum_required(VERSION 3.26)

# A standalone CMake project, deliberately: the top-level build never adds this
# directory, so every configuration CI builds still links no SDK sources. Same
# arrangement as tests/consumer/, and for the same reason -- this one needs a
# dependency the library itself does not have.
#
#   cmake -S tests/device -B build-device -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-device
#
# Needs PICO_SDK_PATH in the environment. Produces
# build-device/ar2_heap_watermark.uf2 and build-device/cr4_ring_sink_costs.uf2.

set(PICO_BOARD pico2_w CACHE STRING "Target board")

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set. This directory is the only part of "
        "pico-logging that links the Pico SDK; the library does not.")
endif()

# The SDK's own importer rather than a vendored copy: there is one consumer of
# it here, and a stale copy would resolve a different SDK than the one the
# environment names.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_logging_device LANGUAGES C CXX ASM)

pico_sdk_init()

# The library under measurement, from this checkout rather than a package: the
# point is to measure what this working tree does.
add_subdirectory(../.. pico_logging)

add_executable(ar2_heap_watermark ar2_heap_watermark.cpp)

# Warnings are errors for the code this project owns, per source rather than on
# the target: the SDK compiles its own sources into any target that links its
# INTERFACE libraries, and those are not this project's to fix.
house_cxx_werror_sources(ar2_heap_watermark.cpp)

target_link_libraries(ar2_heap_watermark PRIVATE
    pico_logging::core
    pico_stdlib
)

# The measurement itself. `_malloc_r` is what newlib's __smakebuf_r calls, so
# wrapping it counts the exact event AR-2 asks about; the SDK wraps `malloc`,
# which is a different symbol, so the two coexist. Wrapping only affects
# references resolved across object files, which is what makebuf.o's call to
# the allocator is.
target_link_options(ar2_heap_watermark PRIVATE -Wl,--wrap=_malloc_r)

target_compile_definitions(ar2_heap_watermark PRIVATE
    PICO_LOGGING_DEVICE_BOARD="${PICO_BOARD}"
)

# stdio over USB CDC: the measurement reads back over the same USB serial the
# log lines go out on, which is the configuration a consumer actually runs.
pico_enable_stdio_usb(ar2_heap_watermark 1)
pico_enable_stdio_uart(ar2_heap_watermark 0)

pico_add_extra_outputs(ar2_heap_watermark)

# CR-4's costs: the stack a log statement reaches and how long RingSink holds
# the SDK's critical_section. Same arrangement as the image above; it needs
# no wrapped symbol.
add_executable(cr4_ring_sink_costs cr4_ring_sink_costs.cpp)
house_cxx_werror_sources(cr4_ring_sink_costs.cpp)
target_link_libraries(cr4_ring_sink_costs PRIVATE
    pico_logging::core
    pico_stdlib
    pico_sync
    hardware_clocks
)
# AR-5 through StderrSink with stdio over UART as well as USB, so a statement
# drives both drivers and the depth bounds either; the report is still read
# over USB, so no UART adapter is needed.
option(PICO_LOGGING_DEVICE_STDIO_UART "Also route the AR-5 rig's stdio to UART"
       OFF)
if(PICO_LOGGING_DEVICE_STDIO_UART)
    set(PICO_LOGGING_DEVICE_STDIO "usb+uart")
    set(PICO_LOGGING_DEVICE_UART 1)
else()
    set(PICO_LOGGING_DEVICE_STDIO "usb")
    set(PICO_LOGGING_DEVICE_UART 0)
endif()
target_compile_definitions(cr4_ring_sink_costs PRIVATE
    PICO_LOGGING_DEVICE_BOARD="${PICO_BOARD}"
    PICO_LOGGING_DEVICE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    PICO_LOGGING_DEVICE_STDIO="${PICO_LOGGING_DEVICE_STDIO}"
)
pico_enable_stdio_usb(cr4_ring_sink_costs 1)
pico_enable_stdio_uart(cr4_ring_sink_costs ${PICO_LOGGING_DEVICE_UART})
pico_add_extra_outputs(cr4_ring_sink_costs)
