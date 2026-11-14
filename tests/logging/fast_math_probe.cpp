// SPDX-License-Identifier: MIT
// The formatter under the floating-point flags that make a float log statement
// fail to compile. As written, logging text and integers, this builds under
// -ffast-math, so a fast-math consumer can log. PROBE=1 streams a double, and
// the build must then fail with the reason, once per flag. The object has a
// main() so that it is a program's and not a stray external function.
// FR-5
#include "pico_logging/logging/logging.hpp"

int main() {
    constexpr int COUNT = 3;
    PICO_LOG_INFO << "count " << COUNT;
#if defined(PROBE) && PROBE == 1
    PICO_LOG_INFO << 1.5;
#endif
    return 0;
}
