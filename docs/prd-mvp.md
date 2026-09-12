# ChoscorDB

Product requirements and technical specification for a lightweight, open-source, cross-platform SQL client.

Status: Draft v1  
Target: Windows, macOS, Linux  
License: GPL-3.0-or-later  
Architecture: Qt 6 Widgets UI + Rust core

## 1. Product overview

ChoscorDB is a fast and memory-efficient alternative to DBeaver.

The application provides:

- Database connection management
- Database and schema navigation
- SQL editing and execution
- Large result-set browsing
- Query cancellation
- Data export
- Query history
- Basic row editing

The first release supports PostgreSQL and SQLite. MySQL/MariaDB follows after the core architecture is stable.

## 2. Goals

- Start quickly.
- Use less memory than JVM- and Chromium-based database clients.
- Keep the UI responsive during database operations.
- Browse large result sets without loading every row into memory.
- Provide a clean cross-platform desktop experience.
- Keep the database engine independent from Qt.
- Make new database drivers easy to implement and test.

## 3. Non-goals for MVP

- Full DBeaver feature parity
- ER diagram editor
- Cloud synchronization
- Team collaboration
- AI-generated SQL
- Browser or mobile versions
- Third-party binary plugins
- Oracle, DB2, Redis, Cassandra, and MongoDB support

## 4. Target users

- Software developers
- Data analysts
- Database administrators
- Students learning SQL
- Users who want a small native database client

## 5. MVP features

### 5.1 Connections

- Create, edit, duplicate, test, and delete connection profiles.
- Support PostgreSQL and SQLite.
- Support PostgreSQL TLS settings.
- Store passwords in the operating-system credential manager.
- Never store passwords in the application SQLite database.
- Show connection errors with SQLSTATE or vendor error code when available.
- Support multiple active connections.

### 5.2 Database navigator

- Display connections, databases, schemas, tables, views, columns, keys, and indexes.
- Load children only when a tree node is expanded.
- Refresh one node without rebuilding the complete tree.
- Generate basic `SELECT`, `INSERT`, `UPDATE`, and `DELETE` statements.
- Copy qualified object names.
- Display object DDL when supported.

### 5.3 SQL editor

- Multiple editor tabs
- Syntax highlighting
- Line numbers
- Search and replace
- Undo and redo
- Bracket matching
- Execute selected SQL
- Execute the statement at the cursor
- Cancel an active query
- Save and open UTF-8 SQL files
- Basic completion for tables, columns, schemas, and SQL keywords
- Configurable font and keyboard shortcuts

### 5.4 Query execution

- Run all database work outside the Qt UI thread.
- Display queued, running, cancelling, completed, failed, and disconnected states.
- Support query timeout.
- Support database-native cancellation when available.
- Display duration, affected rows, warnings, and errors.
- Support auto-commit and manual transactions.
- Never automatically repeat a write after reconnecting.

### 5.5 Result grid

- Use `QTableView` with a custom `QAbstractTableModel`.
- Fetch results in pages.
- Default page size: 1,000 rows.
- Allow page sizes from 100 to 10,000 rows.
- Use an LRU page cache with a byte limit.
- Default limit: 64 MiB per result and 256 MiB for the application.
- Represent `NULL` separately from an empty string.
- Support numbers, decimals, text, booleans, dates, timestamps, UUIDs, JSON, and binary values.
- Load large text and BLOB values only when requested.
- Copy selected cells, rows, or complete pages.
- Do not execute `COUNT(*)` automatically.

### 5.6 Export

- Export CSV, JSON, JSON Lines, and SQL `INSERT` statements.
- Stream rows directly from the database cursor to a temporary file.
- Do not load the complete export into memory.
- Support cancellation.
- Rename the temporary file only after a successful export.
- Remove incomplete temporary files after cancellation or failure.

### 5.7 History and recovery

