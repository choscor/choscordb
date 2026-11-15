# Architecture

ChoscorDB has two parts: a Rust engine and a desktop frontend in Qt Widgets.
The Rust engine owns behavior, drivers and stored data.
The desktop frontend owns rendering and input.
A typed CXX bridge connects the two parts.
This page describes the crates, the ownership rules and the gate that enforces them.

## Crate map

The Cargo workspace contains all crates in `crates/`.
Each crate name starts with `choscordb-`.

| Crate | Responsibility | Workspace dependencies |
| --- | --- | --- |
| `driver-api` | Database-independent contracts for drivers and services, for example object-kind traits and driver workflows. | None |
| `driver-sqlite` | SQLite driver. All SQLite I/O runs on one dedicated worker. | `driver-api` |
| `driver-postgres` | PostgreSQL driver with server portals and bounded pages. | `driver-api`, `postgres-values`, `sql-language` |
| `driver-mysql` | MySQL driver with disk-backed results and bounded pages. | `driver-api`, `sql-language` |
| `postgres-values` | Decodes PostgreSQL binary values. | `driver-api` |
| `sql-language` | SQL editor services, for example the scanner. Positions are UTF-8 byte offsets. | None |
| `storage` | Local metadata and migrations. It never stores database credentials. | `driver-api` |
| `credentials` | Access to the credential store of the operating system. | `driver-api` |
| `result-store` | Temporary disk storage for result pages. | `driver-api` |
| `result-cache` | The application-wide page cache with memory budgets. | `driver-api` |
| `export` | Streams result rows to a file and formats copied rows. It never runs SQL. | `driver-api` |
| `pins` | Pin storage and pin identity. | `driver-api` |
| `diagnostics` | Local diagnostics with privacy limits. It never records SQL, row values or paths. | None |
| `update` | Signed update metadata and package verification. | None |
| `core` | Composes the services and schedules I/O on per-connection actors. | `credentials`, `diagnostics`, `driver-api`, `export`, `result-cache`, `result-store`, `sql-language`, `storage` |
| `bridge` | The CXX bridge and the composition root. | `core`, the drivers and most other crates |

`driver-api` has no Qt dependency and no concrete driver.
The drivers implement its contracts.
`core` depends on `driver-api` only, not on a concrete driver.
`bridge` creates the drivers and gives them to `core`.
A driver is an adapter that is compiled into ChoscorDB.
ChoscorDB does not load third-party binary plugins.

`bridge` translates typed commands and events between Rust and C++.
It builds as a static library that CMake links into the desktop frontend.
Read [Build and run](build.md) for the build integration.

### Event delivery

One engine task converts core events to bridge events, in the order that they arrive.
The UI thread takes only converted events, so a large result page does not stop it.
`EVENT_DRAIN_BUDGET` sets the maximum number of events in conversion or in the queue at one time.
A drain waits a short time for small events, and a large page goes to a later tick.
The bridge sends each cell with a typed `CellKind`. C++ does not parse cell text to find the type.

### Connection sessions

Each connection has one actor for the query session.
The query session runs SQL, edits, object data and DDL.

A PostgreSQL or MySQL connection also opens a catalog session at the first navigator read.
Navigator reads use the catalog session, so a long query does not stop the navigator.
The query session answers navigator reads in these conditions:

- The query session has an open transaction.
  Only the query session can show the changes that the transaction did not commit.
- The engine cannot read the transaction state. The engine shows the error.
- The catalog session cannot open.
  The engine shows the error one time, and the query session answers the reads.
  After `CATALOG_RETRY` (30 seconds), the next read tries to open the catalog session again.

The engine reads the transaction state after each statement, when its result is complete.
While a result streams, the engine uses the last state.

A profile with the `ReadOnly` safety level locks the query session after the connection opens.
The driver gives the lock statement through `Connection::read_only_session_sql`.
SQLite uses `PRAGMA query_only`. PostgreSQL and MySQL make each transaction read-only.
If a driver has no lock statement, the connection fails with an `Unsupported` error.
Before a statement runs, `sql_execution_gate` applies the safety level.
The gate can run the statement, ask for confirmation, or reject a statement that can change data.

The engine closes the catalog session when the connection disconnects.
If the catalog session closes because of a connection error, the next read opens it again.

### Result views and hidden columns

