// SPDX-License-Identifier: MIT
// OR-2, OR-4: one target of a consumer overrides the ceiling the package was
// configured with. The package's value is the default for every target that
// says nothing, and this target's own LOGGING_GLOBAL_LEVEL wins -- without a
// redefinition, which this target's -Werror would refuse.
#include "pico_logging/pico_logging.hpp"

static_assert(PICO_LOGGING_EFFECTIVE_LEVEL == 1,
              "a target's own LOGGING_GLOBAL_LEVEL did not override the "
              "package's configured ceiling");

int main() {
    return pico_logging::VERSION_MAJOR == 0 && pico_logging::VERSION_MINOR == 1
               ? 0
               : 1;
}
