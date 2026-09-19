# ChoscorDB: MVP design fidelity and complete desktop flows

- Status: Implemented; checkpoint 2 visual review accepted on 2026-09-19.
- Date: 2026-09-13 (Asia/Ho_Chi_Minh).
- Source: User-requested whole-app UI/UX upgrade following `docs/mvp-design`, developed using `$brainstorm`.
- Approval: The user accepted recommendations 1–4 (scope, fidelity, missing-state treatment, review checkpoints), then approved all five visual scenes covering recommendations 5–9 (result ownership, object data, navigation, dialogs, migration).

## Completion record

The user identified the components as already done before requesting screen/flow
implementation, then accepted the completed comparison/evidence package with
“ok good, continue” after the checkpoint 2 review request on 2026-09-19.
Implementation and the required visual review are complete. The presented
form-layout variations remain documented in the
[completion evidence](../design/mvp-flows-implementation-evidence.md).

Validation passed: 36 native suites, 268 Rust tests, explicit PostgreSQL integration,
and 100 Cocoa screen/dialog captures. The existing 16ms GUI-dispatch target still
fails in both baseline and current measurements; broader release-performance
qualification remains unverified. Visual acceptance does not waive those limits.
No commit or publication is implied by this completion record.

## Outcome and authority

Upgrade the native database app bottom-up: **design tokens → shared components and states → compositions → screens and complete flows**. Developers and analysts should get the appearance and navigation of the MVP prototype with real SQLite/PostgreSQL behavior, preserved work, and clear execution context.

`docs/mvp-design/` is the new visual and layout authority. Use all loaded CSS and JavaScript, including their final overrides and dynamically constructed content, not isolated source declarations or obsolete markup. This spec governs production behavior where the illustrative prototype is incomplete or conflicts with the approved decisions below.

This supersedes conflicting visual, layout, modal, and flow requirements in `docs/specs/2026-09-12-modern-ui-system.md`, `docs/specs/2026-09-12-shadcn-qt-design-system.md`, and `docs/design/`. Retain their nonconflicting database, accessibility, persistence, performance, and quality contracts. In particular, the old Nova/Neutral appearance and requirement to retain the history dock are superseded. Update active design documentation during implementation; retain historical evidence as historical.

The user requested visual explanations after finding the questionnaire too verbose. A temporary browser storyboard illustrated the five accepted flow decisions. Its conclusions are recorded here; the next session must not depend on that temporary file. It was a static proposal with example data, not an implemented application or pixel-fidelity evidence. No durable HTML companion was requested.

## Current workspace findings

- C++23, Qt Widgets, and QScintilla form the desktop; the Rust core is Qt-independent. Build and verification instructions are in `docs/BUILD.md` and `docs/CI.md`.
- `desktop/design_system/` already contains typed themes, typography, icons, components, control styling, modal panels, and a preview window. `desktop/tools/component_gallery.cpp` provides an existing developer gallery. Evolve these shared facilities rather than creating a parallel styling system.
- `desktop/app/main_window.cpp` composes a navigator dock, query toolbar, editor tabs, split editor/results area, and history dock. `AppearanceController` handles appearance/layout persistence.
- `desktop/app/query_workspace.*` owns one shared SQL query/result lifecycle. It also coordinates paging, export, large-value detail, connection selection, and transactions. SQL tabs do not currently own independent result lifecycles.
- `NavigatorModel` supports lazy, token-correlated children and stale-response rejection. Its exposed object records contain identity, name, kind, and child availability, not a complete tabular object-inspection schema.
- `desktop/bridge/engine_adapter.h` exposes metadata, DDL, execution, paging, export, value chunks, profiles, preferences, history, recovery, and transaction operations. Real metadata loading exists in both database drivers. Inspect and extend the typed contracts as necessary to support the new object panes; do not fabricate missing properties.
- Export and Preferences currently have nonmodal behavior. The approved modal conversion is intentional. Large-value inspection remains nonmodal.
- Existing native tests cover components, editor, results, navigation, preferences, profiles, history/recovery, export, cancellation, disconnect, and related workflows. Their current assertions are evidence to inspect, not immutable layout requirements.
- The working tree was clean before this spec was added. No production implementation or application test run was performed in this brainstorm.

## Scope and non-goals