- Store query text, connection profile, timestamp, duration, status, and row count.
- Default history retention: 90 days or 10,000 records.
- Allow history to be disabled or cleared.
- Restore editor tabs after restart.
- Never reconnect or execute SQL automatically during recovery.

### 5.8 Row editing — post-MVP

- Enable editing only when rows have a primary key or usable unique key.
- Track pending inserts, updates, and deletes.
- Use parameterized SQL for generated changes.
- Preview changes before execution.
- Support commit and rollback.
- Confirm destructive operations.

## 6. Technology stack

| Layer | Technology |
|---|---|
| UI | C++23, Qt 6.8+ Widgets |
| Editor | QScintilla initially |
| Rust/C++ bridge | CXX |
| Top-level build | CMake + Ninja |
| Rust build | Cargo |
| CMake/Rust integration | Corrosion |
| Async runtime | Tokio |
| PostgreSQL | `tokio-postgres` |
| SQLite | `rusqlite` |
| MySQL/MariaDB | `mysql_async` after MVP |
| SQL Server | `tiberius` or ODBC after MVP |
| SQL parsing | Tree-sitter SQL grammars |
| App metadata | SQLite |
| Secrets | QtKeychain or native credential-store adapters |
| Rust logging | `tracing` |
| Rust tests | Built-in test framework, `proptest`, Criterion |
| C++ tests | Qt Test or Catch2 |

Qt must be dynamically linked when distributed under LGPL terms. The application GPL license also permits GPL-licensed QScintilla use, subject to final dependency license review.

## 7. Architecture

```text
Qt 6 Widgets UI (C++)
        |
        | commands, events, IDs, result pages
        v
CXX bridge
        |
        v
Rust application core
        |
        +-- connection manager
        +-- query engine
        +-- result-page cache
        +-- metadata service
        +-- SQL parser
        +-- export service
        +-- application storage
        |
        v
Database drivers
```

### 7.1 Qt responsibilities

- Windows, dialogs, docks, tabs, menus, and toolbars
- Database navigator
- SQL editor widget
- Result `QAbstractTableModel`
- Rendering and user input
- Delivering Rust events on the Qt event loop
- Converting database values into display strings

### 7.2 Rust responsibilities

- Database connections
- Query execution and cancellation
- Transactions
- Result pagination and cache limits
- Schema introspection
- SQL parsing
- Completion data
- History and settings persistence
- Export streaming
- Error normalization and log redaction

### 7.3 Dependency rules

- Rust core must not depend on Qt.
- Database drivers must not contain UI code.
- Qt types must not cross the CXX bridge.
- The UI must not access native database connections.
- The bridge must contain no business logic.

## 8. Rust/C++ bridge

Use CXX with coarse-grained operations.

Allowed bridge types:

- Integers and booleans
- UTF-8 strings
- Byte buffers
- Plain shared structures
- Numeric IDs
- Opaque Rust-owned objects

Do not expose:

- `QString`, `QVariant`, `QObject`, or `QModelIndex`
- Tokio futures
- Rust trait objects
- Database connections
- Borrowed buffers with unclear lifetime
- Rust panics or C++ exceptions

Main commands:

```text
connect(profile, secret)
disconnect(connection_id)
load_metadata(connection_id, parent_id)
execute(connection_id, sql, options)
fetch_page(query_id, page_index)
cancel(query_id)
commit(connection_id)
rollback(connection_id)
export(query_id, options)
```

Main events:

```text
connection_state_changed
metadata_ready
query_state_changed
result_schema_ready
result_page_ready
query_notice
query_failed
query_completed
export_progress
```

Use generational IDs for connections, queries, cursors, and pages. This prevents an old UI handle from accessing a newly allocated object with the same slot.

## 9. Concurrency model

```text
Qt UI thread
    |
    v
Rust command queue
    |
    v
Connection actor
    |
    v
Database connection
```

