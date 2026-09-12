#!/usr/bin/env python3
"""Run native query-settings recovery against owned, corrupt SQLite fixtures."""

import argparse
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Native query_settings_corruption_test binary")
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    schema = (Path(__file__).resolve().parents[2] / "crates/storage/src/schema.sql").read_text()
    cases = {
        "malformed": "{broken query preferences",
        "out_of_range": json.dumps({"version": 1, "page_size": 99, "timeout_seconds": 0}),
    }
    for name, corrupt in cases.items():
        with tempfile.TemporaryDirectory(prefix=f"choscordb-query-settings-{name}-") as directory:
            database = Path(directory) / "metadata.sqlite"
            with sqlite3.connect(database) as connection:
                connection.executescript(schema)
                connection.execute("CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY)")
                connection.execute("INSERT INTO schema_migrations(version) VALUES (1)")
                connection.execute(
                    "INSERT INTO settings(key,value) VALUES ('query_preferences',?)", (corrupt,)
                )
            environment = os.environ.copy()
            environment["CHOSCORDB_TEST_CORRUPT_QUERY_SETTINGS_DATABASE"] = str(database)
            environment.setdefault("QT_QPA_PLATFORM", "offscreen")
            print(f"Testing corrupt query settings: {name}", flush=True)
            subprocess.run(
                [str(binary), "corruptSettingsKeepQueriesUsableAndRequireExplicitRepair"],
                env=environment,
                check=True,
                timeout=90,
            )
            with sqlite3.connect(database) as connection:
                row = connection.execute(
                    "SELECT value FROM settings WHERE key='query_preferences'"
                ).fetchone()
                expected = {"version": 1, "page_size": 222, "timeout_seconds": 0}
                if row is None or json.loads(row[0]) != expected:
                    raise AssertionError(f"{name}: native repair was not persisted: {row!r}")
    print("Native malformed and out-of-range query settings repair passed.", flush=True)


if __name__ == "__main__":
    main()
