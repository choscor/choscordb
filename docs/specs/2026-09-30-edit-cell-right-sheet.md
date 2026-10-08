# Edit a cell in a right sheet

- Status: Ready for implementation
- Date: 2026-09-30
- Source: User request and annotated context-menu screenshot; brainstorm decisions accepting `Edit cell…`, existing staged Apply flow, existing cell editability, and immediate draft discard on dismissal.

## Outcome and current state

Users can edit an eligible table cell in a larger, multiline textarea. Right-clicking a cell exposes `Edit cell…` as the first action, followed by a separator before the existing JSON actions. Selecting it opens an owner-contained modal right sheet with the current cell value and Save and Cancel buttons.

`desktop/app/query_workspace.cpp` constructs the shared context menu used by table Data and SQL result grids. It already captures a persistent clicked index for cell-specific JSON actions. `desktop/models/result_table_model.cpp` owns visible page state and staged edits; its flags express inline editability. `desktop/app/query_workspace_edit.cpp` applies staged edits through the existing SQL-and-parameter review and atomic backend batch. `docs/specs/2026-09-20-editable-table-results.md` establishes that edits remain local until Apply.

`desktop/design_system/right_sheet/right_sheet.h` supplies the existing owner-contained modal right-sheet shell with title, body, close button, footer, shared backdrop, focus containment and restoration. `desktop/app/query_workspace_json.cpp` demonstrates its integration. Stock `QPlainTextEdit` is styled by the shared text-area rules described in `desktop/design_system/README.md`.

## Decisions and requirements

### Menu and scope

1. Use the label `Edit cell…`. The user accepted this recommendation instead of `Edit in Modal`; the label describes the action, while the right-sheet presentation remains its implementation.
2. Place the action first and a separator immediately after it. Preserve all existing actions, their order, object names, accessibility, signals and separators otherwise.
3. Target exactly the cell under the pointer when the menu opened, independently of current selection or selected cells. Opening the editor must not write to the model or database.
4. Use existing inline cell editability in both table Data and eligible SQL result grids. Do not broaden backend edit eligibility. Leave the action present but disabled for an invalid or blank target, stale model/page, noneditable cell, or activity that already prevents grid editing. Recheck the target and eligibility when the action executes and when Save executes.
5. Existing exclusions remain effective: existing key and generated columns, deleted rows, read-only/unmappable results, binary, deferred, fallback and unavailable values. Inserted cells follow existing insert editability. Rust may disable all existing-row edits for a page with unsafe originals; respect this result even when the clicked cell itself is scalar.

### Sheet and draft

6. Reuse `design::RightSheet`, with title `Edit cell…`, one plain multiline textarea, and shared Save and Cancel buttons in the footer. A concise column identifier, type, status or error label may supply context; do not add another editor, a NULL switch, syntax editor or SQL review to this sheet.
7. Show the complete current staged cell content, rather than the original pre-edit value or a grid preview. Preserve multiline text, whitespace and punctuation exactly. Use the established Rust parsing policy for nontext scalar values; do not format JSON or trim arbitrary text. Focus the textarea on opening, preserve ordinary multiline Enter behavior, and use the existing shared focus-restoration contract on closing.
8. The sheet owns only its temporary draft. Typing does not change the model. Opening and cancelling after a previous inline edit must preserve that previously staged value and all unrelated pending edits.
9. Cancel, close button, Escape and backdrop click immediately discard the sheet draft and close it, without confirmation. This is the explicit user decision. Existing staged grid changes are not discarded. Reopening starts from the current model value; do not persist drafts.
10. Save stages only the target cell, through the existing grid edit pipeline. It does not issue SQL, write directly to the database, trigger Apply, or open the SQL review. On success or unchanged Save, close the sheet and restore focus through the shared shell. The existing Apply control, review, atomic execution, error handling, transaction restriction and navigation prompts remain the database-writing workflow.
11. Invalid input or staging failure leaves the sheet open, retains the exact draft and shows an actionable accessible error. Distinguish type rejection, memory/resource refusal, and stale or ineligible target as practical. Do not partially mutate the target or unrelated staged changes on rejection.

