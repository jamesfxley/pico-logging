# Built against the umbrella header alone, at the default level, so it stays a
# check on the public surface rather than on a particular filter setting.
house_cxx_add_module_test(pico_logging core
    SOURCES pico_logging_test.cpp
)

# The umbrella header's version constants are hand-maintained; feeding the
# project() version in here is what makes the test able to catch drift between
# the two rather than comparing the header against a literal copied from it.
# The host-side half of the pin that src/logging/sinks.cpp static_asserts.
house_cxx_version_definitions(pico_logging_core_tests pico_logging PRIVATE)
