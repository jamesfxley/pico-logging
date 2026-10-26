cmake_minimum_required(VERSION 3.26)

# The declared consumer case "pico-w-refused" (house.toml), AR-6: a consumer
# that adds this library on a board without an RP2350 is refused at configure,
# and the refusal names what is missing. The consumer imports the SDK and adds
# this library as a subdirectory, so the board is the consumer's, which is the
# case the check is for. The case passes only when this configure fails with
# the library's own message.
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pico_logging_consumer_board C CXX ASM)
pico_sdk_init()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. library)
