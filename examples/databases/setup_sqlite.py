#!/usr/bin/env python3
"""Create the ChoscorDB SQLite example database."""

import argparse
from contextlib import closing
import os
from pathlib import Path
import sqlite3
import tempfile


DEFAULT_OUTPUT = Path("build/examples/choscordb-demo.sqlite")
SCHEMA = Path(__file__).with_name("sqlite.sql")


def create_database(output: Path, *, force: bool = False) -> None:
    """Create a validated database, replacing output only when explicitly allowed."""
    output = output.resolve()
    if output.exists() and not force:
        raise FileExistsError(f"{output} already exists; pass --force to replace it")

    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with closing(sqlite3.connect(temporary)) as connection:
            connection.executescript(SCHEMA.read_text(encoding="utf-8"))
            result = connection.execute("PRAGMA integrity_check").fetchone()
            if result != ("ok",):
                raise RuntimeError(f"SQLite integrity check failed: {result!r}")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--force", action="store_true", help="replace an existing output database"
    )
    args = parser.parse_args()
    try:
        create_database(args.output, force=args.force)
    except (FileExistsError, OSError, RuntimeError, sqlite3.Error) as error:
        parser.exit(1, f"error: {error}\n")
    print(f"SQLite example ready: {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
