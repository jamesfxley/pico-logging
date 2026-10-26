# Links pico_logging::logging, the module it mirrors, rather than the umbrella:
# the test tree mirrors src/ so that what a test covers is readable from its
# path. The module carries both the umbrella's include directory and its own, so
# nothing here needs the INTERFACE target to reach a header.
house_cxx_add_module_test(pico_logging logging
    SOURCES logging_test.cpp
    LIBRARIES pico_logging::testing
)

target_compile_definitions(pico_logging_logging_tests PRIVATE LOGGING_GLOBAL_LEVEL=2)

# FR-3's over-wide values, built with GNU extensions as a consumer's target is
# by default: only there is a 128-bit integer an integral type.
house_cxx_add_module_test(pico_logging logging
    NAME pico_logging_wide_value_tests
    SOURCES wide_value_test.cpp
    LIBRARIES pico_logging::testing
)
set_target_properties(pico_logging_wide_value_tests PROPERTIES CXX_EXTENSIONS ON)

# FR-5's boundary tests. The formatter trades a restricted domain for a small
# fraction of the flash std::to_chars costs (tests/firmware/measurements.md), so
# its edges are what has to be pinned.
house_cxx_add_module_test(pico_logging logging
    NAME pico_logging_float_format_tests
    SOURCES float_format_test.cpp
    LIBRARIES pico_logging::testing
)

# OR-2: two translation units of the same binary compiled at different
# ceilings. Each source carries its own LOGGING_GLOBAL_LEVEL. A design holding
# the level in the library's own translation unit could not express this
# without an ODR violation, and would fail differently depending on link order
# and optimisation level rather than failing outright.
add_library(pico_logging_level_verbose OBJECT level_odr/verbose_unit.cpp)
target_link_libraries(pico_logging_level_verbose PRIVATE
    pico_logging_strict
    pico_logging::logging
)
target_include_directories(pico_logging_level_verbose PRIVATE level_odr)
target_compile_definitions(pico_logging_level_verbose PRIVATE LOGGING_GLOBAL_LEVEL=4)
target_compile_features(pico_logging_level_verbose PRIVATE cxx_std_20)

add_library(pico_logging_level_quiet OBJECT level_odr/quiet_unit.cpp)
target_link_libraries(pico_logging_level_quiet PRIVATE
    pico_logging_strict
    pico_logging::logging
)
target_include_directories(pico_logging_level_quiet PRIVATE level_odr)
target_compile_definitions(pico_logging_level_quiet PRIVATE LOGGING_GLOBAL_LEVEL=1)
target_compile_features(pico_logging_level_quiet PRIVATE cxx_std_20)

house_cxx_add_module_test(pico_logging logging
    NAME pico_logging_level_odr_tests
    SOURCES
        level_odr/level_odr_test.cpp
        $<TARGET_OBJECTS:pico_logging_level_verbose>
        $<TARGET_OBJECTS:pico_logging_level_quiet>
)

target_include_directories(pico_logging_level_odr_tests PRIVATE level_odr)

# Its own executable because it replaces the global allocation functions, which
# is program-wide: hosting it with tests that allocate freely would make all of
# them depend on the counter's arming state.
house_cxx_add_module_test(pico_logging logging
    NAME pico_logging_allocation_tests
    SOURCES allocation_test.cpp
    LIBRARIES pico_util::testing
)

# No LOGGING_GLOBAL_LEVEL: RingSink queues whatever a sink is handed and is
# indifferent to the filter, so this target stays on the default ceiling.
find_package(Threads REQUIRED)
house_cxx_add_module_test(pico_logging logging
    NAME pico_logging_ring_sink_tests
    SOURCES ring_sink_test.cpp
    LIBRARIES Threads::Threads pico_logging::testing
)

