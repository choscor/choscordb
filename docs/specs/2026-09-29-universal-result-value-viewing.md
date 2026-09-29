# Universal result value viewing

Status: Confirmed for implementation; image preview and binary editing scope deferred

Date: 2026-09-29

Source context: User reported that a PostgreSQL customer table with binary data cannot be opened in ChoscorDB and recalled an error mentioning “binary”; the exact diagnostic, column type, and schema are unavailable. The user asked for DBeaver research and for table/view browsing to support any kinds of data. Decisions below were made in this brainstorm.

## Outcome

People can open and browse tables, views, and SQL results containing any server-readable value on the three current drivers: PostgreSQL, MySQL, and SQLite. A column that lacks a native decoder must not prevent otherwise readable rows and columns from appearing. Native values, including genuine binary values, retain their type and existing bounded detail, copy, and export behavior. A value that cannot be represented at all is identified at its cell rather than silently substituted.

“Any kinds of data” means values the server can return within ChoscorDB's established resource limits. It does not promise native editing, rich rendering, or a lossless round trip through an unfamiliar type's text representation.

## Current state and diagnosis

- Object Data in `desktop/app/object_data_workspace.cpp` uses the `QueryWorkspace` result lifecycle, shared with SQL Results. `desktop/models/result_table_model.cpp` can display typed `QByteArray` values as abbreviated hex and deferred large values as a load-on-demand placeholder. `desktop/widgets/value_detail_dialog/` reads bounded chunks. Existing SQLite tests cover BLOB tables and large BLOB details, so a BLOB column alone is not enough to establish the reported cause.
- PostgreSQL object reads use `SELECT *` and decode rows through `crates/driver-postgres/src/worker.rs`. The codec in `crates/postgres-values/src/lib.rs` handles `bytea` (OID 17) but returns `Unsupported PostgreSQL binary type` for other OIDs. Its README identifies arrays, domains, composites, extension types, and interval as examples. One such non-NULL value can fail a page, whether it comes from Object Data or SQL Results. This is the leading hypothesis, **not a confirmed diagnosis of the customer's table**. A fresh implementation session must reproduce the failure with a disposable PostgreSQL fixture and preserve any new diagnostic evidence.
- MySQL currently maps wire bytes to binary or UTF-8 based partly on column charset in `crates/driver-mysql/src/lib.rs`; SQLite maps its five storage classes in `crates/driver-sqlite/src/worker.rs`. Their current behavior and edge cases need verification under the same observable contract.
- DBeaver's [Data Editor](https://dbeaver.com/docs/dbeaver/Data-Editor/) shows both table/view Data and query Results. Its [value viewer](https://dbeaver.com/docs/team-edition/web/Value-Panel/) separates complex and binary value inspection from the grid and offers text, hex, and Base64 BLOB views; [Data Editor preferences](https://github.com/dbeaver/dbeaver/wiki/Data-Editor-preferences) include binary representation and LOB memory controls. This is a product reference, not a requirement to clone its UI. PostgreSQL's [`pg_type` catalog](https://www.postgresql.org/docs/current/catalog-pg-type.html) records type-specific text output functions; unknown binary payloads must not be guessed to be UTF-8.

## Requirements and decisions

1. Cover Object Data for base tables and views and SQL Results on PostgreSQL, MySQL, and SQLite. The PostgreSQL unsupported-type reproduction is the required regression case. Keep one coherent result-value contract across both grids.
2. Preserve native decoding and semantics for supported scalar, JSON, text, and binary values. In particular, `bytea`/BLOB values remain byte-accurate, visibly distinct from text and NULL, and compatible with existing bounded detail viewing and complete copy/export paths.
3. For a PostgreSQL value without a native decoder, obtain PostgreSQL's own **text representation** through a supported server/protocol path, retain its real database type in metadata, and show it as a read-only fallback. Do not decode arbitrary binary protocol bytes as UTF-8 or label the fallback as a native string. The same principle applies to any other current driver when it exposes a readable but unfamiliar type. Keep NULL distinct from empty text or empty binary.
4. A fallback value uses a bounded grid preview and a path to its full value under the existing large-value limits. Copy and Export must use the complete server text representation, never the shortened preview. Existing row JSON and result transformation actions must either preserve the fallback honestly with type information or fail clearly; they must not imply native typing or truncate silently.
5. If a particular value cannot be represented even by the fallback, keep other cells and rows visible and mark that cell unavailable with a concise reason and the database type. It must be distinguishable from NULL, empty, and a literal error-looking value. Copying a selection that includes it fails clearly. Exporting a range that includes it fails clearly and obeys the existing atomic-destination behavior. No error marker may be emitted as data.
6. Genuine query, permission, transport, cancellation, stale-object, and resource-limit errors remain operation failures with actionable diagnostics. Do not turn these into cell markers or silently skip data. A failed fallback acquisition must not cause an unsafe query retry, lose result ordering, or invalidate unrelated active results.
7. Fallback and unavailable cells are read-only. Existing verified editability, key checks, Apply/Discard, filters, sorting, FK navigation, paging, and exports for supported values must remain intact. Type-dependent actions that cannot be performed safely on a fallback or unavailable value should be disabled or fail with a specific reason; they must not coerce display text into a database predicate or write.
8. Preserve bounded memory, page size, deferred-value storage, cancellation, and stale-handle behavior. Large unfamiliar values must be readable in chunks or fail with an explicit resource error; the result grid must not allocate the full value just to produce a preview.

## User-visible flows and failure behavior

- Open the customer-style PostgreSQL table or a view with ordinary columns, `bytea`, and at least one unsupported-by-current-codec type. The grid opens, names and types are visible, ordinary values render normally, `bytea` retains its hex preview/detail behavior, and the unfamiliar value appears with a read-only text fallback.
- Run a SQL query returning the same mix. The result behaves consistently with Object Data. Paging forward and backward does not change values or rerun the user's SQL.
- Open a large binary or fallback value in the existing detail path. The preview is bounded, the full value remains available under existing limits, and NULL and empty values remain visibly different.
- If one value cannot be converted to either native or server text form, the affected cell shows an unavailable state and reason while other cells remain accessible. A requested copy/export containing that cell explains why it cannot complete. A genuine query or resource error reports failure instead of showing a partly successful result as complete.

## Acceptance criteria and public test seams

| ID | Observable outcome | Public test seam |
| --- | --- | --- |
| AC1 | A PostgreSQL table and view containing `bytea` plus array, domain, enum, composite, interval, and representative extension or unfamiliar type values open without the old unsupported-binary failure when the server can provide text output. NULLs and empty values remain distinct. | Disposable PostgreSQL driver `open_object`/`fetch_page` integration fixture and native Object Data grid/status inspection. Include a type with an unfamiliar OID; do not only test `bytea`. |
| AC2 | Equivalent query expressions appear in SQL Results, with native values still typed and unfamiliar values visibly labeled read-only. Result rows remain stable through paging and do not re-execute the query. | Public query execution/fetch API and native SQL Results grid using a side-effect or execution-count fixture where practical. |
| AC3 | MySQL and SQLite table/view and SQL result paths continue to show their supported binary, text, JSON/temporal/numeric, NULL, and mixed-type values; any newly found readable unfamiliar type uses the same fallback contract. | Driver-backed result API fixtures for both drivers and focused native grid checks. Do not require an invented unsupported type where the driver has no such type. |
| AC4 | Small and deferred binary/fallback values show bounded previews and full, accurate details; copy/export returns full bytes or server text with no preview truncation. | Native grid/detail and clipboard interactions, public export API with exact output comparison and large-value fixtures. |
| AC5 | A per-value conversion failure leaves the rest of the grid usable with a distinct unavailable cell and reason. Copy/export including it fails without outputting a marker; export preserves its previous destination. | Inject a failing value conversion at the public driver/adapter result seam; observe grid roles/visible status, clipboard result, and export destination. |
| AC6 | Query, permission, cancellation, stale-object, and resource-limit errors still fail visibly; old pages are not mislabeled as current and other tabs/results are unaffected. | Public driver/bridge error and cancellation seams plus native Object Data and SQL Results status/tab observations. |
| AC7 | Fallback/unavailable cells cannot be edited or used through unsafe typed actions; supported cells retain existing edit, filter, sort, FK, copy, and export behavior. | Native grid interaction tests and existing public edit/result-view API fixtures. |

## Technical constraints and affected areas

Likely areas are `crates/postgres-values/`, `crates/driver-postgres/`, shared `crates/driver-api/` value/schema contracts, `crates/core/` result storage/view/export, CXX bridge DTOs, `desktop/models/result_table_model.*`, and the shared `QueryWorkspace` paths in `desktop/app/`. MySQL/SQLite changes should follow actual failing cases rather than speculative rewrites. Choose a fallback acquisition path that works for arbitrary SQL results as well as object `SELECT *`, respects active transactions and server-side cursor semantics, and does not re-execute user SQL. Preserve source-column identity and database type independently from fallback display text.

Follow the root `AGENTS.md` for desktop changes; read `desktop/design_system/AGENTS.md` if shared presentation is changed. Use semantic roles and shared styling. Run `python3 scripts/ci/ui_consistency.py` (and inspect `--json`), `python3 scripts/ci/ui_policy.py`, and `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present. Add a design-system Light/Dark gallery specimen and matching test if a design-system component changes.

No schema migration, saved-workspace migration, or rollout flag is required. Keep existing output format contracts compatible for supported values. A new representation for fallback/unavailable values must be versioned or safely decoded wherever result pages are persisted, replayed, copied, or exported.

## Risks, assumptions, and deferred decisions

- The precise customer error and schema were not available. Treat the unsupported PostgreSQL binary type as a testable hypothesis, not a proven production root cause. If the reproduction shows a different failure, diagnose it before choosing the fix while preserving the agreed user-visible contract.
- Some PostgreSQL types may have no useful or permitted server text representation, and conversion can fail. The explicit unavailable-cell behavior applies only to value representation failures; operation and resource failures remain failures.
- Implementation finding: PostgreSQL reports a text-output function error as a statement error before delivering the affected `DataRow`. No cell payload or position is available to mark, so this is an operation failure under requirement 6. An unavailable cell applies when a driver can isolate a conversion failure after row delivery, such as invalid text encoding in a delivered fallback field; the driver must not retry the user's SQL to locate the cell.
- Native structural viewers for arrays/composites, image previews, binary editing, and editable fallback values are outside this viewing-focused spec by **deferred scope assumption**, not by explicit user rejection. The unanswered scope question may be revisited before implementation if those features are required.
- Do not infer that PostgreSQL text output is round-trip safe for edits, filters, or export to another database. Preserve it as a faithful readable representation with type context.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and applicable repository instructions, reproduce the PostgreSQL failure with a disposable fixture, then invoke `$implement` with this spec path. Use the acceptance criteria and public seams above for TDD, and re-evaluate the hypothesis against actual diagnostic evidence.

```text
Use $implement with docs/specs/2026-09-29-universal-result-value-viewing.md.
```