`apply_result_view` sorts a result by up to `MAX_SORT_KEYS` (4) columns. `toggle_sort` gives the next sort list for a header click.
A plain click replaces the sort. A Shift+click adds, reverses or removes one key.

The `storage` crate keeps the hidden result columns of each table in the `result_column_layouts` setting.
`result_columns_context` makes the key from the profile ID and the object ID. A connection without a profile has no key, and its hidden columns stay in memory.
The setting keeps the most recent `MAX_HIDDEN_COLUMN_CONTEXTS` (128) tables.
`toggle_hidden_column` refuses to hide the last visible column.

### Set value

`grid_set_value` in `driver-api` gives the result of a Set value choice for a column type and a driver.
NULL needs a nullable column. Empty needs a text column. Now() needs a date or time column and gives `CURRENT_DATE`, `CURRENT_TIME` or `CURRENT_TIMESTAMP`.
DEFAULT omits the column from an inserted row. For an existing row it writes `DEFAULT`, but SQLite cannot do this, and the engine returns a typed error.
The grid stages an expression as `GridEditRow::expressions`. `plan_grid_edits` writes it into the statement without a parameter, so the review shows it.
`GridTemporalKind::text` makes the ISO text of a picked date and time.
`read_cell_file` and `write_cell_file` in `core` read and write a binary cell. A cell file can have up to `MAX_GRID_BINARY_BYTES` (64 KiB).

### Query plan

The Explain toolbar button (Ctrl+Alt+E) shows the plan of the statement at the cursor in the Query plan sheet.
`explain_sql` in `core` adds the plan syntax of the driver: `EXPLAIN (FORMAT JSON)` for PostgreSQL, `EXPLAIN FORMAT=JSON` for MySQL and MariaDB, and `EXPLAIN QUERY PLAN` for SQLite.
These forms do not run the statement. `explain_sql` refuses an empty statement and a statement that is already an `EXPLAIN`.
`Engine::explain` sends one `Explain` command. The actor reads the plan rows, and `parse_explain` makes a list of `PlanNode` values.
Each node has a depth, an operation, an object, a cost, a row estimate and a detail.
A plan has at most `MAX_PLAN_NODES` (10,000) steps. A plan that the parser cannot read is an error.
The sheet shows the nodes as a tree in the order that Rust returns.

### Paste and bulk edit

The grid Paste command (Ctrl+V) stages the clipboard text in the selected cells.
`plan_grid_paste` in `core` reads the block. Tab text and comma rows become cells. Other text becomes one value for each line.
One value fills the full selection. A block tiles a selection that is an exact multiple of the block. Other blocks start at the top-left selected cell.
A block that does not fit, a ragged block or an empty clipboard returns an error. A paste changes at most `MAX_PASTE_CELLS` (100,000) cells.
The grid parses each value with the column type and stages all cells in one batch. If one cell fails, the grid stages no cell.
With more than one selected row, the Edit cell sheet sets a changed value in the same column of each selected row.

### History of grid changes

`grid_edit_history` in `driver-api` makes a history text when it plans grid changes.
The text starts with the original values of each updated or deleted row, as SQL comments.
Then it has each statement, with its parameter values as comments.
`Engine::apply_edit_batch` takes this text as an `EditHistory` with the profile ID.
The connection actor records one history entry after the batch. The entry has the result and the sum of the affected rows.
Object actions apply edit batches without a history entry.

### Object actions

`prepare_object_action` in `core` makes the SQL for Drop, Rename, Truncate and Clone from the trusted object ID.
It returns a list of statements. The desktop frontend applies the list as one edit batch, so a failure rolls back the full action where the database permits it.
Truncate is available only for tables. PostgreSQL gives `TRUNCATE TABLE` with optional `RESTART IDENTITY` and `CASCADE`. `truncate_traits` tells which options a driver supports.
MySQL and MariaDB give `TRUNCATE TABLE`. SQLite gives `DELETE FROM`.
Clone makes a new table in the same schema. PostgreSQL uses `LIKE ... INCLUDING ALL`, and MySQL and MariaDB use `CREATE TABLE ... LIKE`. An optional second statement copies the rows.
SQLite cannot clone a table with its keys and indexes, so the engine returns a typed error.
`object_script` gives the text for Copy script as: `DROP`, `TRUNCATE`, or `SELECT` with `SCRIPT_SELECT_ROWS` (100) rows. For `CREATE` it tells the frontend to load the object DDL.

### New object templates

