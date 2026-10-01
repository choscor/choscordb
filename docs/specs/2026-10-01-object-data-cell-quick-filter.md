# Object Data cell Quick Filter

- Status: Ready for implementation
- Date: 2026-10-01
- Source: User's cell-menu reference image and brainstorm decisions Q1–Q7 in this session; current repository inspection.

## Outcome and current state

Object Data users can right-click a cell, hover over **Quick Filter**, and select a predicate built from that cell's column and typed value. The action combines the predicate with the visible SQL filter using AND and immediately applies it.

`desktop/app/query_workspace.cpp` installs the shared result-grid context menu and already captures the cell under the pointer using a persistent model index. `desktop/app/query_workspace_view.cpp` creates `ResultFilterBar` only for Object Data (`widgets_.objectReadOnly`), and routes filter requests through the existing result-view operation. SQL Results has no filter bar and is excluded from this feature. The class flag's name does not imply Object Data cannot have staged edits.

`desktop/app/result_filter_bar.*` exposes the draft through `conditions()`, sets text through `setExpression()`, and tracks the accepted filter separately. `crates/core/src/result_predicate.rs` evaluates SQLite conditions locally against captured typed rows, not against the source database. The existing result-view pipeline handles complete-result scanning, paging, sort, cancellation, and error recovery. `crates/bridge/src/foreign_key_filter.rs` and `crates/driver-api/src/foreign_key_filter.rs` illustrate typed predicate generation, but their foreign-key-specific eligibility policy is not the Quick Filter policy.

The working tree was clean before this spec was added. This brainstorm session changes documentation only.

## Decisions and requirements

### Menu and target

1. Add **Quick Filter** near the top of the Object Data cell context menu, before copy actions, using the existing shared QMenu presentation. Hover opens the submenu; preserve normal Qt keyboard submenu navigation and accessible action labels. Do not add the reference image's unrelated copy actions or decorative star.
2. The target is exactly the right-clicked cell, even when another cell or multiple cells are selected. Do not modify selection merely to target filtering. Never fall back to the current selection for blank space.
3. Submenu labels use the actual result column name and a template placeholder, such as `id = value`, `id != value`, `id < value`, `id <= value`, `id > value`, `id >= value`, `id IN (value)`, and `name LIKE value`, in that order. Labels need not include potentially large or sensitive cell contents; the resulting expression in the filter bar displays the applied literal.
4. For a NULL cell, replace the eight templates with `<column> IS NULL` and `<column> IS NOT NULL`. For ordinary values, retain the eight templates and disable unsupported operators with explanatory tooltips. If no safe action is available, disable Quick Filter with a reason. It is unavailable on blank space, an invalid/stale index or model, a missing result/connection, or while competing result work is active. Recheck availability on activation; menu enablement alone is not sufficient.

### Value and operator policy

Use the original typed cell value, never display text or a truncated preview. Rust owns eligibility and SQL generation. Use current result column names, not source-table-qualified names, because predicates address captured result columns.

| Clicked value | Enabled operators |
| --- | --- |
| NULL | IS NULL, IS NOT NULL |
| Integer, finite real, safely representable decimal | =, !=, <, <=, >, >=, IN |
| Ordinary text | All eight templates |
| Boolean, binary | =, !=, IN |
| Date/time/timestamp, UUID, JSON represented as text by the existing result filter | Text templates using their existing representation and SQLite semantics |
| Deferred, fallback, unavailable, nonfinite real, precision-losing decimal, or otherwise unsafe value | None, with a reason |

- Always double-quote and escape column identifiers. Quote text with doubled apostrophes; encode Boolean as 1/0, numbers without text quoting, and binary as a SQLite hex literal. Preserve empty strings, empty binary, Unicode, and NULL distinctions. Reject an invalid identifier or any value the existing evaluator cannot represent safely, including embedded NUL if no safe literal representation is provided. Do not broaden existing precision rules or silently round decimals.
- `IN` uses one typed literal: `"id" IN (42)`. It does not collect selected cells or parse commas in a clicked string as a list.
- `LIKE` means contains matching: `"name" LIKE '%Alice%'`. Escape the value's literal `%`, `_`, and the chosen escape character and emit an explicit SQLite ESCAPE clause. Apostrophes still require SQL literal escaping. LIKE on an empty string follows SQLite's `LIKE '%%'` semantics. Do not introduce a new case/collation policy; reuse the existing SQLite result-filter behavior.
- Inequality retains SQLite NULL semantics. `"id" != 42` does not automatically include NULL rows. Date/time ordering and JSON matching use the existing textual SQL representation, not newly introduced temporal or structural comparison rules.
- A clicked safe value does not establish that every row in the column is filterable. If a later row has a deferred/unsupported/precision-losing value, retain the existing scan failure and previous-view recovery behavior.

