cmake_minimum_required(VERSION 3.26)

# A firmware configure names an SDK. The SDK is imported before project() so
# its toolchain file applies, and the consumer is then a firmware program with
# its own source: the host smoke uses the capture sink, which a firmware
# package does not export.
if(PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
    project(pico_logging_consumer_smoke LANGUAGES C CXX ASM)
    pico_sdk_init()
    # The SDK toolchain confines find_package to its sysroot; the installed
    # prefix is outside it.
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(PICO_LOGGING_CONSUMER_SOURCE firmware_smoke.cxx)
else()
    project(pico_logging_consumer_smoke LANGUAGES CXX)
    set(PICO_LOGGING_CONSUMER_SOURCE consumer_smoke.cxx)
endif()

# Stands in for a downstream project consuming an installed pico-logging, which
# an in-tree test cannot cover: it proves find_package reaches the package and
# its exported headers suffice. That PICO_LOGGING_LEVEL reaches a consumer as a
# PUBLIC compile definition (OR-4) is the consumer case "level-five" in
# house.toml.
#
# This translation unit compiles only against an installed package, so it has no
# entry in the compile database and the clang-tidy sweep must skip it. The root
# .lintignore does that; the .cxx extension does not, since the lint gate
# enumerates .cxx alongside .cpp and .cc.
find_package(pico_logging REQUIRED)

add_executable(consumer_smoke ${PICO_LOGGING_CONSUMER_SOURCE})

# Every exported target is named, not only the aggregate: a module dropped from
# the export set would still resolve through it. pico_logging::testing is named
# for a consumer's test tree.
target_link_libraries(consumer_smoke PRIVATE
    pico_logging::core
    pico_logging::logging
)
if(NOT PICO_SDK_PATH)
    target_link_libraries(consumer_smoke PRIVATE pico_logging::testing)
else()
    target_link_libraries(consumer_smoke PRIVATE pico_stdlib)
    pico_add_extra_outputs(consumer_smoke)
endif()

target_compile_features(consumer_smoke PRIVATE cxx_std_20)

# A target overriding the configured ceiling (OR-2, OR-4). It builds with
# -Werror because a second definition of the same macro is a warning, and the
# override has to win without one. Objects only: the proof is the static_assert
# at compile time, and a firmware executable would need the SDK's runtime to
# link.
add_library(consumer_override OBJECT consumer_override.cxx)
target_link_libraries(consumer_override PRIVATE pico_logging::core)
target_compile_definitions(consumer_override PRIVATE LOGGING_GLOBAL_LEVEL=1)
target_compile_options(consumer_override PRIVATE -Werror)
target_compile_features(consumer_override PRIVATE cxx_std_20)
