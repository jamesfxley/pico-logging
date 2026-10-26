set(PICO_LOGGING_PUBLIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(logging)

# The capture sink stands in for a Sink a consumer's test would otherwise
# hand-roll. Host-only: it uses the heap, which is right for a fake and wrong
# for a firmware image.
if(NOT PICO_LOGGING_BUILD_FIRMWARE)
    add_subdirectory(testing)
endif()

# The umbrella, belonging to no module (CXX-13.1) but installed by some target.
# INTERFACE: it carries the one header and links the module that implements it,
# so a consumer needs one target.
add_library(pico_logging_core INTERFACE)
add_library(pico_logging::core ALIAS pico_logging_core)

target_sources(pico_logging_core INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${PICO_LOGGING_PUBLIC_INCLUDE_DIR}
    FILES
        ${PICO_LOGGING_PUBLIC_INCLUDE_DIR}/pico_logging/pico_logging.hpp
)

target_include_directories(pico_logging_core INTERFACE
    $<BUILD_INTERFACE:${PICO_LOGGING_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(pico_logging_core INTERFACE cxx_std_20)

target_link_libraries(pico_logging_core INTERFACE pico_logging::logging)