### Composition and immediate application

1. Read the filter text currently visible in `resultFilterSql`, including unapplied edits. Whitespace-only text counts as empty. With an empty draft, set the generated predicate alone. Otherwise combine the draft and generated predicate with AND, then immediately use the existing filter request path with the current sort unchanged.
2. Keep parentheses minimal and AND chains flat. Examples:

   ```sql
   -- Empty draft, clicked id = 42:
   "id" = 42
   -- Draft: id > 1; clicked name = Alice:
   id > 1 AND "name" = 'Alice'
   -- Draft: id = 1 OR id = 2; clicked name = Alice:
   (id = 1 OR id = 2) AND "name" = 'Alice'
   -- Another quick filter, clicked active = true:
   (id = 1 OR id = 2) AND "name" = 'Alice' AND "active" = 1
   ```

3. Remove redundant enclosing parentheses only when demonstrably safe; add grouping where precedence requires it. Preserve existing condition order and contents otherwise. Do not deduplicate conditions, resolve contradictions, reorder clauses, or perform Boolean algebra. Repeated clicks may intentionally produce duplicate predicates, but must not repeatedly wrap the whole AND chain.
4. Composition belongs in Rust and must understand quoted strings/identifiers and SQL expression structure. A naive search for `OR`/`AND`, splitting on AND, or trimming parentheses is unsafe for literals, functions, BETWEEN, CASE, and nested expressions. Choose a compact token/structure-aware implementation, conservatively grouping valid expressions when necessary. Do not change the existing accepted SQL language merely to simplify formatting.
5. Invalid existing draft text must not be ignored, replaced by the last accepted filter, or submitted as a different valid expression. Leave it recoverable in the composed draft, report the existing inline validation error, and preserve the previously accepted result view. If predicate generation/composition itself fails, show a clear filter error and leave draft and active view unchanged.
6. Reuse the existing pending-edit Apply / Discard / Cancel workflow. Capture the clicked typed value before that workflow can discard staged edits or refresh the model. Cancel starts no filter operation and restores the accepted filter using the existing workflow; Apply continues only after the edit/refresh operation is ready. Do not bypass pending-edit review or apply against a different result/object.
7. A successful request displays the generated/combined expression, starts the existing complete-result scan, preserves sorting, and shows page 1 of the new view. Preserve existing busy/progress/Cancel handling. Scan failure or cancellation retains the previous usable view and leaves the composed draft available for correction under the existing filter-error behavior. An invalidated result, navigation, disconnect, or stale menu activation must never apply a predicate to a replacement result.
8. Existing paging, clear-filter, refresh, result lifecycle, copy, and export rules remain authoritative. No new saved filter state, database query rewrite, database mutation, or persistence migration is needed.

### Decision record

| ID | Decision | Status and source |
| --- | --- | --- |
| Q1 | Object Data only | Settled: user chose Object Data only. |
| Q2 | Apply immediately | Settled: user chose immediate application. |
| Q3 | Right-clicked cell only | Settled: user chose the cell under the pointer. |
| Q4 | Combine with visible draft; preserve precedence | Settled: user accepted recommendation and requested simpler SQL without growing parentheses. |
| Q5 | Contains-LIKE with literal wildcard escaping; singleton-IN | Settled: user accepted recommendation. |
| Q6 | Adapt safely by value, NULL-specific actions, disabled unsupported operations | Settled: user selected safe adaptation; the concrete type matrix follows the existing evaluator and the policy presented in chat. |
| Q7 | Minimal parentheses; preserve conditions | Settled: user explicitly chose minimal parentheses without deduplication. |

Dependencies: Q1 determines the integration surface; Q2/Q3 determine action flow and target; Q4 and Q7 govern composition; Q5/Q6 govern predicate generation. All material decisions are settled. Exact helper names, bridge DTO layout, action object names, and the internal structural-analysis mechanism are implementation details deferred to the implementation session. Keep those compact, typed, and compatible with the behavior above.

## Acceptance criteria and public test seams

Use Rust tests for SQL/eligibility policy and native tests for UI/bridge integration, following the existing test boundaries inspected and presented during brainstorming.

