# AR-1's cross-compiled probes: the log path must not reach an allocating
# formatter on a real image, and the gate that says so must be seen to fail.
#
# Declared here rather than in the root listfile: a target lives in the
# directory holding its sources, which is what lets a reader find it from the
# path.
# Freestanding inside the SDK tree: the probe links no SDK library, so it gets
# no linker script and no runtime from one. The helper supplies nosys, no
# exceptions and no RTTI.
pico_family_add_cross_probe(pico_logging_log_probe
    PACKAGE pico_logging
    SOURCE log_probe.cpp
    LIBRARIES pico_logging::core
)

# The gate's negative control: an image the gate must REJECT. A gate only ever
# seen to pass is not evidence -- stale symbol names, a silently empty nm, or a
# grep that no longer matches all report success for every input. The firmware
# CI job asserts the gate fails on this one. It links no library: what it must
# contain is newlib's allocating formatter, reached without this library's help.
pico_family_add_cross_probe(pico_logging_allocating_fixture
    PACKAGE pico_logging
    SOURCE allocating_fixture.cpp
)

# The heap gate's negative control: a translation unit the gate must REJECT.
# Separate from the fixture above because the two gates ask different
# questions -- that one asks what a linked image DEFINES, this one asks what a
# translation unit leaves UNDEFINED, and a fixture reaching the float formatter
# leaves the formatter undefined rather than the allocator. It links no library
# for the same reason as the other: what it must reach, it must reach without
# this library's help.
pico_family_add_cross_probe(pico_logging_heap_call_fixture
    PACKAGE pico_logging
    SOURCE heap_call_fixture.cpp
)

# The README's and the usage guide's examples that need the SDK, compiled here
# from the documents (EN-03.5); the host tree's tests/docs compiles the rest.
# Header targets only: linking pico_stdlib would compile the SDK's own sources
# under this repository's strict flags.
house_cxx_add_doc_examples(pico_logging_firmware_doc_examples PACKAGE pico_logging
    GROUP firmware GROUPS firmware
    DOCUMENTS ${PROJECT_SOURCE_DIR}/README.md
              ${PROJECT_SOURCE_DIR}/docs/usage-guide.md
    HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/docs
    LIBRARIES pico_logging::core pico_stdlib_headers pico_sync_headers)

# AR-1: the truncation path must stay exception-free at debug optimization.
add_library(pico_logging_debug_string_probe OBJECT string_probe.cpp)
target_link_libraries(pico_logging_debug_string_probe
    PRIVATE pico_logging::core pico_logging_strict)
target_compile_options(pico_logging_debug_string_probe
    PRIVATE -Og -fno-exceptions -fno-rtti)
add_test(NAME pico_logging_debug_string_has_no_throw_path
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM}
        -DOBJECT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/pico_logging_log_probe.dir
        -DDEBUG_ROOT=${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/pico_logging_debug_string_probe.dir
        -P ${CMAKE_CURRENT_SOURCE_DIR}/no_throw_path.cmake)
