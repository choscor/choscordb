# View row as JSON in a shared right sheet

Status: Confirmed for implementation
Date: 2026-09-27
Source: User request and brainstorm decisions in this session

## Outcome and current state

People inspecting SQL Results or Object Data can open the row under the pointer as complete, pretty printed JSON, then copy it. The presentation is a reusable right sheet with a modal backdrop, a header close button, and a fixed gray action footer.

Both result grids use `QueryWorkspace` and `ResultTableModel`. Their row context menu is installed in `desktop/app/query_workspace.cpp`. SQL Results constructs its grid in `desktop/app/main_window_ui.cpp`; Object Data constructs its grid in `desktop/app/object_data_workspace.cpp`. `ResultTableModel` owns typed cells, staged edits, inserted/deleted state, and deferred handles for large values. Display strings can abbreviate binary data and deferred values. The design system already has owner-contained modal backdrop/focus behavior in `desktop/design_system/dialog_presentation/`, a centered `ModalPanel`, and `DialogSections`; it has no right sheet. `ValueDetailDialog` demonstrates chunk loading through `EngineAdapter`. The existing JSON export is a separate whole-result flow.

## Requirements and decisions

1. Add a reusable **right sheet** presentation component owned by `desktop/design_system/`. It attaches to the right edge of the owning window, covers the available height, places a backdrop over the rest of that window, blocks interaction behind it, and responds to owner resizing. The sheet has a title/header with an accessible X close control, a body slot for feature content, and an action footer that stays visible while the body scrolls. The footer uses a semantic gray design-system surface in Light and Dark themes, without screen-owned colors or QSS. Choose responsive width and spacing from design-system metrics so the sheet remains usable in narrow windows. Reuse existing modal presentation/focus behavior where its contract fits; avoid duplicating backdrop behavior.
2. Closing through X, Esc, or backdrop click has the same effect. Opening moves keyboard focus into the sheet; closing restores the prior valid focus. The sheet remains in the same application window and cannot leave the background interactive. Give the sheet, title, close button, content, and footer actions useful accessibility names.
3. Add **View row as JSON** to the row context menu in both SQL Results and Object Data. Target the row under the pointer, even if a different row is selected. Disable it on empty space, an invalid/stale model, or a row that cannot be read. The feature is available for read-only results as well as editable ones. Keep existing menu actions and behavior intact.
4. The feature sheet title is “View row as JSON.” Its body contains a read-only, selectable, scrollable text area with indented JSON for the entire clicked row. The fixed footer contains **Copy JSON**. Copy puts exactly the displayed JSON on the clipboard and leaves the sheet open. The view does not write to the database or change staged edits.
5. JSON represents the **current typed row**, including staged cell edits and staged inserted rows. Map SQL NULL to JSON `null`, booleans to booleans, integers and finite floating values to JSON numbers, and text/date/time/JSON-typed text to JSON strings; do not turn display formatting into data or parse a text cell merely because it contains JSON. Preserve 64-bit integer precision in emitted numeric text. Represent binary bytes, including empty binary, as `{ "$binary": "<base64>" }`. Distinguish empty text from NULL. For a staged inserted field omitted so a database default may apply, do not imply the default has been fetched; document the chosen representation in the UI and tests. Do not expose an internal handle or abbreviated display preview as a value.
6. Use column names as object keys in result-column order. If a name repeats, add a stable numeric suffix such as `name (2)`, choosing a unique key even if that text is itself an original column name. Give empty column names stable fallback keys. All columns must remain present. JSON output must be valid and deterministic for the same row state.
7. If the row contains deferred large values, load their **full** contents before presenting the completed JSON. Show progress while loading, permit cancellation by closing, and prevent a partial document from being copied as complete. If fetching, decoding, or resource limits prevent a full row, show a clear error and no misleading completed JSON. Ignore late events after closing, paging, query change, or opening another row; release transfer leases/resources. Loading and serialization must respect existing memory budgets and keep the UI responsive. An implementation may fail clearly at a safe resource limit rather than truncate. Non-finite numbers or other unrepresentable typed values likewise require a clear failure instead of invalid JSON.

## User flow and edge cases

