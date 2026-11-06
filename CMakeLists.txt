cmake_minimum_required(VERSION 3.26)

# Requires an RP2350: ARMv6-M lacks the exclusive access lock-free atomics need
# (CR-1, AR-6). As a dependency the consumer chooses the board.
include(cmake/pico_family_bootstrap.cmake)
pico_family_bootstrap(PICO_LOGGING PLATFORM rp2350)

project(pico_logging VERSION 0.1.0 LANGUAGES C CXX ASM)

# No firmware-conditional code: one portable library serves host and firmware
# builds. The SDK is initialised only so the cross tree is the family's one
# build-firmware/ tree, for the device test's critical-section adapter.
house_cxx_common_settings(${PICO_LOGGING_BUILD_FIRMWARE})
pico_family_init_sdk(${PICO_LOGGING_BUILD_FIRMWARE})

# The device test links SDK sources. Keep vendor diagnostics separate; the
# device-test helper applies -Werror to its owned sources and the warning gate
# checks all owned build output. FREESTANDING follows the toolchain.
house_cxx_add_strict_target(pico_logging_strict
    VENDOR_SOURCES ${PICO_LOGGING_BUILD_FIRMWARE})

# Declares PICO_LOGGING_SANITIZE. It comes before the targets and is build-wide,
# not target-scoped, so fetched GoogleTest is instrumented too: ASan's
# container-overflow detector needs both sides of a std::string boundary.
house_cxx_enable_sanitizers(PICO_LOGGING)

# Every knob is declared before anything is added: a subdirectory reading an
# undeclared option reads an empty string.
#
# The project-wide compile-time ceiling: 0-5, or unset. Not
# house_cxx_declare_enum_option: unset is the default and leaves the header's
# ceiling (4, DBG) in place, and an enum whose default lies outside its value
# set is not an enum.
set(PICO_LOGGING_LEVEL "" CACHE STRING
    "Compile-time log level ceiling: 0=none 1=error 2=warn 3=info 4=debug 5=trace")
if(NOT PICO_LOGGING_LEVEL STREQUAL "" AND NOT PICO_LOGGING_LEVEL MATCHES "^[0-5]$")
    message(FATAL_ERROR
        "PICO_LOGGING_LEVEL must be 0-5, got '${PICO_LOGGING_LEVEL}'")
endif()

house_cxx_declare_tests_option(PICO_LOGGING)

# The cross-compiled probe for the allocation gate (AR-1); nothing runs it. It
# does not use GoogleTest, so it is independent of PICO_LOGGING_ENABLE_TESTS,
# which is off in a cross build.
pico_family_declare_probe_option(PICO_LOGGING)

# Install/export only when this is the top-level project: as a subproject the
# parent owns the export set. A parent that does export this sets the cache
# variable ON before adding it, which option() then honours.
house_cxx_declare_install_option(PICO_LOGGING)

# When this project's targets are exported, the fetched dependency they link
# must be in an export set too. This must come before the fetch below, or the
# dependency is configured with its default first.
pico_family_propagate_install(PICO_LOGGING PICO_UTIL)

# RingSink's storage is pico_util's RingBuffer and the allocation harness is its
# counter, so this is a PUBLIC link and a DEPENDENCIES entry in the package.
pico_family_dependency(pico_util
    REPOSITORY "https://github.com/jamesfxley/pico-util.git"
    REVISION 2afe3150a52429cb61c2330041cb4ec2e26c00ef)
pico_family_make_available(pico_util)

add_subdirectory(src)

# Set on the module and not the umbrella, which is INTERFACE and compiles
# nothing: PUBLIC on the module reaches this library's own translation unit and,
# through the umbrella, every consumer. Not add_compile_definitions(), which
# reaches only this project's own targets (OR-4).
if(NOT PICO_LOGGING_LEVEL STREQUAL "")
    target_compile_definitions(pico_logging_logging PUBLIC
        PICO_LOGGING_DEFAULT_CEILING=${PICO_LOGGING_LEVEL})
endif()

if(PICO_LOGGING_ENABLE_TESTS)
    house_cxx_enable_gtest()
    add_subdirectory(tests)
endif()

if(PICO_LOGGING_ENABLE_FIRMWARE_PROBE)
    enable_testing()
    add_subdirectory(tests/firmware)
endif()

if(PROJECT_IS_TOP_LEVEL AND PICO_LOGGING_BUILD_FIRMWARE)
    add_subdirectory(tests/device)
endif()

if(PICO_LOGGING_ENABLE_INSTALL)
    # pico_logging_strict is exported because pico_logging_logging links it
    # PRIVATE and install(EXPORT) rejects a link interface that names a target
    # outside the set. Consumers never link pico_logging::strict.
    set(PICO_LOGGING_EXPORT_TARGETS
        pico_logging_strict
        pico_logging_core
        pico_logging_logging
    )

    # Host-only: a firmware build does not create the capture sink, and naming
    # a target that was never created fails the configure of a firmware consumer
    # installing this package.
    if(NOT PICO_LOGGING_BUILD_FIRMWARE)
        list(APPEND PICO_LOGGING_EXPORT_TARGETS pico_logging_testing)
    endif()

    house_cxx_install_package(
        PACKAGE pico_logging VERSION ${PROJECT_VERSION}
        NAMESPACE pico_logging
        TARGETS ${PICO_LOGGING_EXPORT_TARGETS}
        DEPENDS pico_util
    )
endif()
