// SPDX-License-Identifier: MIT
// Negative control for the firmware heap gate (AR-1).
//
// This translation unit DELIBERATELY reaches the C++ allocation operators, and
// the c-heap-alloc set of the symbols gate, run over this object directory,
// must REJECT it. The firmware build compiles it alongside the real probe and
// the `heap refuses its fixture` table in pico.toml (`expect = "refused"`)
// asserts the gate fails here, so each run proves the gate still works on the
// same toolchain it is guarding.
//
// A gate that has only ever been seen to pass is not evidence. The object mode
// asks which symbols a translation unit leaves UNDEFINED, so a control that
// merely contains an allocator somewhere in its image would not exercise it --
// this one has to call the operator itself.
//
// It is not the control for the allocating-formatter gate: allocating_fixture
// beside this file reaches newlib's float formatter, whose undefined symbol is
// the formatter rather than the allocator, so neither fixture stands in for
// the other.
//
// std::vector rather than a bare `new` or std::malloc, for the reason
// allocating_fixture states about std::to_string: it reaches the operator by a
// route that needs no suppression, so a fixture that exists to be wrong does
// not spend the deviation budget saying so.
//
// Do not "fix" this file. Its defect is the fixture.

#include <cstddef>
#include <vector>

namespace {
constexpr std::size_t ELEMENTS = 8;
}  // namespace

int main() {
    // Published through a volatile: a container whose contents are never read
    // is dead code the optimiser may remove, and the gate would then be given
    // an object that reaches nothing.
    std::vector<int> values(ELEMENTS);
    const volatile int sink = values.front();
    return sink;
}
