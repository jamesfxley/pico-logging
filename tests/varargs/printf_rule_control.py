#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""AR-1: the printf rule in src/.clang-tidy refuses a printf-family call.

The rule is bugprone-unsafe-functions with CustomFunctions set to the printf
family (see src/.clang-tidy). A rule only ever seen to pass is not evidence, so
this runs clang-tidy with that configuration over a fixture calling snprintf and
passes only when the call is reported at its line with the rule's reason, the
run fails, and a call to a default-list function (rewind) is not reported. The
configurations are copied, not rewritten, so the test reads what the shipped
tree reads.

    printf_rule_control.py <repository root>

Exit status: 0 when the call is refused as described, 1 when it is not, 2 when
clang-tidy is absent.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REASON = "newlib's floating-point conversions allocate"


def find_tool(root):
    """Return the pinned clang-tidy, or None when no candidate is installed."""
    pin = (root / "infra/house/config/.clang-version").read_text()
    words = [line.split("#", 1)[0].strip() for line in pin.splitlines()]
    major = " ".join(word for word in words if word)
    if not major.isdecimal():
        return None
    for name in (f"clang-tidy-{major}", "clang-tidy"):
        found = shutil.which(name)
        if found:
            return found
    return None


def line_of(fixture, needle):
    """Return the 1-based number of the first fixture line holding the text."""
    for number, line in enumerate(fixture.read_text(encoding="utf-8").splitlines(), 1):
        if needle in line:
            return number
    message = f"{fixture} has no line holding {needle}"
    raise ValueError(message)


def fail(text, status):
    """Report a failure on standard error; return the exit status."""
    print(text, file=sys.stderr)
    return status


def main(argv):
    """Run clang-tidy over the fixture; return the exit status."""
    if len(argv) != 1:
        print("usage: printf_rule_control.py <repository root>", file=sys.stderr)
        return 2
    root = Path(argv[0])
    tool = find_tool(root)
    if tool is None:
        return fail("error: no clang-tidy on PATH; the rule was not exercised", 2)
    fixture = root / "tests/varargs/varargs_call.cpp.in"
    expected = f"varargs_call.cpp:{line_of(fixture, 'std::snprintf')}:"
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        (work / "src").mkdir()
        shutil.copy2(root / ".clang-tidy", work / ".clang-tidy")
        shutil.copy2(root / "src/.clang-tidy", work / "src/.clang-tidy")
        source = work / "src/varargs_call.cpp"
        shutil.copy2(fixture, source)
        done = subprocess.run(
            [tool, "--quiet", str(source), "--", "-std=c++20"],
            capture_output=True,
            text=True,
            check=False,
        )
    said = done.stdout + done.stderr
    for want in (expected, "bugprone-unsafe-functions", REASON):
        if want not in said:
            return fail(f"error: the rule's output lacks {want!r}:\n{said}", 1)
    if "function 'rewind'" in said:
        return fail(f"error: a default-list function was reported:\n{said}", 1)
    if done.returncode == 0:
        return fail(f"error: reported but the run passed:\n{said}", 1)
    print("the printf rule refuses a printf-family call, naming its line and reason")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
