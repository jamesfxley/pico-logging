#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The OR-2 structural check refuses each form by which the ceiling reaches code.

Each case appends one form to a copy of the sources and expects the check to
refuse it with status 1 and a finding; the tree itself must be accepted. A
check that stopped looking fails here rather than passing everything.

    level_ceiling_is_macro_only_control.py <repository>/src
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CHECK = Path(__file__).with_name("level_ceiling_is_macro_only.py")
HEADER = "logging/include/pico_logging/logging/logging.hpp"
EXTRA = "logging/include/pico_logging/logging/extra.h"


def source(*lines):
    """Return the lines as text ending in a newline."""
    return "\n".join(lines) + "\n"


CASES = (
    (
        "an inline constexpr variable",
        HEADER,
        source("inline constexpr int CEILING = PICO_LOGGING_EFFECTIVE_LEVEL;"),
    ),
    (
        "a function reading a macro that wraps the ceiling",
        HEADER,
        source(
            "namespace pico_logging { inline bool dbg_on() {",
            "return PICO_LOG_ENABLED_DBG; } }",
        ),
    ),
    (
        "a function logging through a statement macro",
        HEADER,
        source(
            "namespace pico_logging {",
            'inline void note() { PICO_LOG_DEBUG << "x"; }',
            "}",
        ),
    ),
    (
        "a definition selected by #if on the ceiling",
        HEADER,
        source(
            "#if PICO_LOGGING_EFFECTIVE_LEVEL >= 4",
            "inline constexpr bool DEBUG_ON = true;",
            "#else",
            "inline constexpr bool DEBUG_ON = false;",
            "#endif",
        ),
    ),
    (
        "a definition selected by #ifdef on the ceiling",
        HEADER,
        source(
            "#ifdef LOGGING_GLOBAL_LEVEL",
            "inline constexpr bool CEILING_OVERRIDDEN = true;",
            "#else",
            "inline constexpr bool CEILING_OVERRIDDEN = false;",
            "#endif",
        ),
    ),
    (
        "a macro selected by #if on the ceiling, then read",
        HEADER,
        source(
            "#if PICO_LOGGING_EFFECTIVE_LEVEL >= 4",
            "#define DEBUG_SELECTED 1",
            "#else",
            "#define DEBUG_SELECTED 0",
            "#endif",
            "inline constexpr bool DEBUG_ON = DEBUG_SELECTED;",
        ),
    ),
    (
        "a macro selected by #ifdef on the ceiling, then read",
        HEADER,
        source(
            "#ifdef LOGGING_GLOBAL_LEVEL",
            "#define CEILING_SET 1",
            "#else",
            "#define CEILING_SET 0",
            "#endif",
            "inline constexpr bool OVERRIDDEN = CEILING_SET;",
        ),
    ),
    (
        "a macro aliasing the ceiling, then read",
        HEADER,
        source(
            "#define CEILING_ALIAS PICO_LOGGING_EFFECTIVE_LEVEL",
            "inline constexpr int ALIASED = CEILING_ALIAS;",
        ),
    ),
    (
        "a macro aliasing the ceiling after a tab, then read",
        HEADER,
        source(
            "#define CEILING_TABBED\tPICO_LOGGING_EFFECTIVE_LEVEL",
            "inline constexpr int TABBED = CEILING_TABBED;",
        ),
    ),
    (
        "a use in a header with another extension",
        EXTRA,
        source("inline constexpr int CEILING = PICO_LOGGING_EFFECTIVE_LEVEL;"),
    ),
)


def run_check(directory):
    """Run the check on a directory; return the completed process."""
    return subprocess.run(
        [sys.executable, "-I", str(CHECK), str(directory)],
        capture_output=True,
        text=True,
        check=False,
    )


def refused(source, work, file, text):
    """Return a description of what went wrong, or None when the check refused."""
    copy = work / "src"
    shutil.rmtree(copy, ignore_errors=True)
    shutil.copytree(source, copy)
    with (copy / file).open("a", encoding="utf-8") as handle:
        handle.write(text)
    done = run_check(copy)
    if done.returncode != 1:
        return f"exited {done.returncode}, not 1; {done.stderr.strip()}"
    if file not in done.stderr and Path(file).name not in done.stderr:
        return f"did not name the file it refused: {done.stderr.strip()}"
    return None


def main(argv):
    """Run every case against the tree named by argv[0]; return the status."""
    if len(argv) != 1:
        print(
            "usage: level_ceiling_is_macro_only_control.py <src dir>",
            file=sys.stderr,
        )
        return 2
    source = Path(argv[0])
    accepted = run_check(source)
    if accepted.returncode:
        print(f"error: the check refused the tree: {accepted.stderr}", file=sys.stderr)
        return 1
    failed = False
    with tempfile.TemporaryDirectory() as temporary:
        for name, file, text in CASES:
            problem = refused(source, Path(temporary), file, text)
            if problem:
                print(
                    f"error: the check did not refuse {name}: {problem}",
                    file=sys.stderr,
                )
                failed = True
    if failed:
        return 1
    print("the check refused every form and accepted the tree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
