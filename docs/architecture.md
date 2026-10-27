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
| `export` | Streams result rows to a file. It never runs SQL. | `driver-api` |
| `pins` | Pin storage and pin identity. | `driver-api` |
| `diagnostics` | Local diagnostics with privacy limits. It never records SQL, row values or paths. | None |
| `update` | Signed update metadata and package verification. | None |
| `core` | Composes the services and schedules I/O on per-connection actors. | `credentials`, `diagnostics`, `driver-api`, `export`, `result-cache`, `result-store`, `storage` |
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

The engine closes the catalog session when the connection disconnects.
If the catalog session closes because of a connection error, the next read opens it again.

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
For example, Rust selects the SQL text that `Run current` or `Run all` sends to the database.
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
| `retry-policy-text` | Recovery sentences such as `Refresh to retry.` or `Refine the text or retry.` in string literals. `EngineAdapter::rowStatus` and `EngineAdapter::recoveryHint` supply them. A button label such as `Retry` has no full stop and is not a recovery sentence. |

The last four rules apply to presentation, display-model and bridge code.
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
