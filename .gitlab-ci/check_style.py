#!/usr/bin/env python3

import difflib
import io
from pathlib import Path
import shlex
import subprocess
import sys


def check_file(path):
    original = Path(path).read_bytes()
    formatted = subprocess.run(
        ["uncrustify", "-c", "uncrustify.cfg", "-f", path],
        capture_output=True,
        check=True,
    ).stdout

    if original == formatted:
        return True

    diff = difflib.unified_diff(
        # Split only at LF, preserving CRLF and embedded characters such as FF.
        list(io.StringIO(original.decode("utf-8"))),
        list(io.StringIO(formatted.decode("utf-8"))),
        fromfile=f"a/{path}",
        tofile=f"b/{path}",
    )

    for line in diff:
        print(line, end="")
        if not line.endswith("\n"):
            print("\n\\ No newline at end of file")
    return False


def main():
    changed_files = []
    try:
        paths = subprocess.check_output(
            ["git", "ls-files", "-z", "--", "*.c", "*.h"],
            stderr=subprocess.PIPE,
        ).decode("utf-8").split("\0")
        for path in filter(None, paths):
            if not check_file(path):
                changed_files.append(path)
    except (OSError, UnicodeError, subprocess.CalledProcessError) as error:
        diagnostic = str(error)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            diagnostic += "\n" + error.stderr.decode("utf-8", errors="replace").strip()
        print(f"  Style check failed: {diagnostic}", file=sys.stderr)
        return 2

    print(f"  Uncrustify: {len(changed_files)} file(s) need formatting.")
    if changed_files:
        command = shlex.join([
            "uncrustify", "-c", "uncrustify.cfg", "--replace", "--no-backup",
            *changed_files,
        ])
        print(f"\n    To apply these changes, run:\n   \033[1m{command}\033[0m")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
