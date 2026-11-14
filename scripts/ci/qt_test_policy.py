#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import sys

from quality import CPP_SUFFIXES


ROOT = Path(__file__).resolve().parents[2]
PARTIAL_READ = re.compile(r"\bwaitForReadyRead\s*\(")


def violations(root):
    problems = []
    for path in sorted((root / "tests").rglob("*")):
        if not path.is_file() or path.suffix not in CPP_SUFFIXES:
            continue
        lines = path.read_text(encoding="utf-8").splitlines()
        for line_number, line in enumerate(lines, 1):
            if PARTIAL_READ.search(line):
                problems.append(
                    f"{path.relative_to(root).as_posix()}:{line_number}: waitForReadyRead "
                    "can return before a full line (use QTRY_VERIFY(process.canReadLine()))"
                )
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Reject Qt test waits that can race with partial process output."
    )
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    problems = violations(args.root)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