- Right-click a cell in either grid and choose **View row as JSON**. The sheet opens at the window's right edge. The user can select text, press **Copy JSON**, or close by X, Esc, or backdrop click.
- A right-click on blank table space cannot show the currently selected row as JSON. A row with staged edits shows those values; read-only rows remain inspectable. A staged deleted row may still be inspected while visible, reflecting its current values without changing deletion state.
- When a large value needs loading, the user sees loading progress. Complete JSON appears only after every field is available. Failure or cancellation never produces a copyable partial result. A query/result invalidation while loading ends the operation cleanly.
- The text area scrolls within the sheet; the footer and close button remain reachable on a small window. Copy does not dismiss the sheet.

## Acceptance criteria and public test seams

| ID | Observable outcome | Public seam / evidence |
| --- | --- | --- |
| AC1 | Both grids expose the same enabled **View row as JSON** action for a clicked row, independent of selection; blank/stale targets cannot open it. | Native `QTableView` context menu through SQL Results and Object Data, checking action name/enabled state and the row shown. |
| AC2 | The sheet is right aligned within the owning window, has a backdrop, fixed gray footer and X control, blocks background input, resizes with the owner, and closes by X, Esc, and backdrop click with focus restoration. | Real component in Light and Dark gallery specimens plus native widget geometry, focus, keyboard, pointer, and accessibility observations. |
| AC3 | The read-only text area shows valid, complete, pretty JSON in column order; Copy JSON writes exactly that text and keeps the sheet open. | Open from each grid, parse displayed/clipboard JSON, compare them, and attempt editing the text area. |
| AC4 | Typed values, NULL/empty text, staged edits, binary base64, 64-bit integers, duplicate/empty column names, and staged omitted fields are represented without data loss or key collision. | Public `ResultTableModel::setPage`/edit seam feeding the real grid action, then parse displayed JSON against typed fixtures. |
| AC5 | Deferred text/binary values load fully; an error, cancellation, stale result, or budget limit cannot expose or copy a partial row. | Public `EngineAdapter` value-chunk/event seam with delayed, multi-chunk, failure and cancellation fixtures; observe sheet status, clipboard enablement, and lease release. |
| AC6 | Opening and closing a sheet does not alter selected cells, staged edits, query result, or database contents. | Native grid and database fixture before/after opening, copying, and each close path. |

## Technical constraints and likely affected areas

Follow the repository UI ownership rules in `AGENTS.md` and `desktop/design_system/AGENTS.md`. Before extracting the sheet, check `desktop/design_system/README.md` and call sites of `DialogPresentation`, `ModalPanel`, and `DialogSections`. Keep reusable presentation in the design system and row data/loading state in app/widgets. The sheet should accept arbitrary body and footer actions so it is a genuine shared component, while this feature supplies the JSON text and Copy JSON action.

Likely areas: `desktop/design_system/` for the sheet and semantic tokens/QSS as needed; `desktop/tools/preview/preview_window.cpp` and `tests/desktop/preview_test.cpp` for real Light/Dark specimens; `desktop/design_system/README.md` ownership/specimen tables; `desktop/app/query_workspace.cpp` for the common menu and lifecycle; `desktop/models/result_table_model.*` for safe typed row access; `desktop/bridge/engine_adapter.*` only if existing chunk APIs need coordination; and focused `tests/desktop/` tests. Preserve object names, focus, signals, shared stock control styles, and existing result/edit behavior. Do not use a lossy `QJsonDocument` conversion for 64-bit integers. Use column names rather than decorated header labels.

Run `python3 scripts/ci/ui_consistency.py`, `python3 scripts/ci/ui_policy.py`, and `python3 scripts/ci/qss_policy.py`. For native UI changes, build and run relevant CTest targets, then the full native suite when dependencies are present. The design-system change also needs its visible Light and Dark gallery specimen and matching test. No migration, setting, or rollout flag is needed; this is an additive transient view. Existing supported platforms and accessible keyboard operation must remain usable.

## Risks and bounded implementation choices

Deferred values can be very large. Full output may exceed the application's safe display or memory budget; report that limit explicitly instead of truncating or freezing. The existing chunk API and lease lifecycle need inspection before deciding how to assemble the row. Exact sheet width, animation, footer padding, blank-column fallback spelling, and presentation of omitted staged fields are implementation choices as long as the observable requirements above hold. No user decision remains open.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and applicable repository instructions, then invoke `$implement` with this spec path. Recheck current code before choosing the implementation.

```text
Use $implement with docs/specs/2026-09-27-json-row-right-sheet.md.
```