# CR-4 under ThreadSanitizer. The concurrent RingSink case is the only one in
# this repository with more than one thread, and a data race the stream check
# happens not to observe is still a defect, so the same source is built a
# second time with -fsanitize=thread. Not with the sanitizer leg: TSan and ASan
# cannot share an image.
#
# add_test rather than gtest_discover_tests, which would run the binary at
# build time outside the wrapper below. setarch -R turns off address-space
# randomisation for the run: TSan refuses to start where the kernel randomises
# mmap with more entropy than its shadow layout allows, as current Linux and
# WSL kernels do. halt_on_error=1 ends the run at the first report, so a
# regression fails at once rather than logging every racing access.
if(NOT PICO_LOGGING_SANITIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    find_program(PICO_LOGGING_SETARCH setarch REQUIRED)
    add_executable(pico_logging_ring_sink_tsan_tests ring_sink_test.cpp)
    target_compile_features(pico_logging_ring_sink_tsan_tests PRIVATE cxx_std_20)
    target_compile_options(pico_logging_ring_sink_tsan_tests PRIVATE -fsanitize=thread)
    target_link_options(pico_logging_ring_sink_tsan_tests PRIVATE -fsanitize=thread)
    target_link_libraries(pico_logging_ring_sink_tsan_tests PRIVATE
        pico_logging_strict
        pico_logging::logging
        pico_logging::testing
        Threads::Threads
        GTest::gtest_main
    )
    add_test(NAME RingSinkTest.UnderThreadSanitizer
        COMMAND ${PICO_LOGGING_SETARCH} ${CMAKE_HOST_SYSTEM_PROCESSOR} -R
                $<TARGET_FILE:pico_logging_ring_sink_tsan_tests>)
    set_tests_properties(RingSinkTest.UnderThreadSanitizer PROPERTIES
        ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1"
        TIMEOUT 120)
endif()

# OR-2, structurally: see the script for the forms the behavioural test above
# cannot see.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME pico_logging_level_ceiling_is_macro_only
    COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/level_ceiling_is_macro_only.py
            ${PROJECT_SOURCE_DIR}/src)
# The check itself refuses each form it exists for, so a check that stopped
# looking fails here rather than passing everything.
add_test(NAME pico_logging_level_ceiling_check_refuses
    COMMAND ${Python3_EXECUTABLE} -I
            ${CMAKE_CURRENT_SOURCE_DIR}/level_ceiling_is_macro_only_control.py
            ${PROJECT_SOURCE_DIR}/src)

# FR-5: a fast-math build logs text and integers, and is refused a float. The
# probe compiles as written in every build; the PROBE=1 copy is built only by
# its test, which passes only on the refusal's own diagnostic.
add_library(pico_logging_fast_math_probe OBJECT fast_math_probe.cpp)
target_link_libraries(pico_logging_fast_math_probe
    PRIVATE pico_logging_strict pico_logging::logging)
target_compile_options(pico_logging_fast_math_probe PRIVATE -ffast-math)
add_library(pico_logging_refuses_fast_math_float OBJECT EXCLUDE_FROM_ALL
    fast_math_probe.cpp)
target_link_libraries(pico_logging_refuses_fast_math_float
    PRIVATE pico_logging_strict pico_logging::logging)
target_compile_options(pico_logging_refuses_fast_math_float PRIVATE -ffast-math)
target_compile_definitions(pico_logging_refuses_fast_math_float PRIVATE PROBE=1)
add_test(NAME pico_logging_refuses_fast_math_float
    COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -DTARGET=pico_logging_refuses_fast_math_float
            "-DEXPECT=needs IEEE floating-point semantics"
            -P ${PROJECT_SOURCE_DIR}/tests/expect_refusal.cmake)

# FR-5 under each flag the refusal reads, a probe per flag, so dropping a macro
# from the check fails its own case: fast math names them all, which is why it
# cannot stand for the others. -ffinite-math-only is named by every compiler;
# -fno-signed-zeros by GCC alone. GCC enables associative math only with signed
# zeros off, so __ASSOCIATIVE_MATH__ never appears without __NO_SIGNED_ZEROS__
# and has no case of its own; -funsafe-math-optimizations, which sets both,
# stands for it.
function(pico_logging_float_refusal NAME FLAG)
    add_library(${NAME} OBJECT EXCLUDE_FROM_ALL fast_math_probe.cpp)
    target_link_libraries(${NAME}
        PRIVATE pico_logging_strict pico_logging::logging)
    target_compile_options(${NAME} PRIVATE ${FLAG})
    target_compile_definitions(${NAME} PRIVATE PROBE=1)
    add_test(NAME ${NAME}
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR}
                -DTARGET=${NAME}
                "-DEXPECT=needs IEEE floating-point semantics"
                -P ${PROJECT_SOURCE_DIR}/tests/expect_refusal.cmake)
endfunction()
pico_logging_float_refusal(pico_logging_refuses_finite_math_float
    -ffinite-math-only)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    pico_logging_float_refusal(pico_logging_refuses_unsafe_math_float
        -funsafe-math-optimizations)
    # -fno-signed-zeros alone, so only __NO_SIGNED_ZEROS__ is defined.
    pico_logging_float_refusal(pico_logging_refuses_unsigned_zero_float
        -fno-signed-zeros)
endif()
