# Test support that ships: its consumers are the sibling repositories' test
# trees.
#
# Header-only, so INTERFACE: a translation unit would give a consumer an object
# to link for nothing.
#
# Host-only: it uses the heap freely, which suits a fake and not a firmware
# image, so the cross build never creates it and the export set names it only
# on a host build.
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
