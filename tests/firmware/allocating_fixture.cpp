// SPDX-License-Identifier: MIT
// Negative control for the firmware allocation gate (AR-1). This image
// deliberately reaches newlib's allocating float formatter, and the
// allocating-formatter set of the symbols gate must reject it: the `allocation
// refuses its fixture` table in pico.toml (`expect = "refused"`).
//
// std::to_string and not a printf-family call, which would need a suppression.
//
// Do not fix this file. Its defect is the fixture.

#include <string>

namespace {
constexpr double DIVISOR = 3.0;
}  // namespace

int main() { return static_cast<int>(std::to_string(1.0 / DIVISOR).size()); }