- Each connection is owned by one Rust connection actor.
- Commands for one connection execute in order.
- Different connections may run concurrently.
- Tokio handles network I/O and task scheduling.
- `rusqlite` work runs on dedicated blocking workers.
- Queues must have fixed limits.
- Completed work returns through an event queue.
- C++ drains events and posts updates to the Qt UI thread.
- No bridge call from the UI thread may wait for database or disk I/O.

## 10. Result pipeline

```text
Database cursor
    -> typed Rust rows
    -> bounded result page
    -> CXX page transfer/handle
    -> C++ page cache
    -> QAbstractTableModel
    -> visible QTableView cells
```

Rules:

- Transfer a complete page across FFI, not one cell at a time.
- Pause database fetching when the UI has enough pages buffered.
- Keep the visible page pinned in memory.
- Evict least-recently-used pages when the byte budget is exceeded.
- Use exact decimal representations. Do not convert decimals through floating point.
- Preserve database type name, precision, scale, timezone, and nullability.
- Format only visible or nearby cells.

## 11. Driver interface

Each driver implements:

```rust
trait DatabaseDriver {
    async fn connect(&self, options: ConnectionOptions)
        -> Result<Box<dyn Connection>>;

    fn capabilities(&self) -> DriverCapabilities;
}

trait Connection {
    async fn execute(&mut self, sql: &str, options: QueryOptions)
        -> Result<Box<dyn ResultCursor>>;

    async fn load_metadata(&mut self, parent: ObjectId)
        -> Result<Vec<SchemaObject>>;

    async fn commit(&mut self) -> Result<()>;
    async fn rollback(&mut self) -> Result<()>;
    async fn cancel(&self, query: QueryId) -> Result<()>;
}
```

Drivers declare capabilities such as:

- Schemas
- Transactions
- Native cancellation
- Server cursors
- Multiple result sets
- Explain plans
- Editable results
- Stored procedures
- Database-specific objects

The UI hides or disables unsupported features.

## 12. Application storage

Use a local SQLite database:

```text
schema_migrations
connection_profiles
profile_groups
editor_documents
query_history
metadata_cache
settings
recent_items
```

Passwords and private keys are not stored in this database. Store only a reference to the OS credential item.

## 13. Repository structure

```text
choscordb/
├── CMakeLists.txt
├── Cargo.toml
├── rust-toolchain.toml
├── crates/
│   ├── core/
│   ├── bridge/
│   ├── driver-api/
│   ├── driver-postgres/
│   ├── driver-sqlite/
│   ├── storage/
│   └── sql-language/
├── desktop/
│   ├── app/
│   ├── bridge/
│   ├── models/
│   ├── widgets/
│   └── resources/
├── tests/
└── docs/
```

## 14. Security requirements

- Store secrets in Keychain, Credential Manager, or Secret Service.
- Do not fall back to plaintext secret storage automatically.
- Verify TLS certificates and hostnames by default.
- Redact passwords, tokens, keys, connection strings, and row data from logs.
- Use parameterized SQL for generated data changes.
- Treat database names, messages, and returned values as untrusted input.
- Do not execute SQL files automatically.
- Do not silently repeat writes after a network failure.
- Require confirmation for `DROP`, `TRUNCATE`, and unsafe `UPDATE` or `DELETE` statements.
- Disable telemetry by default. MVP sends no usage analytics.

## 15. Performance targets

These are initial release targets, measured using release builds:

| Metric | Target |
|---|---:|
| Cold startup | ≤ 1.5 seconds |
| Warm startup | ≤ 0.8 seconds |
| Idle memory | ≤ 120 MiB |
| One connected session | ≤ 160 MiB |
| Editor input latency p95 | ≤ 30 ms |
| Normal UI task duration | ≤ 16 ms |
| Result page size | 1,000 rows default |
| Result cache | 64 MiB per result |
| Global result cache | 256 MiB default |
| Normal shutdown | ≤ 3 seconds |

The application must browse a one-million-row query source without storing all rows in memory.

## 16. Testing

### Rust

