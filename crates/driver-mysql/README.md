# MySQL adapter

This crate implements `DatabaseDriver`, `Connection`, `ResultCursor`,
`CancelHandle`, and `DeferredReader` from `choscordb-driver-api`. It owns MySQL
connection setup, protocol result consumption, value conversion, transaction
state, metadata, editing, and error translation. `mysql_async` remains a registry
dependency; client-specific types stay inside the adapter.

Bounded execution and page fetching are required implementations. The shared
`execute` and `fetch_page` convenience methods use `DEFAULT_RESULT_MEMORY_BUDGET`.
MySQL result streaming and multiple result sets remain database-specific details;
the common contracts do not require PostgreSQL-style portals.

The shared driver conformance tests run against a disposable fixture:

```sh
cargo test -p choscordb-driver-mysql --test driver_contract -- --ignored
```

The fixture uses `127.0.0.1:33306` (override with `CHOSCORDB_MYSQL_PORT`), database
`choscordb_test`, user `root`, and the repository's test-only password. See
`scripts/integration/mysql_ci_fixture.py` for fixture setup.
