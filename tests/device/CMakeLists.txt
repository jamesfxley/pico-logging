pico_family_add_device_test(pico_logging_device_tests
    PACKAGE pico_logging
    SOURCES main.cpp
    LIBRARIES pico_logging::core pico_sync hardware_timer)
