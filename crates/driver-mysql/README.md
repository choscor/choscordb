# MySQL and MariaDB adapter

This crate implements `DatabaseDriver`, `Connection`, `ResultCursor`,
`CancelHandle`, and `DeferredReader` from `choscordb-driver-api`. It owns MySQL
connection setup, protocol result consumption, value conversion, transaction
state, metadata, editing, and error translation. `mysql_async` remains a registry
dependency; client-specific types stay inside the adapter.

Bounded execution and page fetching are required implementations. The shared
`execute` and `fetch_page` convenience methods use `DEFAULT_RESULT_MEMORY_BUDGET`.
MySQL result streaming and multiple result sets remain database-specific details;
the common contracts do not require PostgreSQL-style portals.

`MariadbDriver` (id `mariadb`) shares this implementation through `Flavor`. The
MariaDB flavor maps sequences and system-versioned tables in the catalog, edits
and reads the history of system-versioned tables, and decodes columns with a
`json_valid` check as JSON. Each driver connects with its own flavor and reports a
notice when the server is the other product. Accounts that require MariaDB's
`parsec` plugin fail with an authentication error; `ed25519` accounts connect.

The shared driver conformance tests run against a disposable fixture:

```sh
cargo test -p choscordb-driver-mysql --test driver_contract -- --ignored
```

The fixture uses `127.0.0.1:33306` (override with `CHOSCORDB_MYSQL_PORT`), database
`choscordb_test`, user `root`, and the repository's test-only password. See
`scripts/integration/mysql_ci_fixture.py` for fixture setup.

To run the same suites, plus the MariaDB-only tests, against MariaDB 11.4:

```sh
python scripts/integration/mysql_ci_fixture.py start --flavor mariadb --bin-dir "$(brew --prefix mariadb@11.4)/bin"
CHOSCORDB_MARIADB_PORT=33307 cargo test -p choscordb-driver-mysql --lib --test live --test editing --test driver_contract --test mariadb -- --include-ignored --test-threads=1
```