Redesign every existing app-owned surface, including small controls, menus/popups, search/completion, confirmations, preferences/query settings, value detail, recovery, and exceptional states. Add the real read-only object explorer and central-screen navigation necessary to make the prototype's MVP flows work.

Include Start, SQL workspace, Object explorer, and History; sidebar connection management; Export and Preferences dialogs; Quick switch; real object Columns, Indexes, Keys, DDL, and Data views; and keyboard routes between these surfaces. Connections are managed in the sidebar and dialogs, not a separate Connections page.

Non-goals:

- Row editing, Changes/review/apply UI, schema mutation tools, additional database drivers, or unrelated SQL capabilities. Remove production routes to the prototype's deferred Changes concept.
- Independent SQL results or concurrent execution per editor tab.
- A web application, framework rewrite, custom title bar, replacement OS menu bar, or replacement system file pickers.
- Old/new appearance switching, user accent/density customization, new telemetry, or a distribution project.
- New Windows/Linux visual acceptance work. Existing cross-platform CI requirements remain in force.

## Visual foundation and matching contract

1. Pin the local reference revision/content hashes and capture its fully rendered screens, components, and applicable interaction states before changing production screens. Record browser/OS, logical viewport, device scale, resolved fonts, theme, fixtures, and capture procedure. Reference fixture setup must be repeatable and separate from real user profiles.
2. Match app-owned geometry, spacing, colors, typography, icons, borders, radii, elevation, and state treatments on macOS in Light and Dark at **1280×900** and **960×640 logical pixels**. Define corresponding app-content rectangles so native shell dimensions do not distort the comparison. Native title bars, OS menus, and system file pickers are explicit exceptions.
3. Use the reference's green palette and platform UI typography. Preserve user SQL-editor font settings; use defaults for baseline comparisons and test custom fonts separately. Keep System/Light/Dark, including live OS-following behavior. Do not retain Geist/Nova appearance merely because it is already implemented.
4. Derive token values from the final rendered reference and record CSS-to-Qt logical-unit mappings. Extend semantic roles for states absent from the prototype using the same visual language. Screens compose shared components; reusable visual constants belong in shared definitions.
5. Require side-by-side captures and difference/overlay inspection for geometry and rendering. Only documented unavoidable font-rasterization differences, native exceptions, and the accessibility/scaling exceptions below are acceptable. A broad whole-image similarity threshold is not proof of a match. The implementation must not silently substitute its own aesthetic or modify the reference to hide differences.
6. Keyboard access, visible focus, accessible names/roles, contrast, non-color status information, reduced motion, Unicode fallback, and Qt/OS scaling take priority where literal matching conflicts with usability. Record each actual exception. At minimum size, keep primary actions reachable and scroll content within panes. Do not sacrifice font legibility to fit a screenshot.
7. Use the existing production-backed gallery for deterministic specimens and actual interactive controls. It must run without a database or credentials and must not mutate production preferences. Keep developer-only gallery facilities out of release packages.

### State and variant coverage

Inventory every app-owned surface and map it to a shared component/composition or named native exception. Cover buttons/tool buttons and sizes, fields and forms, selectors/toggles, navigation rows, tabs, tables/lists/headers, paging, splitters/scrollbars, menus/tooltips/popovers, editor chrome/search/completion, modal/nonmodal content, and status/feedback.

For each, cover applicable normal, hover, pressed/clicked, keyboard focus, disabled, selected/checked/mixed, read-only, invalid, loading, success, warning, error, cancelling, cancelled, and disconnected states. Include open/close and focus restoration, pointer versus keyboard activation, long text, Unicode, overflow, and theme changes. Avoid meaningless state combinations. Motion should follow the reference where defined; missing effects should be restrained, consistent, and disabled or simplified for reduced motion. Include missing-state extensions in the component review before screen migration.

## Approved user-visible behavior

### D1 — One shared SQL result, with explicit ownership

- SQL editor tabs preserve independent drafts, undo, cursor/selection, and scroll state. One latest SQL result and message lifecycle remains shared across these tabs.
- Switching idle SQL tabs does not relabel a retained result as belonging to the newly selected document. Show the result's originating document and connection, along with actual execution status.
- Starting another SQL execution replaces the previous SQL result according to the existing execution lifecycle. Clearly remove or distinguish obsolete content during loading/failure; never present it as the new query's output.
- Preserve existing run scope, cancellation, destructive-statement confirmation, transaction, timeout, paging, and export semantics. Opening SQL/history/generated statements never executes them.
- Keep the executing document active while its query is running. Block actions that would hide its Cancel control, including switching away to another screen/document, with an explanation. Cancellation remains accessible while queued/running and shows a pending cancelling state until acknowledged. Navigation resumes after a terminal outcome.
- Independent results and concurrent execution per SQL tab were rejected for this scope.