`create_template` in `sql-language` gives the SQL to start a new table, view, function, procedure or schema.
A schema parent qualifies the new name. A connection or database parent does not.
SQLite supports only tables and views, so the other templates return a reason, and the menu item is disabled.
MySQL and MariaDB make a schema with `CREATE DATABASE`.

### Switch database

`Connection::list_databases` gives the databases that a connection can switch to, and the current one.
PostgreSQL lists the databases that accept connections, and not the templates. MySQL and MariaDB list the schemas.
SQLite lists the `.db`, `.sqlite` and `.sqlite3` files in the folder of the database file.
A list can have up to `MAX_DATABASE_CHOICES` (10,000) entries. A larger list is a `ResourceLimit` error.
`profile_with_database` in `bridge` sets the database name, or the SQLite path, of a copy of the profile.
The desktop frontend opens a new connection with that profile and moves the current SQL tab to it. The old connection stays open.

### Manage databases

`plan_database_action` in `core` makes the SQL to create, rename or drop a database.
It quotes the names for PostgreSQL, MySQL and MariaDB, and it validates the encoding and the collation.
It refuses to rename or drop the current database. A drop needs the typed database name.
MySQL and MariaDB cannot rename a database, and SQLite keeps each database in a file. The plan returns a reason for these cases.
`DatabaseActionKind::fields` tells the dialog which fields each action shows.
The dialog shows the SQL before Apply and sends it through `Engine::apply_ddl`.

### Value preview

The value detail dialog has a Preview action. The dialog loads the complete value in chunks, up to `VALUE_PREVIEW_MAX_BYTES` (16 MiB).
`value_preview` in `core` reads the bytes and the database type, and returns one preview kind.
JSON text gets a pretty form, a minified form and tree rows in document order. The tree stops at `MAX_JSON_PREVIEW_NODES` rows.
A binary value gets the image format from its first bytes. The desktop frontend decodes and shows the image.
A PDF document gets a summary with the version and the page count. ChoscorDB does not render PDF pages.
A geometry type gets WKT from WKB, EWKB or hexadecimal EWKB. A MySQL SRID prefix and an EWKB SRID become `SRID=n;`.

### Schema ER diagram

`load_object_graph` accepts a schema or a database identifier. Then the driver returns all tables and foreign keys of that schema.
A PostgreSQL database identifier gives the tables of all user schemas.
`SchemaGraphBuilder` in `driver-api` enforces `MAX_SCHEMA_GRAPH_TABLES` (500) and `MAX_SCHEMA_GRAPH_COLUMNS` (20,000).
It reports foreign keys to tables outside the diagram as a warning.
`schema_graph_layout` in `core` gives each table a grid column, a grid row and a relation group.
Each group of connected tables gets layers in the foreign-key direction. A referenced table is to the left of the table that refers to it.
The groups fill shelves that are about the square root of the table count wide. A table without relations has no group.
The bridge adds the cells to each graph table. The desktop frontend sizes the columns and the rows and colors each group with a theme token.
The navigator shows "Show ER diagram" for each kind with the `schema_diagram` trait. The diagram has search, zoom and fit.

### Triggers

Each driver lists the triggers of a table or a view as `ObjectKind::Trigger` children of the relation.
Each trigger has the Timing, Event and Definition properties. The Definition is a complete `CREATE TRIGGER` statement.
MySQL and MariaDB read the columns and the triggers in one paged query, thus the page offsets stay correct.
The Triggers pane of an object tab shows these children. The navigator does not show them.
`trigger_script` in `core` makes the SQL to create, edit or drop a trigger. Edit gives a `DROP TRIGGER` statement and the definition.
The desktop frontend opens the SQL in a new query tab. The user reads the SQL and then runs it.
The SQL scanner keeps a `BEGIN ... END` trigger body in one statement. It counts `BEGIN`, `CASE` and the MySQL `END IF`, `END LOOP`, `END WHILE` and `END REPEAT` blocks.

### Excel export

`ExportFormat::Xlsx` writes one worksheet with a header row. The `xlsx` module in `export` streams the sheet XML into a ZIP file.
The sheet part uses deflate and a data descriptor. Thus the writer keeps only one compressed buffer in memory.
Integers up to 2^53, finite real numbers and decimals with 15 or fewer significant digits become number cells.
Booleans become boolean cells. All other values become inline text cells.
The writer escapes XML characters and control characters. It also escapes text that looks like an Excel `_xHHHH_` escape.
The export fails when it gets to an Excel limit: 1,048,576 rows, 16,384 columns or 32,767 characters in one cell.
The export also fails when the file gets to 4 GiB. Then the destination file does not change.

