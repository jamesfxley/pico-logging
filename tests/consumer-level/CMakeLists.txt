cmake_minimum_required(VERSION 3.26)
project(pico_logging_consumer_level LANGUAGES CXX)

# The declared consumer case "level-five" (house.toml): the package is installed
# at PICO_LOGGING_LEVEL=5 and this consumer, which reaches nothing in the source
# tree, must see that ceiling (OR-4). 5 is TRACE and not the default, so the
# consumer cannot see it by accident. The program keeps the name consumer_smoke
# because the package's consumer_executables names it.
#
# Like tests/consumer, it compiles only against an installed package, so the
# root .lintignore keeps it out of the repo-wide clang-tidy sweep.
find_package(pico_logging REQUIRED)

add_executable(consumer_smoke level_smoke.cxx)
target_link_libraries(consumer_smoke PRIVATE
    pico_logging::core
    pico_logging::logging
    pico_logging::testing
)
target_compile_features(consumer_smoke PRIVATE cxx_std_20)