### Typed NULL, empty text and insert defaults

12. Bounded implementation assumption: retain the existing grid `Set NULL` action as the explicit way to stage SQL NULL; no new NULL-writing control is required inside the sheet.
13. When the current cell is SQL NULL, initialize the textarea blank and display a separate SQL NULL state hint. An untouched inserted cell also starts blank with a separate database-default/omitted state hint. These state labels are not textarea content.
14. Preserve the initial typed state if the user makes no edit and presses Save. In particular, SQL NULL remains NULL, and an untouched inserted cell remains omitted so its database default can apply.
15. If the user explicitly edits the textarea, submit the actual text through Rust parsing even when it is blank or the literal `NULL`. For a text column, blank becomes an empty text value and `NULL` becomes literal text. For recognized nontext types, the same existing Rust type validation applies. Do not infer SQL NULL or an insert default from textarea content, and do not use grid display-marker equality as a typed no-op rule. Track initial state and user-edit intent so programmatic initialization does not count as a user edit.

### Failure and lifecycle

16. Bind the draft to the clicked persistent index and the workspace's result/target generation. Do not save by bare row and column after page replacement. A result/model reset, target replacement, disconnect, target-row removal or workspace destruction must invalidate and release the editor safely; no obsolete draft may write into a new page or another connection.
17. Use the modal shell's existing input boundary, so ordinary grid changes are blocked while editing. Also guard deferred/programmatic activity. If the target cell changes externally while the draft is open, invalidate and close the draft safely rather than overwriting that newer cell. Recheck all state before accepting asynchronous validation results. Changes to unrelated cells do not authorize writing a stale target.
18. Respect existing resident/staged memory limits and refusal behavior; do not introduce unbounded backend snapshots, silently truncate textarea text, or bypass a staging budget. If parsing or conversion requires backend work, keep it off the UI thread and bind completion to the current draft. Pending work must not resurrect a dismissed sheet or access destroyed owners.

Non-goals: immediate database Save, broadening editability, binary/deferred editing, syntax highlighting or JSON beautification, cross-page draft retention, autosave, durable draft recovery, new transactions, and redesigning the existing Apply flow.

## Acceptance criteria and public test seams

| Criterion | Observable seam |
| --- | --- |
| `Edit cell…` is the first menu action, followed by a separator; existing actions remain ordered. | Qt context-menu interaction in `tests/desktop/object_data_workspace_test.cpp`, including inspection of QAction sequence/separators. |
| Right-click on an eligible cell opens the sheet for that clicked cell even when another cell or multiple cells are selected. Blank, stale, deleted, existing-row key/generated, binary/deferred and read-only targets cannot open an editable sheet. | Table Data and SQL-result widget tests invoking the real context menu on cell geometry; existing Rust grid eligibility tests verify domain policy. |
| The sheet shows complete current/staged multiline text without changing it and focuses its single textarea; Enter inserts a newline. | Qt interaction tests inspecting textarea text, focus and model state in `object_data_workspace_test.cpp` or `result_view_workspace_test.cpp`. |
| Typing alone makes no model or database change. Save stages only the clicked cell, closes, enables existing Apply when appropriate, and preserves other staged cells. | Real SQLite table Data and eligible SQL-result interaction tests, observing model pending state and querying the database before Apply. |
| Existing Apply review and confirmation remain required before DB mutation; cancellation of review preserves the saved staged edit. | Existing SQLite workspace Apply/review integration boundary in `object_data_workspace_test.cpp` and `grid_edit_workspace_test.cpp`. |
| Cancel, X, Escape and backdrop click discard a dirty draft immediately, preserve previous staged values, and restore focus; reopening shows model content. | Public RightSheet/widget interaction tests using each dismissal path and inspecting model and focus. |
| No-edit Save preserves SQL NULL and omitted insert defaults. Explicitly edited empty text and literal `NULL` remain distinct from SQL NULL for text cells. | Qt editor interactions plus `tests/desktop/result_model_test.cpp` through the public typed staging boundary; SQLite reads after existing Apply verify NULL/empty/default semantics. |
| Invalid integer/bool/float input and budget refusal retain the draft, show an accessible error and leave model state unchanged; correction can then Save. | Public Rust parsing tests in `crates/driver-api/tests/edit_value.rs`, model staging tests with `setByteBudget`, and Qt Save/error interactions. |
| Page/model reset, disconnect, target-cell replacement and owner teardown cannot save or reopen an obsolete draft; canceled async completions are harmless. | Workspace integration tests using real load/reset/disconnect boundaries and readiness signals, following `grid_edit_workspace_test.cpp` race patterns. |
| Existing typed scalar and Apply rules remain unchanged, including conservative eligibility and manual transaction restrictions. | Existing `result_model_test`, `result_view_workspace_test`, Rust `grid_edit` tests and driver-backed grid Apply scenarios. |
| The new controls use shared presentation and occur in the consistency census. | `python3 scripts/ci/ui_consistency.py --json`, plus the UI, QSS and C++ ownership gates. |