### Table export

`plan_table_export` in `core` makes one target for each table or view: the `SELECT` that Rust builds, the destination file and the export format.
A folder destination uses a file name pattern with `{schema}` and `{table}`. Rust adds the extension and rejects two tables that write the same file.
Several tables go to one file only for the SQL format.
CSV options are part of `ExportFormat::Csv`: delimiter, quote, line break, NULL text and header row. `CsvOptions::validate` rejects options that make the file ambiguous.
`Engine::export_tables` sends one `ExportTables` command to the connection actor.
The actor closes the open SQL cursor, runs each `SELECT` and streams the rows through `choscordb_export::export`.
For the SQL format, the actor writes the `DROP` statement and the DDL from `Connection::object_ddl` before the rows.
The actor sends `TableExportProgress` after each table, then `TableExportFinished` or `TableExportFailed` with the name of the failed table.
A failed or cancelled file is not written. Files of earlier tables stay.

### Table import

The navigator "Import…" item on a table and "Import into new table…" on a connection, database or schema open the import dialog.
`read_import_file` in `core` reads a CSV, JSON array or JSON Lines file in UTF-8 or Latin-1. The preview has the first 100 rows.
CSV options are the delimiter, the quote, the NULL text and the header row. A JSON row is an object. The keys become the columns in the order of the file.
Limits: 64 MiB, 100,000 rows and 1,000 columns for each file.
`table_import_default_mapping` in the bridge maps the table columns to the file columns with the same name.
`plan_table_import` makes one `INSERT` for each row. `row_import_sql` in `driver-api` gives the conflict policy for each driver:

| Policy | SQLite and PostgreSQL | MySQL and MariaDB |
| --- | --- | --- |
| Stop | `INSERT` | `INSERT` |
| Skip | `ON CONFLICT DO NOTHING` | `ON DUPLICATE KEY UPDATE` of the first column to itself |
| Update | `ON CONFLICT (key) DO UPDATE` | `ON DUPLICATE KEY UPDATE` |
| Replace | `REPLACE INTO` (SQLite only) | `REPLACE INTO` |

`Update` needs all primary key columns in the mapping. PostgreSQL does not show `Replace`.
Before the import, the dialog shows the SQL for each row from `table_import_review`. It uses the same plan as the import.
`Engine::import_table` sends one `ImportTable` command to the connection actor.
The actor reads the file on a blocking thread, inspects the table with `Connection::inspect_edit_target` and applies all rows with one `apply_edit_batch` call in one transaction.
Each driver puts the index of the failed statement in `DriverError::failed_statement`. The bridge adds `Row N:` to the error message.
A new table gets `TEXT` columns. The actor creates the table first and drops it again when the rows fail.
Cancel stops the import before the write. During the write, it asks the driver to cancel the statement.

### Run a SQL file

File → "Run SQL file…" (Ctrl+Alt+O) runs each statement of a SQL file on the selected connection.
The run mode is "Stop at the first error", "Continue after errors" or "Run in one transaction and roll back on error".
The connection safety level applies to the whole file. A read-only session rejects the run. "Confirm writes" and "Confirm all" ask one time.
`Engine::run_sql_file` sends one `RunSqlFile` command to the connection actor.
The actor reads the file in parts of 1 MiB. `SqlFileSplitter` gives the complete statements of each part with their line numbers.
`split_script` in `sql-language` splits the text. For MySQL and MariaDB, it obeys `DELIMITER` lines from `mysqldump` files.
The splitter rejects text that is not UTF-8 and a statement that is larger than 64 MiB.
Each error message starts with `Line N:`. "Continue after errors" keeps the first 100 errors, and the dialog shows them.
A run with failed statements does not show a success message.
Cancel asks the driver to cancel the current statement. A transaction run rolls back when it fails or stops.
The splitter does not run `psql` commands. The `COPY … FROM stdin` data of a `pg_dump` file fails. Use `pg_dump --inserts` for these files.

### Backup and restore

