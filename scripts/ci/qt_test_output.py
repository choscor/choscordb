#!/usr/bin/env python3
"""Relay Qt Test's file output for Windows GUI-subsystem test executables."""

import subprocess
import sys
from tempfile import TemporaryDirectory
from pathlib import Path


def main() -> int:
    with TemporaryDirectory() as directory:
        output = Path(directory) / "qtest.txt"
        result = subprocess.run([sys.argv[1], "-o", f"{output},txt"], check=False)
        if output.exists():
            print(output.read_text(encoding="utf-8", errors="replace"), flush=True)
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
