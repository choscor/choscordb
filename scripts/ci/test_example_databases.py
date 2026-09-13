"""Behavior checks for the runnable example databases."""

from contextlib import closing
import importlib.util
from pathlib import Path
import sqlite3
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SETUP_PATH = ROOT / "examples" / "databases" / "setup_sqlite.py"


def load_setup_module():
    spec = importlib.util.spec_from_file_location("setup_sqlite", SETUP_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load the SQLite example setup module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ExampleDatabaseTest(unittest.TestCase):
    def test_sqlite_setup_creates_queryable_demo_data(self):
        setup = load_setup_module()
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "demo.sqlite"
            setup.create_database(database)

            with closing(sqlite3.connect(database)) as connection:
                self.assertEqual(
                    connection.execute("PRAGMA user_version").fetchone(), (1,)
                )
                self.assertEqual(
                    connection.execute("SELECT count(*) FROM customers").fetchone(),
                    (5,),
                )
                self.assertEqual(
                    connection.execute("SELECT count(*) FROM products").fetchone(),
                    (6,),
                )
                self.assertEqual(
                    connection.execute("SELECT count(*) FROM orders").fetchone(),
                    (8,),
                )
                self.assertEqual(
                    connection.execute(
                        "SELECT customer_name, total FROM order_summary "
                        "ORDER BY total DESC LIMIT 1"
                    ).fetchone(),
                    ("Minh Nguyen", 377),
                )

    def test_sqlite_setup_preserves_an_existing_file_without_force(self):
        setup = load_setup_module()
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "existing.sqlite"
            database.write_bytes(b"keep me")

            with self.assertRaisesRegex(FileExistsError, "--force"):
                setup.create_database(database)

            self.assertEqual(database.read_bytes(), b"keep me")

    def test_sqlite_setup_force_replaces_an_existing_file(self):
        setup = load_setup_module()
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "existing.sqlite"
            database.write_bytes(b"replace me")

            setup.create_database(database, force=True)

            with closing(sqlite3.connect(database)) as connection:
                self.assertEqual(
                    connection.execute("SELECT count(*) FROM order_items").fetchone(),
                    (13,),
                )


if __name__ == "__main__":
    unittest.main()