File → "Back up database…" and "Restore database…" open `BackupDialog` for the selected connection.
`BackupRequest::new` in `core` checks the file, the tool folder and the option codes. It rejects "Schema only" with "Data only".
`backup_options` gives the options and their defaults for each driver. `backup_unavailable` gives the reason when a driver cannot do the operation.
The connection actor keeps a `BackupTarget` that it makes from the connection options before it connects.
`plan_backup` finds the client tool in the tool folder or on `PATH`. ChoscorDB does not include the client tools.
PostgreSQL uses `pg_dump` and `pg_restore`. MySQL uses `mysqldump` and `mysql`. MariaDB tries `mariadb-dump` and `mariadb` first.
The password goes to the tool in `PGPASSWORD` or `MYSQL_PWD`. The first output line shows the command without the password.
`pg_restore` accepts only custom and tar archives. Run a plain SQL backup with "Run SQL file".
The `mysql` tool reads the backup file from its standard input.
Backup and restore cannot use an SSH tunnel, a SOCKS proxy or a client certificate. The tools connect to the server directly.
SQLite backs up with `VACUUM INTO` into a new file. SQLite restore is not available because the database file is open.
The dialog shows the tool output live. The engine sends the first 2,000 lines and cuts each line at 4 KiB.
Cancel stops the tool. A tool that stops with an error code gives a failure with that code.
The safety level applies to a restore like a SQL file run. A backup only reads data.

### Saved queries

The Saved queries panel shows the `.sql` files in the `sql` folder beside the application database.
`saved_sql_tree` in `core` reads the folder without symlinks. It gives folders first, then files, in name order or newest first.
The panel shows the rows in this order. "Sort by name" and "Sort by date modified" are in the context menu.
"New folder…" calls `create_saved_sql_folder`. A folder name has no slash and does not start with a dot.
A first line `-- keyword: name` gives the file a keyword. "Set keyword…" adds, changes or removes this line with `set_saved_sql_keyword`.
A keyword has 1 to 64 letters, digits or underscores. A keyword file is limited to 64 KiB, and all snippets to 8 MiB.
The listing gives each snippet to SQL completion with the kind `snippet`. Its insert text is the file text after the keyword line.
The panel shows the keyword errors of the listing.

### Structure editor

The navigator "Edit structure…" item on a table and "Design new table…" on a connection, database or schema open the structure editor.
`structure_edit_unavailable` in `core` permits only plain tables.
`StructureChangeKind` in `driver-api` gives the changes and the fields of each change.
The changes add, change or drop a column, add or drop an index, set the primary key, add a foreign key or drop a key.
`StructureChangeKind::unavailable` gives the reason when a driver cannot do a change. SQLite needs a table rebuild for a primary key, a foreign key or a dropped key. A new table permits only the changes that create columns, indexes and keys.
The dialog makes one form for each change from the field descriptors. It does not know the SQL rules.
`plan_structure` makes the SQL for each driver. A new table gets one `CREATE TABLE` with its columns and constraints, then the comments and the indexes.
The dialog shows the SQL before Apply. The SQL comes from `structure_plan` in the bridge, and `apply_structure` makes the same plan again in Rust.
`Engine::apply_ddl` sends one `ApplyDdl` command. The actor runs the statements in order and stops at the first failure.
The error message starts with `Statement N:`. MySQL and MariaDB commit each DDL statement, so the statements before the failure stay applied.
After Apply, the navigator loads the children of the table or the parent again.

### Result tabs for each statement

`Run all` runs each statement as a separate query.
Rust (`statement_plan` in `crates/sql-language`) splits the selection or the document into statements and gives each statement its label.
The label is `Query N`, or the text of a `-- name:` comment line above the statement.
A plan has a maximum of 64 statements. If the plan is empty or too large, Rust returns the error that the frontend shows.
The query workspace keeps one result tab for each finished statement.
When a statement fails and "Stop on first error" is on, the run stops at that statement.
A pinned tab stays when the next run starts. The next run releases the other tabs.
Limitation: a connection keeps only one open cursor.
When the next statement starts, the earlier statement keeps only the pages that it loaded before.

### Metadata cache

The `storage` crate keeps the navigator children of saved connection profiles in the `metadata_cache` table.
The key is the profile ID and the parent object ID.
The value has the ID, the name, the qualified name, the kind and the child flag of each child.
The cache does not keep object properties, column details or DDL.

- The engine writes a parent when a complete live first page loads.
- The engine does not write children that the query session reads in a transaction or in an unknown state.
- A cache entry expires after 7 days. The engine ignores an expired entry and deletes it at the next write.
- When you delete a profile, the engine deletes its cache entries.
- When a saved profile gets a different connection target, the engine deletes its cache entries.

