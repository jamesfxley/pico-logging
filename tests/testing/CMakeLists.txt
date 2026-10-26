# Links pico_logging::testing, the module it mirrors, rather than the umbrella:
# the test tree mirrors src/ so that what a test covers is readable from its
# path. A fake that ships is a fake that needs its own tests.
house_cxx_add_module_test(pico_logging testing
    SOURCES capture_sink_test.cpp
)