## Constraints and risks

- Follow repository `CLAUDE.md`: C++/Qt owns presentation and transient draft/model interaction; Rust owns parsing, validation, eligibility and database execution. Reuse `crates/driver-api/src/edit_value.rs`, `crates/driver-api/src/grid_edit.rs` and typed bridge conversion rather than implementing type rules in C++.
- The current `ResultTableModel::setData` only returns `bool`, conflating parsing and memory rejection. Its EditRole string equality shortcut cannot express an explicit literal `NULL` update from SQL NULL. Implementation should expose the smallest typed staging/error boundary needed to preserve the stated semantics; keep ordinary inline behavior compatible. Domain errors must originate in Rust when domain validation is required.
- Use `desktop/design_system/README.md` ownership map: RightSheet shell, shared text-area style, semantic button/label presentation, theme colors, typography and spacing tokens. Do not add screen-owned QSS, literal visual colors or fonts, or a new empty editor subclass.
- Likely changes: `desktop/app/query_workspace.{h,cpp}`, a compact edit-sheet workflow file if appropriate, `desktop/models/result_table_model.{h,cpp}`, Rust parse DTO/bridge only if needed for errors or typed write intent, and relevant workspace/model tests. Register any new source in the existing CMake source list. Inspect current contents and uncommitted changes before choosing exact files.
- A sheet can retain a second copy of text temporarily. Honor existing page and staging budgets; this feature does not enable full deferred-value loading or expand resource limits.
- No durable-state migration, saved-workspace draft format, rollout flag, export format or release operation is required. No network research was required: repository interfaces and the user's product decisions establish this change.
- Run `python3 scripts/ci/cpp_ownership.py` after native/bridge changes, inspect `--json` as necessary, resolve introduced or touched findings and explicitly report remaining pre-existing failures. Also review changed C++ for backend behavior the checker cannot detect.
- Run `python3 scripts/ci/ui_consistency.py`, `python3 scripts/ci/ui_policy.py` and `python3 scripts/ci/qss_policy.py`. Verify new construction sites in the JSON census; extend checker regression coverage only if a new construction pattern requires it.
- Build and run relevant native CTest targets and the full native suite when dependencies are present. Run focused Rust parser/policy tests for changes to those contracts. Synchronize tests on actual eligibility/request/operation readiness rather than a single event-loop turn or idle snapshot.
- Shared design-system changes are not expected. If required, read `desktop/design_system/CLAUDE.md` and update its Light/Dark gallery specimen and corresponding test.

Read this whole spec and inspect the current workspace before implementing.