### D2 — Separate, bounded object browsing

- Selecting a table/view opens its object context. Columns, Indexes, Keys, and DDL load real metadata lazily. Show applicable real properties, empty collections, unsupported/unavailable properties with reasons, loading, and retryable failures. Do not interpret unsupported as an empty collection or present fixture metadata in production.
- Explicitly opening Data initiates a bounded paged read of that object. It is a read-only UI: no editable cells or Changes actions. Provide Refresh, Cancel during loading, paging, copy, export, and large-value inspection with shared components.
- The object-data result is separate from the shared SQL result. Browsing and returning to SQL preserve the SQL draft and its result. No per-object unlimited cache or multi-query tab system is implied; maintain only bounded retained state.
- Do not fetch an entire table or issue an automatic `COUNT(*)`. Show actual loaded rows and known paging availability; totals remain unknown unless genuinely available. Use the existing page-size/budget policy, typed values, NULL/empty distinction, and bounded large-value treatment.
- Block conflicting operations on the same connection during query/export work with an explanation. Do not silently cancel SQL, replace its result, reconnect, or change transaction mode to make object browsing possible. Respect the existing connection's transaction state; browsing must not commit or roll back user work.
- Qualify and quote identifiers using the database dialect, including unusual/Unicode names. Object changes, refresh, cancellation, and disconnect invalidate stale requests and value/export handles as appropriate.
- Open query and Generate SQL create a new document associated with the object's connection, leaving existing drafts unchanged and dispatching no execution.

### D3 — Central screens and persistent browsing context

- Use Start, SQL workspace, Object explorer, and History as central screens sharing the full-height sidebar under the native title bar. Follow the prototype's tab placement, compact content panes, and pinned bottom actions.
- Replace the history dock with the History screen. Preserve its real filtering, retention/disable/clear behavior, confirmation, and inert reopen semantics. Reopening a selected entry creates a SQL document; a missing/disconnected profile is explicit and never triggers automatic execution.
- Quick switch and appropriate native menu commands navigate between these screens. Keyboard access must use the same navigation rules as pointer input. Preserve existing custom shortcut bindings and avoid new binding conflicts.
- Selecting a sidebar connection changes browsing context but never silently retargets an existing SQL document. Keep its execution target visibly identifiable and explicitly changeable. Preserve existing restrictions on changing context during active work.
- Start offers New connection and saved connections. Connect successfully before displaying real objects; initial object selection may open the first available object as in the prototype. Save & connect opens a new SQL document. Empty databases still permit opening a query.
- Connection dialogs support real PostgreSQL/SQLite fields, asynchronous Test, Save profile, and Save & connect; show validation, authentication/network/TLS/credential-store failures without losing the draft. Sidebar actions include appropriate connect/disconnect, edit/test, duplicate, and delete. No plaintext credential fallback or prototype restriction requiring one saved connection.
- Closing the last SQL tab returns to Start, subject to the existing dirty-document/recovery safeguards. Screen navigation itself never discards drafts or bypasses active-operation guards.

### D4 — Modal Export and Preferences

- Convert Export and Preferences into centered app-owned panels with a dimmed, inert parent, contained keyboard focus, and restoration to the invoking control or a sensible surviving fallback. Modal conversion supersedes older nonmodal requirements for these two surfaces only.
- Export retains native destination picking, supported formats, format-specific validation, overwrite confirmation, asynchronous operation, actual counts, retry, and atomic destination publication. Unknown totals use indeterminate progress, not fabricated percentages.
- Close/Escape/cancel during export requests cancellation and waits for terminal cleanup. Show Cancelling while waiting. Failures preserve the destination, explain the error, and permit retry. Disconnect/shutdown/query invalidation retain safe cleanup and stale-event handling.
- Preferences contains Appearance, SQL editor, Results & execution, History & recovery, and Keyboard shortcuts, composing existing supported settings and validation. Use **Save preferences** and **Close**.
- Appearance may preview live. Close discards unaccepted drafts and restores unsaved appearance against the current System theme where applicable. Save validates and persists accepted changes before closing. A failure retains the dialog and draft, identifies affected settings, and offers retry; do not claim success early. Preserve existing editor text, undo, selection, and query state when applying settings.
- Large-value detail remains nonmodal and bounded; query/connection invalidation closes or invalidates it safely. Existing query settings, completion, confirmations, recovery, and other omitted prototype surfaces receive the same shared visual treatment while retaining their established behavior.

