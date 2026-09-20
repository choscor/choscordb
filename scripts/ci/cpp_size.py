#!/usr/bin/env python3
"""Limit handwritten C/C++/Objective-C++ sources and headers to 1,000 lines."""

import argparse
from pathlib import Path
import sys

from quality import CPP_SUFFIXES


ROOT = Path(__file__).resolve().parents[2]
MAX_LINES = 1000


def violations(root):
    problems = []
    for directory in (root / "desktop", root / "tests"):
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.suffix not in CPP_SUFFIXES:
                continue
            with path.open(encoding="utf-8") as source:
                lines = sum(1 for _ in source)
            if lines > MAX_LINES:
                problems.append(
                    f"{path.relative_to(root).as_posix()}:{MAX_LINES + 1}: "
                    f"{lines} lines exceeds {MAX_LINES}; split by responsibility"
                )
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    problems = violations(args.root)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
