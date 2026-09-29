# Driver contracts

Application services depend on these traits and application-owned values. Each
database adapter implements the same public boundary without exposing its client
library's connection, statement, row, or error types:

- `DatabaseDriver`: identity, capabilities, connection setup, and reconnect.
- `Connection`: execution, transaction control, metadata, editing, and close.
- `ResultCursor`: owned column metadata, bounded pages, summaries, and close.
- `CancelHandle`: cancellation scoped to one execution generation.
- `DeferredReader`: bounded value reads independent of the live database cursor.

`execute_bounded` and `fetch_page_bounded` are required methods. Implementations
must check schema limits before executing writes and check owned page allocations
before allocating them. The `execute` and `fetch_page` convenience defaults call
these methods with `DEFAULT_RESULT_MEMORY_BUDGET` (4 MiB). This budget does not
claim to bound allocations inside database engines or upstream protocol clients.

Optional features retain explicit unsupported or unknown defaults. Capabilities
describe database differences; adapters must implement features they advertise.
Connection close must terminate the session and roll back unfinished writes.
Cancellation handles cannot affect a later execution. Database-specific mechanics
belong in each driver, including PostgreSQL portals, MySQL result streams, and
SQLite's blocking worker.

`tests/support/driver_contract.rs` supplies one behavior suite invoked by all
three drivers' `tests/driver_contract.rs`. SQLite runs without an external server;
PostgreSQL and MySQL tests require disposable fixtures and are explicitly ignored
in ordinary unit-test runs. Their compile-fail trait examples also prevent an
adapter from inheriting a late, post-allocation budget check.