When the navigator expands a node, the engine first sends the saved children with the `cached` flag.
Then it loads the live children and sends them without the flag.
The live children replace the saved children.
The navigator keeps the saved rows that the live children still contain, so their expansion stays.
A manual refresh and an object inspection read only the live children.
A failed cache read or write shows as an error.

The local metadata database contains schema and object names of saved profiles.
It does not contain credentials or row values.

### PostgreSQL protocol delta

The PostgreSQL driver uses a copy of `tokio-postgres` with a small local delta.
The delta sends some protocol messages together, to save network round trips.
`crates/driver-postgres/third_party/tokio-postgres/CHOSCORDB_PATCH.md` gives the full delta.

## Rust and C++ ownership

### The desktop frontend owns presentation

The C++ code in `desktop/` owns only these items:

- Widgets, display models and user interaction.
- Transient view state.
- The Qt event loop and native UI integration.
- Conversion of values at the CXX bridge.

Visual tokens, for example colors, sizes, radii and typography, belong to the desktop frontend.
They never cross the bridge.
Read [Design system](design-system.md) for the tokens.

### The Rust engine owns behavior

The Rust engine owns all application behavior that is not presentation:

- The engine, the drivers, SQL rules and result rules.
- Domain validation, stored state, application data and document I/O.
- Network operations, diagnostics, exports and update policy.
- Defaults, limits, budgets, intervals and user-facing policy messages.
- Persisted encodings, for example IDs, context strings, file names and storage locations.

The bridge gives these values to C++ as typed DTOs.
For example, Rust selects the SQL text that `Run` or `Run all` sends to the database.
C++ sends the run scope as the typed `ExecutionScope` enum and does not find statements itself.
The bridge always runs SQL editor text with auto-commit.
Do not repeat a Rust rule in C++ or in the bridge.
C++ shows the errors from Rust.
C++ does not check a rule again before or after it calls Rust.
UI input hints do not replace the Rust validation.

### Rules for frontend code

- Qt can get a path from a native dialog.
  Send that path to Rust for validation and execution.
- Keep Rust engine work off the UI thread.
- Read Rust policy through the cached `EngineAdapter` accessors.
- A model `data()` function or a delegate can need a trait.
  Then store the trait on the node or page when you build it.
- Do not branch on object-kind or driver-name literals in screens, models or the bridge.
- Use `EngineAdapter::objectKindTraits` or a Rust driver DTO, for example `driverWorkflow`, `profileDriverForm` or `transactionGuard`.
- If new behavior needs a new difference between kinds or drivers, add a Rust trait.
- Keep the kind-to-icon and kind-to-label maps in `desktop/app/object_kind_icon.h`.
- Do not give a display row the kind of a different object. Add a display kind to `object_kind_icon.h`.
- Filter lists and trees with `TextFilter` from `desktop/bridge/text_filter.h`.
- Rank and limit search results in Rust, as `QuickSearchNeedle::plan` does. C++ shows the rows in the Rust order.
- Never compare or search `tr()` text to make a decision.

Old C++ code that does the work of the Rust engine is migration work.
It is not an example for new code.
When you change such code, move the non-frontend behavior to Rust.
Test policy in Rust, and keep C++ tests on UI and bridge behavior.

## State machines

Rust owns the state machines of the screens.
Each machine is in `crates/core/src` and has one `step` function and one `present` function.
`step` returns the next state or a typed `TransitionError`.
`present` returns a `Presentation`: the tone, the busy flag, the body, the result pane, the retry action, the message and the toast.

| Machine | Rust module | Spec | Screens |
| --- | --- | --- | --- |
| Query | `query_machine.rs` | `specs/query/Query.tla` | SQL results, object data |
| Connection | `connection_machine.rs` | `specs/connection/Connection.tla` | The connection actor and its catalog session |
| Panel | `panel_machine.rs` | `specs/panel/Panel.tla` | Saved SQL, history, pins, objects, object details, diagram, quick search |
| Grid edit | `grid_edit_machine.rs` | `specs/grid_edit/GridEdit.tla` | The plan and the apply steps of grid changes |
| Export | `export_machine.rs` | `specs/export/Export.tla` | Export dialog |
| Updater | `updater_machine.rs` | `specs/updater/Updater.tla` | The `Updates` menu on Windows and Linux |
| Profile test | `profile_test_machine.rs` | `specs/profile_test/ProfileTest.tla` | Connection test, SSH host key trust and the load, save, delete and connect status in the profile dialog |

