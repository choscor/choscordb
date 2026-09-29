# Colorful JSON views for result cells, rows, and pages

Status: Confirmed for implementation
Date: 2026-09-29
Source: User request and brainstorm decisions in this session

## Outcome and current state

People inspecting SQL Results or Object Data can read and copy JSON with syntax coloring in the existing right sheet. They can inspect one cell, one row, or the currently loaded result page without changing data.

Both grids use `QueryWorkspace` and `ResultTableModel`. The shared context menu is in `desktop/app/query_workspace.cpp`. **View row as JSON** already uses `desktop/app/query_workspace_json.cpp`, a `design::RightSheet`, a read-only `QPlainTextEdit`, full deferred-value loading, and **Copy JSON**. Its serializer in `desktop/models/result_table_model.cpp` currently quotes JSON/JSONB text as a JSON string by design of [the earlier row-view spec](2026-09-27-json-row-right-sheet.md); this spec supersedes that choice for JSON/JSONB values. Result grids are paged (default 1,000 rows, configurable up to 10,000), and can have applied filters, sort order, and staged edits. **Export results** already handles the complete query separately.

## Requirements and decisions

1. Add readable, theme-aware JSON syntax coloring to the row sheet and use the same presentation for cell and table views. Differentiate object keys, string values, numbers, and `true`/`false`/`null`; keep punctuation readable. Use a monospace typography role, indentation, a selectable read-only text view, and contrast-safe semantic theme colors in Light and Dark themes. Recolor on theme changes. Coloring must not alter the underlying plain text or copied bytes. The existing shared right-sheet shell, loading/status area, and fixed **Copy JSON** footer remain the interaction pattern.
2. Add **View cell as JSON** to the shared grid context menu. Target the cell under the pointer, independent of selection. Offer it for non-NULL JSON/JSONB column cells and for non-JSON text cells whose complete content parses as JSON, including scalar JSON values. Do not offer it for unrelated numeric, boolean, or binary cells merely because their displayed text could parse. For a JSON/JSONB cell with malformed staged text, keep the action available so the sheet can explain the parse error; immediately available ordinary malformed text does not qualify. A deferred text cell may be offered provisionally, then fully loaded and parsed in the sheet; if parsing fails, show the error and disable Copy. Never decide eligibility from an abbreviated preview. The sheet title is **View cell as JSON** and shows the parsed document pretty printed. **Copy JSON** copies exactly the displayed document.
3. Keep **View row as JSON**, but embed valid JSON/JSONB cell content as structured JSON values instead of strings. Ordinary text remains a string even when its content looks like JSON; the cell action may still inspect that text separately. Preserve all other row conventions from the earlier spec: typed values, ordered collision-free column keys, staged edits/inserts/deletions, `$omitted` and `$binary` wrappers, exact integer handling, and full deferred values.
4. Add **View table as JSON** immediately below **View row as JSON** in the same context menu in both grids. The action opens the same right sheet with that title and a pretty printed JSON array of row objects, in the current grid order, for **only the currently loaded page**. Include all page rows independent of selection and apply the same row serialization rules, including current staged values. The page may reflect applied filters and sort order because those affect the loaded grid. Show a clear page-scope note, including that other pages are excluded. Keep the existing whole-result Export path separate.
5. The table action is available when the grid has a current, nonempty page, including a right click on blank grid space; row/cell actions still require their respective valid pointer targets. A stale or replaced result cannot open any JSON view. JSON sheet actions are read-only and must not alter selection, staged edits, paging, query state, or database contents.
6. Invalid JSON/JSONB text, invalid UTF-8, failed deferred fetches, unrepresentable typed values, size or memory limits, and result invalidation must yield a clear error with no partial JSON exposed as a complete document and **Copy JSON** disabled. A parsed ordinary text cell is eligible only when valid. Show loading progress for deferred content; closing cancels the view, and late events cannot populate another view. Use a bounded output and loading budget so a page of up to 10,000 rows remains responsive; fail clearly when it exceeds safe limits. Exact budget values are an implementation choice, but should be documented in user-facing errors and tests.

## User-visible flows and edge cases

