# Test support that ships (EN-04.1): its consumers are the sibling repositories'
# test trees, and a hand-written double drifts from the seam it stands in for.
#
# Header-only, so INTERFACE -- CaptureSink is a few inline members over
# std::string and std::vector, and adding a translation unit would give a
# consumer an object to link for nothing.
#
# Host-only. It uses the heap freely, which is right for a fake and wrong for a
# firmware image; the cross build never creates this target, and the export set
# names it only on a host build for the same reason.
add_library(pico_logging_testing INTERFACE)
add_library(pico_logging::testing ALIAS pico_logging_testing)

target_sources(pico_logging_testing INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES
        include/pico_logging/testing/capture_sink.hpp
)

target_include_directories(pico_logging_testing INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_logging_testing INTERFACE cxx_std_20)
target_link_libraries(pico_logging_testing INTERFACE pico_logging::logging)
