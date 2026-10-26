// SPDX-License-Identifier: MIT
// Negative control for the firmware allocation gate (AR-1).
//
// This image DELIBERATELY reaches newlib's allocating float formatter, and the
// allocating-formatter set of the symbols gate must REJECT it. The firmware
// build links it alongside the real probe and the `allocation refuses its
// fixture` table in pico.toml (`expect = "refused"`) asserts the gate fails
// here, so each run proves the gate still works on the same toolchain it is
// guarding.
//
// A gate that has only ever been seen to pass is not evidence: a script whose
// symbol names have gone stale, or whose nm invocation silently returns
// nothing, reports success for every image it is given.
//
// std::to_string rather than a printf-family call, for two reasons. It needs no
// suppression, so the fixture does not spend the deviation budget on code that
// exists to be wrong. And it reaches the formatter by a route
// the printf rule in src/.clang-tidy cannot see, which is the point of having
// both checks: the source rule catches the reintroduction people actually
// write, and the symbol gate catches every route to the same place.
//
// Do not "fix" this file. Its defect is the fixture.

#include <string>

namespace {
constexpr double DIVISOR = 3.0;
}  // namespace

int main() { return static_cast<int>(std::to_string(1.0 / DIVISOR).size()); }