- Right click a valid JSON/JSONB cell, or parseable text cell, and choose **View cell as JSON**. The sheet shows the complete JSON value, whether its root is an object, array, string, number, boolean, or `null`. A SQL NULL cell is not a JSON document and does not qualify. Malformed JSON/JSONB staged text opens an error state with Copy disabled.
- Right click a row and choose **View row as JSON**. JSON/JSONB values appear nested; plain text containing JSON stays quoted. Existing row behavior for duplicate column names, binary, omitted insert fields, and deferred values continues.
- Choose **View table as JSON** from a populated page, including from blank space. The sheet shows an array of every row on that page, including staged inserted rows and visibly staged deleted rows, in grid order. An empty or unavailable page disables the action.
- The sheet uses the existing X, Esc, and backdrop close paths, focus restoration, text selection, and Copy action. Loading or parse failure leaves no copyable partial result. Copy leaves the sheet open.

## Acceptance criteria and public test seams

| ID | Observable outcome | Public seam / evidence |
| --- | --- | --- |
| AC1 | Row, cell, and table JSON views have distinct readable syntax colors in Light and Dark themes; the displayed plain text and clipboard text match exactly. | Open each view through real grid actions under both themes; inspect the `QPlainTextEdit` document formats and clipboard, and visually check a Light/Dark specimen if the design-system component changes. |
| AC2 | Cell action follows the clicked cell, accepts JSON/JSONB or parseable text (including scalar JSON), excludes SQL NULL and unrelated typed cells, and shows complete pretty JSON. | Native `QTableView` context menu in SQL Results and Object Data with typed `ResultTableModel::setPage` fixtures; compare sheet text with a JSON parser. |
| AC3 | Malformed JSON/JSONB staged text shows an error and disables Copy; available malformed ordinary text does not qualify; deferred ordinary text is checked after loading. | Stage edits through the public model edit seam and use a deferred text fixture; invoke the grid menu and observe action state, sheet status, and Copy state. |
| AC4 | Row and page views embed JSON/JSONB values, retain ordinary text as strings, and preserve prior typed-row conventions and order. | Public model page/edit seam through the real actions; parse sheet JSON and compare nested values, row order, duplicate keys, staged rows, and binary/omitted markers. |
| AC5 | Table action sits immediately below row action, includes the loaded page regardless of selection or click position, excludes other pages, and communicates page scope. | Real context menu and paging controls in both grids with a multi-page fixture; inspect action order, title/note, array rows, and enabled states. |
| AC6 | Deferred content loads fully; failure, cancellation, invalidation, or a safe budget limit cannot produce copyable partial JSON or leaked transfers. | Public `EngineAdapter` chunk/event seam with delayed, oversized, invalid, and multi-chunk fixtures; observe status, sheet text, Copy state, and lease release. |
| AC7 | Opening, copying, and closing any JSON view leaves grid selection, edits, result page, and database contents intact. | Native grid and database fixture before/after all three actions and close paths. |

## Technical constraints and likely areas

Follow repository `AGENTS.md` and `desktop/design_system/AGENTS.md`. Keep reusable syntax presentation in the design system if it has a reusable contract; keep query, paging, parsing eligibility, loading, and staged data state in app/model code. Inspect `desktop/design_system/README.md` and existing call sites before extracting a component. Do not add screen-owned QSS, literal colors, font sizes, or an empty subclass. Preserve object names, accessible names, focus, signals, and existing row action behavior apart from the stated JSON embedding change. Avoid a lossy conversion path for 64-bit integer JSON output. Ensure the context-menu census sees any new UI construction pattern.

Likely areas: `desktop/app/query_workspace.cpp`, `desktop/app/query_workspace_json.cpp`, `desktop/app/query_workspace.h`, `desktop/models/result_table_model.*`, design-system text/color code if needed, and focused native tests under `tests/desktop/`. Add/update a real Light/Dark gallery specimen and matching test for any design-system component edit. Run `python3 scripts/ci/ui_consistency.py` (inspect `--json`), `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present.

## Rollout, assumptions, and risks

This is an additive transient UI feature; no setting, migration, or rollout flag is needed. Table JSON represents the loaded page, not the complete query. A plain text cell may be inspected as JSON without changing how that text is serialized in row/table JSON. The implementation may choose exact highlighter structure, output cap, loading cap, and progress wording, provided the observable behavior above holds. Larger pages and nested JSON can be expensive to format; keep the UI responsive and report a safe limit rather than truncating. No user decision remains open.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and applicable repository instructions, then invoke `$implement` with this spec path. Recheck current code before choosing the implementation.

```text
Use $implement with docs/specs/2026-09-29-colorful-json-views.md.
```