| Criterion | Observable seam |
| --- | --- |
| Quick Filter exists only in Object Data; hover and keyboard navigation expose the correct ordered templates, with column-specific labels and NULL alternatives. | Native Object Data and SQL Results context menus in `tests/desktop/result_view_workspace_test.cpp` or `object_data_workspace_test.cpp`; exercise submenu hover/navigation through the shared menu surface in Light and Dark. |
| Right-clicked cell supplies the column/value irrespective of other selections; blank space is unavailable; stale activation cannot affect a new model/result. | Native table coordinates with a different selected cell and multi-selection; inspect enabled state, filter text, and visible rows; invalidate or replace the result before activation. |
| Type matrix and generated literals are safe and accurate, including quotes in names/values, Unicode, empty values, Boolean, binary, decimal limits, nonfinite values, NULL, and unsupported/deferred cells. | Public Rust predicate-generation/eligibility boundary with typed fixtures; thin adapter/bridge tests verify conversion and the UI's enabled state/reason. SQL output is also evaluated through the existing engine filter command/event seam. |
| LIKE contains literal wildcard characters; IN treats a comma-containing string as one value. | Rust-generated predicates executed through the existing core result-filter seam against `%`, `_`, escape characters, apostrophes, empty text, and comma-containing fixtures; native activation confirms filter-bar text and matching rows. |
| Visible unapplied text is used; OR is grouped safely, AND chains remain flat, repeated clicks do not grow wrappers, and duplicates remain. | Public Rust composition boundary with strings/quoted identifiers containing AND/OR/parentheses, BETWEEN, CASE, functions, nested expressions, and redundant outer grouping; compare resulting SQL truth behavior through the existing evaluator, plus native draft-to-action activation. |
| Invalid drafts and scan failures preserve the accepted view, expose an error, and retain a recoverable draft; generation failure preserves the original draft. | Native `resultFilterSql`/`resultFilterError` and table observations; real invalid expression and deferred/precision-loss fixtures through the public engine command/event seam. |
| Choosing an enabled option immediately applies across the full captured result, preserves sort, and resets to page 1; no additional Apply click is needed. | Real multi-page SQLite Object Data fixture with matches beyond page 1, a preexisting sort, native submenu activation, filter text, pager and visible-row observations. Verify normal clear/refresh behavior continues through the same pipeline. |
| Busy operations prevent competing filtering; pending edits use existing Apply/Discard/Cancel; cancellation, navigation, and stale completions remain safe. | Existing native result-view/grid-edit workflows, including a staged edit in the clicked cell, edit refresh readiness, view progress/cancellation, and result replacement. Observe filter text, accepted rows, and absence of unintended writes. |

## Constraints and risks

- Follow root `AGENTS.md`: Rust in `crates/core` owns predicate policy, SQL literal escaping, validation, and expression composition. C++/Qt owns menu presentation, typed cell capture/conversion, transient UI state, and invocation. Keep the CXX bridge small and typed; do not implement SQL rules in C++ or duplicate them in bridge wrappers. Keep scans and database work off the UI thread.
- Likely affected files: `desktop/app/query_workspace.*`, `query_workspace_view.cpp`, `result_filter_bar.*`, `desktop/bridge/engine_adapter.*`, `crates/bridge/src/`, a focused core Quick Filter module, and relevant Rust/native tests. Inspect current code before selecting exact APIs.
- Prefer existing shared menu styles and submenu placement from `desktop/design_system/menu/` and the ownership map in `desktop/design_system/README.md`. No new design-system component is required by this spec. Preserve existing menu actions, object/accessibility names, focus, and signals. Do not add screen-owned QSS, colors, fonts, or spacing literals. If shared design-system code must change, follow its `AGENTS.md`, including gallery specimens and tests.
- Preserve the SQLite filter's SQL size, expression depth, resource limits, read-only restrictions, and supported representation. Report limit failures through existing errors rather than increasing limits to accommodate growing wrappers.
- Structural SQL analysis is the primary correctness risk: avoid confusing AND inside BETWEEN or OR inside strings with Boolean composition. Do not substitute a simplistic regular expression for precedence handling.
- Staged cell values and model/result changes during menu/dialog lifetimes need explicit identity and readiness checks. Capture typed input before resolving edits, recheck result identity before submitting, and preserve existing deferred-request behavior.
- Follow `docs/CI.md`. For implementation run `python3 scripts/ci/cpp_ownership.py`, `ui_consistency.py`, `ui_policy.py`, and `qss_policy.py` from `scripts/ci/`; inspect JSON censuses where relevant and report remaining pre-existing ownership failures. Run focused Rust policy/engine tests and native CTest targets `result-view-workspace`, `object-data-workspace`, and affected grid-edit/menu tests. Run the full native suite when dependencies are present. No runtime tests are required for this documentation-only brainstorm handoff.
- No material decisions remain unresolved. SQL Results support, multivalue IN, editable pattern/list dialogs, NOT LIKE/NOT IN additions, saved templates, deduplication, and general SQL rewriting are outside this change.

Read this whole spec and inspect the current workspace before implementing.

## Fresh-session instruction

```text
Use $implement with docs/specs/2026-10-01-object-data-cell-quick-filter.md.
```
