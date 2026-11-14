// SPDX-License-Identifier: MIT
// Negative control for the firmware heap gate (AR-1). This unit deliberately
// reaches the C++ allocation operators, and the c-heap-alloc set of the symbols
// gate must reject it: the `heap refuses its fixture` table in pico.toml
// (`expect = "refused"`). Object mode asks which symbols a unit leaves
// undefined, so the fixture has to call the operator itself.
//
// std::vector and not a bare `new` or std::malloc, which would need a
// suppression.
//
// Do not fix this file. Its defect is the fixture.

#include <cstddef>
#include <vector>

namespace {
constexpr std::size_t ELEMENTS = 8;
}  // namespace

int main() {
    // Published through a volatile: a container never read is dead code the
    // optimiser removes, leaving the gate an object that reaches nothing.
    std::vector<int> values(ELEMENTS);
    const volatile int sink = values.front();
    return sink;
}
