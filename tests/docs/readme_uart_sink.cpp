// SPDX-License-Identifier: MIT
// The README's custom sink, over a UART write the application provides.
#include <pico_logging/logging/logging.hpp>
#include <string_view>

namespace doc_examples {
void uart_write(std::string_view text);

#include "readme_uart_sink.inc"
}  // namespace doc_examples