The bridge sends each state and each presentation as a typed DTO.
`EngineAdapter` gives the `*Transition` and `*Presentation` accessors.
C++ keeps the typed state, asks Rust for the next state and shows the presentation.
C++ does not compare state names and does not choose a tone or a failure message.

Some screens know only facts, for example "a load failed" or "the list has rows".
These screens send a `PanelFacts` value to `EngineAdapter::panelObservation`.
`panel_machine::settle` chooses the state from the facts in one fixed order.
The order is failed, work in progress, disconnected, not loaded, rows, filter and then empty.
The quick search status comes from `quick_search::status` for the same reason.
`query_machine::result_state` chooses between a result with rows and an empty result.
The connection actor and `EngineAdapter::queryResultState` use the same function.
The connection actor keeps the last state of each query.
`query_machine::failure_path` builds the failure states from that state, so each emitted state is a legal step.

Some rules apply to all machines:

- A busy state has the neutral tone.
- A failure has the error tone and a message that ends with a recovery hint.
- An empty result is not a failure.
- A failed panel shows a placeholder and a toast.
- A filter does not hide a failure.

### Verification

Each spec has a `transitions.json` table in its directory.
The table gives the states, the events, the transitions and the presentation.
The Rust tests in `crates/core/tests/state_machines` compare `step` and `present` with the table.
`scripts/ci/tla.py check` makes TLC compare the same table with the spec and check the invariants and the liveness properties.
If the Rust code, the table or the spec changes alone, one of these checks fails.

`crates/core/tests/connection_traces.rs` runs the connection actor with a fake driver.
It writes each run to `target/tla-traces/*.ndjson`.
`scripts/ci/tla.py traces` makes TLC accept each run as a behavior of `Connection.tla`.

## The ownership gate

`scripts/ci/cpp_ownership.py` enforces the ownership rules.
`quality.py fast` and `quality.py full` run it as the `cpp-ownership` stage.
Read [Continuous integration and quality gates](ci.md) for the other gates.

### Run the gate

```sh
python3 scripts/ci/quality.py cpp-ownership
python3 scripts/ci/cpp_ownership.py --json > ownership.json
python3 -m unittest discover -s scripts/ci -p 'test_cpp_ownership.py'
```

The exit codes are these:

- `0`: The gate found no open findings.
- `1`: The gate found findings, inventory errors or exception errors.
- `2`: The audit could not run.

The `--json` output has the same exit code.
Do not hide the exit code with a shell pipeline.

### What the gate scans

The gate finds native files in the repository with Git.
It scans tracked files and untracked files that Git does not ignore.
It also scans ignored native files in `desktop/` and `tests/`, so an ignore pattern cannot hide them.
It scans C, C++, Objective-C, headers, inline fragments and C++ modules.
It scans all platform `#if` branches without a compiler.
It also scans CMake files for database, network and storage library dependencies.

Each file has a role from the `ROLES` map:

| Location | Role |
| --- | --- |
| `desktop/app/`, `desktop/widgets/` | `presentation` |
| `desktop/models/` | `display-model` |
| `desktop/design_system/` | `design-system` |
| `desktop/bridge/` | `bridge` |
| `desktop/tools/` | `development-tool` |
| `tests/` | `test` |
| `vendor/` | `third-party` |

A new first-party location fails until you add it to `ROLES`.
The gate scans test files, but their findings do not block.
Developer tools and the bridge get the production rules.
Source symlinks fail.

The JSON report contains sorted paths, roles, line counts, SHA-256 digests, findings, exceptions and a summary.
It has a schema version and no timestamps or machine paths.
The digests normalize CRLF to LF, so LF and CRLF checkouts give the same report.

### Rules

