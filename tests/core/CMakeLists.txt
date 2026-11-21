# Built against the umbrella header alone, at the default level, so it stays a
# check on the public surface and not on a particular filter setting.
house_cxx_add_module_test(pico_logging core
    SOURCES pico_logging_test.cpp
)

# The umbrella header's version constants are hand-maintained. Feeding the
# project() version in here lets the test catch drift between the two, where a
# literal copied from the header would compare the header with itself. This is
# the host-side half of the pin that src/logging/sinks.cpp static_asserts.
house_cxx_version_definitions(pico_logging_core_tests pico_logging PRIVATE)