### D5 — Replace appearance while preserving saved work

- Ship one replacement UI without an old/new toggle. Preserve saved profiles, credential references, SQL files, recovery buffers, history, custom shortcuts, editor fonts, and compatible window geometry/splitter placement.
- Reset only incompatible dock/layout placement; provide sensible defaults for the new central screens. Existing Reset layout remains meaningful. Preserve read compatibility for existing appearance settings; do not reintroduce legacy accent/density effects.
- Startup recovery remains disconnected and never executes SQL. Persist compatible records through existing storage contracts; any necessary new metadata must have explicit compatible handling and restart tests.
- macOS is the visual acceptance platform. Dedicated Windows/Linux visual verification is deferred, not removal of existing portability or CI gates.

## Implementation stages and review checkpoints

1. Inspect the current workspace, establish the reference capture manifest, inventory surfaces/states, and identify metadata/result-lifecycle gaps. Reconcile superseded design documentation.
2. Implement tokens, typography/icon treatment, shared components, and compositions. Produce runnable gallery evidence with Light/Dark, state/variant specimens, keyboard interaction, narrow layouts, and actual popup/modal behavior.
3. **Checkpoint 1: user reviews the concrete token/component gallery before production screen migration.** Approval of this spec/storyboard is not approval of future rendered components. Prepare all reviewable evidence before asking; do not add extra approval gates for routine implementation choices.
4. Migrate screens/flows using those approved components, extend typed metadata and bounded object-data behavior as needed, and preserve existing data semantics. Complete native workflow tests, reference comparisons, quality checks, and documentation.
5. **Checkpoint 2: user reviews completed screens and flows against the reference captures.** Present a concise visual comparison, exact exceptions, and validation results. Fix identified issues and update evidence before claiming completion.

## Observable completion and public test seams

These checks operationalize the approved behavior and visual review scope. Prefer actual UI input and observable output; use service boundaries for deterministic failures, never private-state assertions as substitutes for user-visible behavior.

| ID | Observable outcome | Public test seam / evidence |
| --- | --- | --- |
| AC1 | Every app-owned component and applicable variant/state follows the reference or a recorded exception. | Production-backed native gallery, deterministic browser/native captures, overlay review, pointer/keyboard activation, checkpoint 1. |
| AC2 | Start, SQL, Object, History, dialogs, and secondary surfaces match at both approved sizes in Light/Dark. Bottom actions remain reachable. | Real native windows and deterministic fixture screenshots; checkpoint 2 with explicit crop/scale/font metadata. |
| AC3 | Switching SQL drafts preserves buffers/undo/cursor/scroll; retained SQL results keep their originating identity. New execution replaces only the SQL lifecycle. | MainWindow input with two distinct SQL documents and real SQLite results, visible result labels/table, editor undo and selection observations. |
| AC4 | Object tabs show real metadata/DDL and correct empty, unsupported, error, and retry states. Stale requests cannot replace a newer object's view. | Object explorer UI plus real SQLite/PostgreSQL fixtures; adapter submission/event seam for delayed, failed, and reordered metadata. |
| AC5 | Object Data loads bounded pages without replacing SQL results, implicit full-table/count queries, writes, or transaction changes. Copy/export/detail operate on the correct source. | Navigate SQL → object → SQL using distinct fixtures; observable database operations, result tables, clipboard/export bytes, transaction effects, existing memory/paging seams. |
| AC6 | Active operations keep cancellation accessible; blocked navigation/context changes explain why. Cancellation/disconnect cannot update a later result with stale data. | Native mouse/keyboard workflows with delayed execution/export fixtures and adapter events; terminal status and restored action availability. |
| AC7 | Sidebar connection selection does not retarget existing SQL; history/generated SQL opens without execution. | UI actions, visible connection labels, execution-event observations with a positive execution control, generated editor text, database side effects. |
| AC8 | Profile Test/Save/Connect and management show real outcomes and preserve drafts on failure. Credentials remain protected. | Existing profile UI/service tests and injected credential failures; disposable SQLite/PostgreSQL connections and persisted profile observations. |
| AC9 | Export/Preferences block the background, contain focus, and restore it. Export cancellation cleans up; preferences failure retains drafts. | Actual native dialog boundaries with keyboard/pointer input; destination contents/temp cleanup and delayed storage/export responses; restart after successful save. |
| AC10 | Keyboard-only use, visible focus, accessible status, long/Unicode content, custom editor fonts, live themes, reduced motion, and scaling remain usable. | Qt accessibility interfaces, policy adapters, actual focused controls and native captures at supported scales/minimum size. |
| AC11 | Existing profiles/history/recovery/shortcuts/fonts and compatible layout survive upgrade/restart; recovery never connects or executes. | Temporary persisted pre-upgrade fixtures, actual application restart, public storage reads and connection/execution observations. |
| AC12 | Query/transaction safety, export atomicity, bounded rendering, startup, memory, responsiveness, and shutdown do not regress. | Existing native/Rust suites, owned PostgreSQL fixtures, `docs/performance/measurement-contract.md` probes against a valid baseline. |
| AC13 | Shared styling stays centralized; release resources are present and developer gallery UI is excluded. | Existing UI policy checks, component composition review, staged package/resource inspection where affected. |

