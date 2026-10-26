# One module, not two. sinks.hpp is the base and logging.hpp includes it, but
# sinks.cpp implements StderrSink in terms of logging.hpp -- so splitting the
# interface from the statement stream would make each target need the other.
add_library(pico_logging_logging
    sinks.cpp
)
add_library(pico_logging::logging ALIAS pico_logging_logging)

# sinks.cpp static_asserts the umbrella header's advertised version against
# these, so the two records of the version cannot drift;
# tests/core/pico_logging_test.cpp pins the same constants from the host side.
house_cxx_version_definitions(pico_logging_logging pico_logging PRIVATE)

# Two BASE_DIRS, because this target carries headers belonging to two places:
# its own module and the umbrella at src/include/. The core and logging
# targets both carry that installed umbrella header.
target_sources(pico_logging_logging PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PICO_LOGGING_PUBLIC_INCLUDE_DIR}
    FILES
        include/pico_logging/logging/logging.hpp
        include/pico_logging/logging/ring_sink.hpp
        include/pico_logging/logging/sinks.hpp
        ${PICO_LOGGING_PUBLIC_INCLUDE_DIR}/pico_logging/pico_logging.hpp
)

target_include_directories(pico_logging_logging PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PICO_LOGGING_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_logging_logging PUBLIC cxx_std_20)

target_link_libraries(pico_logging_logging
    # PUBLIC: RingSink stores its slots in a pico_util::RingBuffer, so that
    # header reaches a consumer through ring_sink.hpp.
    PUBLIC pico_util::core
    PRIVATE pico_logging_strict)
