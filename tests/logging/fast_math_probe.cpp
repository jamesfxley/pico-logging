// SPDX-License-Identifier: MIT
// FR-5 under the floating-point flags the float refusal reads. The formatter
// needs IEEE semantics: under finite math inf and nan cannot be promised, and
// under unsafe math the correctly rounded digits and the sign of zero are
// lost. As written, logging text and integers, this builds under -ffast-math,
// so a fast-math consumer can log; PROBE=1 streams a double and must be
// refused with the reason, once per flag. main() so the object is a program's,
// not a stray external function.
#include "pico_logging/logging/logging.hpp"

int main() {
    constexpr int COUNT = 3;
    PICO_LOG_INFO << "count " << COUNT;
#if defined(PROBE) && PROBE == 1
    PICO_LOG_INFO << 1.5;
#endif
    return 0;
}