| Rule | What it finds |
| --- | --- |
| `storage` | Qt settings and file access, C++ file streams and filesystem APIs. |
| `network` | Qt network classes, sockets, curl and some native network APIs. |
| `database` | Qt SQL and the SQLite, PostgreSQL, MySQL and ODBC client APIs. |
| `process`, `native-io` | Process execution and some native file and socket APIs. |
| `backend-include`, `backend-link` | Database, network and storage headers and CMake dependencies. |
| `excluded-dependency` | Production includes of test or vendor sources. |
| `filesystem-query` | Filesystem checks and directory changes. |
| `serialization` | JSON, XML, binary serialization and cryptographic APIs. |
| `domain-parsing` | Regular expressions and version parsers. |
| `sql-text` | SQL statement literals. |
| `translated-control-flow` | Comparisons or searches on `tr()` text. |
| `limit-revalidation` | Comparisons with the fields of a Rust limits DTO. |
| `rust-limit-literal` | Literals equal to a Rust `MAX_`, `MIN_` or `DEFAULT_` constant of 4096 or more. |
| `text-matching` | Case-insensitive searches, lowercase searches and `QSortFilterProxyModel` text filters. |
| `object-kind-literal` | Branches on object-kind words such as `"table"`. |
| `driver-literal` | Branches on driver names such as `"mysql"`, in any letter case. |
| `screen-state-literal` | String screen states, for example `property("state") == "failed"`, `setProperty("state", "loading")` or `setExecutionState(QStringLiteral("queued"))`. Use the typed states of the Rust state machines. |
| `retry-policy-text` | Recovery sentences such as `Refresh to retry.` or `Refine the text or retry.` in string literals. `EngineAdapter::rowStatus` and `EngineAdapter::recoveryHint` supply them. A button label such as `Retry` has no full stop and is not a recovery sentence. |

The last five rules apply to presentation, display-model and bridge code.
They find equality, list membership, `compare` and prefix or suffix searches.
They also find wrapped literals, for example `QStringLiteral("table")` or `u"table"_s`.
Design-system and developer-tool code can map kinds and drivers to appearance.
Bridge event dispatch can compare `eventKind == "schema"`, because that event name is also an object kind.

`rust-limit-literal` scans only values of 4096 or more.
Small values collide with usual geometry and counts.
Smaller limits must also come from a Rust DTO or a cached accessor, and review enforces this.
A UI-only number that is equal to a Rust limit needs a `// ui-budget: <reason>` marker.
Put the marker on its line or on the line above.

### Finding levels

An `error` finding is a capability that usually belongs to Rust.
A `review` finding is ambiguous code, for example SQL syntax colors or example SQL.
Both levels block the gate.
A review finding is not always a violation.
The gate masks comments and string contents for the capability rules.
It scans SQL literals separately.

### Resolve a finding

1. Move the behavior to Rust.
2. Keep only UI orchestration and value conversion in C++.
3. Remove the includes that the file does not use.

If the use is real presentation, review the full file for that capability.
Then add an entry to `scripts/ci/cpp_ownership_exceptions.json` with these fields:

- `path`: One exact repository-relative file. Do not use globs.
- `rule`: One rule that the file triggers now.
- `sha256`: The SHA-256 of the file, with CRLF changed to LF.
- `reason`: The presentation contract, and why the capability belongs in C++.

The gate fails for changed content, deleted files, findings that no longer occur, duplicate entries and empty reasons.
An exception for one rule does not suppress a different rule.
There is no command that accepts all findings.
Do not update a digest before you review the changed capability.
A digest detects a change only, not an approval.

### Limits of the gate

The gate is a lexical scanner.
It is not a compiler, a call-graph analyzer or a proof of correct architecture.
It cannot find the purpose of arithmetic, loops, validation or data transformations.
It does not prove that Rust validates UI input again, or that a bridge call runs off the UI thread.

These forms can avoid the rules:

- Macro token pasting, encoded strings and indirect calls.
- APIs that the rules do not list, and third-party wrappers.
- Dynamic CMake expressions and generated sources.
- Kinds or drivers in a named container or a named constant, for example `kind == kTable`.

Review rejects these forms the same as literals.
Common names such as `open` and `connect` are not banned, because Qt UI code also uses them.

### Keep the gate current

Review changes to the discovery, rules, exceptions, `.gitignore`, build configuration and generated-source inputs as architecture changes.
A passing scan does not replace a review of changed C++ functions.
Ask these questions about each changed function:

- Does it choose SQL?
- Does it validate domain data?
- Does it store or interpret application data?
- Does it make network requests?
- Does it decide update policy?

If the answer is yes, move that behavior to Rust.
If review finds a new kind of drift, add a failing fixture to `scripts/ci/test_cpp_ownership.py`.
Then add a rule for it in the same change, and test a valid UI counterpart.