Use `docs/CI.md` for required affected-code gates and the repository full quality command after integration. Adapt tests that encode superseded layouts instead of preserving incorrect UI to satisfy them. Report baseline failures and environment limitations separately. Browser mockups, structural checks, or headless tests alone do not establish native visual/focus/accessibility acceptance. Do not add tests that merely mirror visual constants; test meaningful rendering and behavior at the boundaries above.

## Technical constraints and likely affected areas

Likely areas: `desktop/design_system/`, `desktop/tools/component_gallery.cpp`, `desktop/app/main_window.*`, `desktop/app/query_workspace.*`, `desktop/app/navigator_controller.*`, `desktop/app/appearance_controller.*`, `desktop/widgets/`, models/delegates, resources, `desktop/bridge/`, Rust metadata/core/storage where required, `tests/desktop/`, relevant Rust/integration fixtures, `CMakeLists.txt`, and active `docs/design/` evidence.

Keep Qt presentation in C++ and driver/storage logic in Rust. Standard widgets, custom styles/delegates, and composition remain implementation choices constrained by fidelity and maintainability. Preserve stable action IDs, accessible names/roles, and test-facing object names where their feature remains; update tests deliberately for replaced surfaces.

All database, metadata, credential, persistence, and export work remains asynchronous and bounded. Retaining both SQL and object results must respect aggregate memory/cache limits and ownership of leases/handles. Inspect the core's actual connection/query concurrency before implementing the second result owner; do not bypass transaction restrictions with an undisclosed connection or silently release the SQL result. Ordinary page eviction may follow existing bounded-cache behavior, with explicit loading/error presentation.

No new observability service is required. Provide reproducible captures, a surface/state coverage matrix, mapping/exception records, test results, and existing inline diagnostics. Keep fixture values, simulated failures, and prototype-only captions out of production UI.

## Risks and explicit deferrals

- Qt/browser rasterization differences and native shell geometry require controlled comparison. No claim of literal cross-platform bitmap equality is authorized.
- Detailed object metadata may require bridge/driver additions. SQLite/PostgreSQL capabilities differ; unsupported information must be explicit rather than invented.
- Separate bounded object and SQL results introduce lifetime, memory, and transaction coordination risk. AC5/AC6/AC12 are required, not optional styling checks.
- Modal conversion changes focus and close timing; verify real native input as well as deterministic tests.
- Reference states absent from the prototype are delegated design extensions to review in checkpoint 1. Exact missing-state token values, internal class organization, capture tooling, and dialect-specific metadata mapping are implementation decisions, not unanswered product choices.
- Row editing/Changes, independent SQL-tab results, and dedicated Windows/Linux visual acceptance remain deferred. No other product decision was left open after the user's approval of all scenes.

## Fresh-session handoff

Read this entire spec, inspect the current workspace and applicable repository instructions, then invoke `$implement` with this spec path. Preserve the accepted behavior and the two concrete visual review checkpoints. Do not treat the temporary storyboard or historical screenshots as implementation evidence.

```text
Use $implement with docs/specs/2026-09-13-mvp-ui-flows.md.
```
