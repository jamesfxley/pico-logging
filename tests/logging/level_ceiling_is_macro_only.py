#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""OR-2: the compile-time ceiling reaches no linked entity.

A behavioural test cannot hold this: an inline constexpr variable or an
inlined function reading the ceiling is an ODR violation the optimiser folds
away, so each unit still behaves as compiled. This holds it structurally.

The names checked are the ceiling's three macros and every macro whose #define
names one of them, directly or through another -- so a statement macro, an
enable macro or an alias counts as the ceiling. Outside comments, every line
of every file under src/ naming one of them must be a preprocessor directive
or a line of a #define continued onto it, and a conditional naming one may
contain only preprocessor lines: code inside it would be a definition
selected by the ceiling.

    level_ceiling_is_macro_only.py <repository>/src

Exit status: 0 when the tree holds, 1 when the ceiling reaches code, 2 when the
sources could not be listed.
"""

import re
import sys
from pathlib import Path

BASE = (
    "PICO_LOGGING_EFFECTIVE_LEVEL",
    "LOGGING_GLOBAL_LEVEL",
    "PICO_LOGGING_DEFAULT_CEILING",
)
CONDITIONAL = re.compile(r"#[ \t]*if")
ALTERNATIVE = re.compile(r"#[ \t]*elif")
END = re.compile(r"#[ \t]*endif")
DEFINITION = re.compile(r"#[ \t]*define[ \t]+[A-Za-z_]")
DEFINE_PREFIX = re.compile(r"#[ \t]*define[ \t]+")
DEFINE_LINE = re.compile(r"#[ \t]*define[ \t]")
DIRECTIVE = re.compile(
    r"#[ \t]*(define|undef|if|ifdef|ifndef|elif|else|endif)([ \t]|$)",
)
NAME_END = re.compile(r"[^A-Za-z0-9_].*$", re.DOTALL)


class SourcesError(Exception):
    """The sources under the directory could not be listed."""


def sources_under(directory):
    """Return every file under the directory except CMake lists, sorted."""
    try:
        found = sorted(
            path
            for path in Path(directory).rglob("*")
            if path.is_file() and path.name != "CMakeLists.txt"
        )
    except OSError as error:
        message = f"could not list the sources under {directory}: {error}"
        raise SourcesError(message) from error
    if not found:
        message = f"no sources under {directory}"
        raise SourcesError(message)
    return found


def lines_of(path):
    """Return a file's lines, without line terminators."""
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def definitions(paths):
    """Return (name, body) for every #define, across continuations.

    The body carries the conditions of the conditionals the definition sits in:
    a macro defined under a condition on the ceiling carries the ceiling as
    surely as one whose body names it.
    """
    found = []
    name = ""
    body = ""
    continued = False
    for path in paths:
        continued = False
        conditions = []
        for raw in lines_of(path):
            line = raw.lstrip(" \t")
            if not continued and CONDITIONAL.match(line):
                conditions.append(line)
            elif not continued and ALTERNATIVE.match(line) and conditions:
                conditions[-1] += " " + line
            elif not continued and END.match(line) and conditions:
                conditions.pop()
            starts = not continued and DEFINITION.match(line) is not None
            if starts:
                rest = DEFINE_PREFIX.sub("", line, count=1)
                name = NAME_END.sub("", rest)
                enclosing = "".join(" " + condition for condition in conditions)
                body = rest[len(name) :] + enclosing
            elif continued:
                body += " " + line
            continued = (continued or starts) and raw.endswith("\\")
            if name and not continued:
                found.append((name, body))
                name = ""
    return found


def word(names):
    """Return a pattern matching any of the names as a whole identifier."""
    alternatives = "|".join(re.escape(name) for name in names)
    return re.compile(f"(^|[^A-Za-z0-9_])({alternatives})([^A-Za-z0-9_]|$)")


def ceiling_names(defined):
    """Return the ceiling's macros and every macro built on them, in order."""
    names = list(BASE)
    while True:
        pattern = word(names)
        added = sorted(
            {
                name
                for name, body in defined
                if name not in names and pattern.search(body)
            },
        )
        if not added:
            return names
        names.extend(added)


def reaches_code(paths, names):
    """Return a description of each line by which the ceiling reaches code."""
    pattern = word(names)
    offending = []
    for path in paths:
        continued = False
        guarded = {}
        depth = 0
        for number, raw in enumerate(lines_of(path), start=1):
            line = raw.lstrip(" \t")
            comment = line.startswith("//")
            names_it = pattern.search(raw) is not None and not comment
            if not continued and CONDITIONAL.match(line):
                depth += 1
                guarded[depth] = names_it or (
                    depth > 1 and guarded.get(depth - 1, False)
                )
            elif not continued and ALTERNATIVE.match(line) and names_it:
                guarded[depth] = True
            elif not continued and END.match(line) and depth > 0:
                depth -= 1
            directive = continued or line.startswith("#")
            allowed = continued or DIRECTIVE.match(line) is not None
            if names_it and not allowed:
                offending.append(f"{path}:{number}: {line}")
            elif (
                depth > 0
                and guarded.get(depth)
                and not directive
                and not comment
                and line
            ):
                offending.append(f"{path}:{number}: selected by the ceiling: {line}")
            continued = (
                continued or DEFINE_LINE.match(line) is not None
            ) and raw.endswith(
                "\\",
            )
    return offending


def main(argv):
    """Check the directory named by argv[0]; return the exit status."""
    if len(argv) != 1:
        print("usage: level_ceiling_is_macro_only.py <src dir>", file=sys.stderr)
        return 2
    try:
        paths = sources_under(argv[0])
    except SourcesError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    names = ceiling_names(definitions(paths))
    offending = reaches_code(paths, names)
    if offending:
        print(
            "error: the compile-time ceiling reaches code outside a macro:",
            file=sys.stderr,
        )
        print("\n".join(offending), file=sys.stderr)
        return 1
    built = len(names) - len(BASE)
    print(
        f"ceiling and {built} macro(s) built on it named only in macros "
        f"across {len(paths)} file(s)",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
