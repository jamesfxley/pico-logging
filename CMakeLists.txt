cmake_minimum_required(VERSION 3.26)

# AR-6. CR-1's lock-free atomics need exclusive access, which ARMv6-M lacks.
# As a dependency the board is the consumer's, and only that is checked of it.
include(cmake/pico_family_bootstrap.cmake)
pico_family_bootstrap(PICO_LOGGING PLATFORM rp2350)

project(pico_logging VERSION 0.1.0 LANGUAGES C CXX ASM)

# No firmware-conditional code: the same portable library (headers +
# src/logging/sinks.cpp) serves host and firmware builds. The SDK is initialised only so
# that the cross tree is the family's one build-firmware/ tree; no target here
# links an SDK library.
house_cxx_common_settings(${PICO_LOGGING_BUILD_FIRMWARE})
pico_family_init_sdk(${PICO_LOGGING_BUILD_FIRMWARE})

# -Werror at target scope, because the probes link no SDK library: nothing
# third-party compiles into these targets, so every diagnostic is actionable and
# no VENDOR_SOURCES keyword is needed. FREESTANDING follows the toolchain, which
# is the helper's default.
house_cxx_add_strict_target(pico_logging_strict)

# Declares PICO_LOGGING_SANITIZE. Before the targets, and build-wide rather
# than target-scoped, so fetched GoogleTest is instrumented too -- ASan's
# container-overflow detector needs both sides of a std::string boundary.
house_cxx_enable_sanitizers(PICO_LOGGING)

# Every knob this build has, declared before anything is added: a subdirectory
# reading an option that has not been declared yet reads an empty string.
#
# The project-wide compile-time ceiling. Not house_cxx_declare_enum_option:
# the accepted set is 0-5 plus unset, and unset is the default that leaves the
# header's own ceiling (4, DBG) in place and adds nothing to any command line.
# An enum with a default outside its own value set is not one.
set(PICO_LOGGING_LEVEL "" CACHE STRING
    "Compile-time log level ceiling: 0=none 1=error 2=warn 3=info 4=debug 5=trace")
if(NOT PICO_LOGGING_LEVEL STREQUAL "" AND NOT PICO_LOGGING_LEVEL MATCHES "^[0-5]$")
    message(FATAL_ERROR
        "PICO_LOGGING_LEVEL must be 0-5, got '${PICO_LOGGING_LEVEL}'")
endif()

house_cxx_declare_tests_option(PICO_LOGGING)

# The cross-compiled probe behind AR-1's allocation gate. Nothing runs it; it
# exists so the linker has to resolve every formatting path, and
# the symbols gate can then inspect the image. GoogleTest is
# not involved, so this is independent of PICO_LOGGING_ENABLE_TESTS -- which is
# off when cross-compiling, which is the only time this is on.
pico_family_declare_probe_option(PICO_LOGGING)

# Install/export only when this is the top-level project: as a subproject the
# parent owns the export set. A parent that does export this sets the cache
# variable ON before adding it, which option() then honours.
house_cxx_declare_install_option(PICO_LOGGING)

# When this project's targets are exported, the fetched dependency they link
# must be in an export set too. Before the fetch below, or it is configured
# with its default first.
pico_family_propagate_install(PICO_LOGGING PICO_UTIL)

# pico_util is the family's base layer. RingSink's storage is its RingBuffer
# and the allocation harness under tests/ is its counter, so this is a PUBLIC
# link on the logging module and a DEPENDENCIES entry in the installed package.
pico_family_dependency(pico_util
    REPOSITORY "https://github.com/jamesfxley/pico-util.git"
    REVISION 2afe3150a52429cb61c2330041cb4ec2e26c00ef)
pico_family_make_available(pico_util)

add_subdirectory(src)

# PUBLIC because the ceiling is read by a public header, so the library and
# every consumer of it must be compiled with the same value unless a consumer
# deliberately overrides it (CXX-10.3). Propagated as PICO_LOGGING_DEFAULT_CEILING
# rather than LOGGING_GLOBAL_LEVEL: a consumer's own LOGGING_GLOBAL_LEVEL on one
# target is then a second macro the header prefers, not a redefinition. On the module rather than the
# umbrella: the umbrella is INTERFACE and compiles nothing, and this ceiling has
# to reach both this library's own translation unit and every consumer. PUBLIC
# on the module does both, and propagates through the umbrella's INTERFACE link.
#
# A per-target override on a consumer is legal and means what it says -- no
# entity the linker sees carries this value. Do not "simplify" this to
# add_compile_definitions(), which reaches only this project's own targets and
# would leave a consumer compiling the header at a different ceiling than the
# library was built with.
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

if(PICO_LOGGING_ENABLE_INSTALL)
    # pico_logging_strict is exported because pico_logging_core links it
    # PRIVATE and install(EXPORT) will not export a target whose link interface
    # names one outside the set. A consumer never links pico_logging::strict --
    # it carries this repo's warning flags, not part of the shipped surface.
    set(PICO_LOGGING_EXPORT_TARGETS
        pico_logging_strict
        pico_logging_core
        pico_logging_logging
    )

    # The capture sink is host-only: it stands in for a Sink a consumer's tests
    # supply, so a firmware build does not create it. Naming a target that was
    # never created fails the configure, which is what a firmware consumer
    # installing this package as a dependency would hit.
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
