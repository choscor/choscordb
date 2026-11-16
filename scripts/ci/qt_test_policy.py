#!/usr/bin/env python3

import argparse
from pathlib import Path
import re
import sys

from quality import CPP_SUFFIXES


ROOT = Path(__file__).resolve().parents[2]
PARTIAL_READ = re.compile(r"\bwaitForReadyRead\s*\(")
SPINNER_TURN = re.compile(r"\bisTurning\s*\(")
MOTION_POLICY = re.compile(r"\b(?:setReducedMotion|reducedMotion)\b")
FILE_CALL = re.compile(r"\b(\w+)\s*(?:\.|->)\s*(write|flush|close|setFileTime)\s*\(")


def violations(root):
    problems = []
    for path in sorted((root / "tests").rglob("*")):
        if not path.is_file() or path.suffix not in CPP_SUFFIXES:
            continue
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        sets_motion_policy = MOTION_POLICY.search(text) is not None
        unflushed = set()
        for line_number, line in enumerate(lines, 1):
            if SPINNER_TURN.search(line) and not sets_motion_policy:
                problems.append(
                    f"{path.relative_to(root).as_posix()}:{line_number}: isTurning depends on "
                    "the host reduced-motion setting (set ThemeManager::setReducedMotion "
                    "or the theme reducedMotion field)"
                )
            for name, call in FILE_CALL.findall(line):
                if call == "write":
                    unflushed.add(name)
                elif call == "setFileTime" and name in unflushed:
                    problems.append(
                        f"{path.relative_to(root).as_posix()}:{line_number}: setFileTime after "
                        "an unflushed write is lost when QFile flushes its buffer "
                        f"(call {name}.flush() first)"
                    )
                else:
                    unflushed.discard(name)
            if PARTIAL_READ.search(line):
                problems.append(
                    f"{path.relative_to(root).as_posix()}:{line_number}: waitForReadyRead "
                    "can return before a full line (use QTRY_VERIFY(process.canReadLine()))"
                )
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Reject Qt test waits and assertions that depend on races or host settings."
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