- Unit tests for state machines, caches, value conversion, migrations, and redaction
- Property tests for paging, quoting, CSV generation, and handle lifetimes
- Driver contract tests shared by every database driver
- Integration tests against real databases
- Criterion benchmarks for parsing, paging, and conversion

### C++/Qt

- Unit tests for table models, tree models, formatters, and bridge adapters
- Tests for row insertion, page eviction, selection copying, and error display
- UI tests for connect, execute, cancel, export, commit, and rollback

### Reliability

- Network loss
- Query timeout
- Cancellation races
- Database restart
- Corrupt local cache
- Disk-full export
- Invalid Unicode and large binary values
- Shutdown with active queries

## 17. Build and release

CI platforms:

- Windows x64
- macOS arm64 and x86_64
- Ubuntu x64

Every pull request runs:

```text
C++ build and tests
Rust format and Clippy
Rust unit tests
Bridge compile tests
License checks
```

Release candidates also run database integration tests, packaging tests, performance measurements, and installation smoke tests.

Packages:

- Windows installer
- Signed and notarized macOS application
- Linux AppImage or Flatpak
- Source archive
- Checksums, licenses, third-party notices, and SBOM

## 18. Roadmap

### Phase 0: Foundation

- Repository and CI
- Qt main window
- Rust engine
- CXX bridge
- Logging and local SQLite migrations

### Phase 1: SQLite vertical slice

- Connection profile
- Navigator
- SQL editor
- Execution and cancellation
- Paged grid
- CSV/JSON export

### Phase 2: PostgreSQL MVP

- TLS
- Schema introspection
- Native value types
- Transactions
- Server notices
- Native cancellation

### Phase 3: Public alpha

- History and recovery
- Credential-store integration
- Theme and accessibility pass
- Cross-platform installers
- Performance report

### Phase 4: Beta

- MySQL/MariaDB
- Row editing
- Better completion
- Explain-plan viewer

### Phase 5: 1.0

- Stability and security review
- Driver contract freeze
- Signed releases
- Contributor documentation

## 19. MVP acceptance criteria

- Application installs and launches on Windows, macOS, and Linux.
- PostgreSQL and SQLite connections work.
- Passwords never appear in the local database or logs.
- Navigator loads objects lazily.
- Editor executes the selection or current statement.
- Active queries can be cancelled.
- Result grid respects configured memory limits.
- A one-million-row result source can be browsed progressively.
- CSV and JSON exports stream to disk and support cancellation.
- Transactions support explicit commit and rollback.
- Workspace restores without reconnecting or executing SQL.
- Critical automated tests pass.
- Release contains licenses, notices, checksums, and build instructions.

## 20. Main risks

| Risk | Mitigation |
|---|---|
| Rust/C++ ownership bugs | Small CXX API, opaque handles, lifecycle tests, sanitizers |
| High memory usage | Page cursors, byte limits, backpressure, LRU cache |
| Database differences | Capability-based driver API |
| Complex dual build | Pinned toolchains, CMake presets, Corrosion |
| Linux packaging differences | Defined supported distributions and Flatpak/AppImage |
| Project scope | PostgreSQL and SQLite only for MVP |
| Editor licensing | GPL application or direct Scintilla integration |

## 21. Decisions still required

- Final product name
- QScintilla or direct Scintilla
- Exact PostgreSQL cursor strategy
- Exact Linux credential-store fallback behavior
- Minimum supported Linux distributions
- Final performance reference hardware
- GPL-3.0-only or GPL-3.0-or-later

## 22. References

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt item models](https://doc.qt.io/qt-6/qabstractitemmodel.html)
- [Qt threading](https://doc.qt.io/qt-6/threads-qobject.html)
- [CXX](https://cxx.rs/)
- [Corrosion](https://github.com/corrosion-rs/corrosion)
- [Rust FFI guidance](https://doc.rust-lang.org/nomicon/ffi.html)
- [QScintilla](https://riverbankcomputing.com/software/qscintilla/intro)
- [QtKeychain](https://github.com/frankosterfeld/qtkeychain)
