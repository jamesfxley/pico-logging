# Cross-compiled probes: the log path must not reach an allocating formatter on
# a real image, and the gate that says so must be seen to reject a violation
# (AR-1).
# Freestanding inside the SDK tree: the probe links no SDK library, so it gets
# no linker script and no runtime from one. The helper supplies nosys, no
# exceptions and no RTTI.
pico_family_add_cross_probe(pico_logging_log_probe
    PACKAGE pico_logging
    SOURCE log_probe.cpp
    LIBRARIES pico_logging::core
)

# The gate's negative control: an image the gate must reject, which the firmware
# CI job asserts. It links no library: it must reach newlib's allocating
# formatter without this library's help.
pico_family_add_cross_probe(pico_logging_allocating_fixture
    PACKAGE pico_logging
    SOURCE allocating_fixture.cpp
)

# The heap gate's negative control: a translation unit the gate must reject. It
# links no library, for the same reason as the fixture above.
pico_family_add_cross_probe(pico_logging_heap_call_fixture
    PACKAGE pico_logging
    SOURCE heap_call_fixture.cpp
)

# The README's and the usage guide's examples that need the SDK, compiled here
# from the documents; the host tree's tests/docs compiles the rest.
# Header targets only: linking pico_stdlib would compile the SDK's own sources
# under this repository's strict flags.
house_cxx_add_doc_examples(pico_logging_firmware_doc_examples PACKAGE pico_logging
    GROUP firmware GROUPS firmware
    DOCUMENTS ${PROJECT_SOURCE_DIR}/README.md
              ${PROJECT_SOURCE_DIR}/docs/usage-guide.md
    HARNESS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/docs
    LIBRARIES pico_logging::core pico_stdlib_headers pico_sync_headers)

# The truncation path must stay exception-free at debug optimisation (AR-1).
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
