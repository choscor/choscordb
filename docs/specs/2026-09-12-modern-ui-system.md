# ChoscorDB Modern UI System

- Status: Confirmed implementation specification
- Date: 2026-09-12
- Source context: User-requested whole-desktop UI enhancement, developed through the `$brainstorm` decision process. Telegram Desktop's [UI styling architecture](https://github.com/telegramdesktop/tdesktop/blob/dev/AGENTS.md) is an architectural reference, not a visual or source-code dependency.

## Summary and intended outcome

Create a cohesive, modern UI/UX for ChoscorDB as a compact native database-management tool. Build it bottom-up: foundations, primitives, database-specific components, layouts, then complete screens and flows.

The primary users are developers and data analysts doing sustained SQL work. Optimize for information density, speed, keyboard use, and clear database state while remaining discoverable to less experienced users. Preserve the native desktop shell and all current database behavior.

“Inspired by Telegram Desktop” means centralized, typed, scalable design values and reusable polished components. It does not mean reproducing Telegram's messaging layout, exact palette, theme format, custom window chrome, or UI framework.

## Current state and workspace findings

- The desktop is C++23 with Qt 6.8+ Widgets and QScintilla; the Rust core remains Qt-independent. See `CMakeLists.txt`, `README.md`, and `docs/prd-mvp.md`.
- `desktop/app/main_window.cpp` builds the main window directly, applies one window-level stylesheet, embeds a few presentation colors and dimensions, and composes native widgets in place.
- The existing structure is a native menu bar, left connection navigator, central editor tabs, contextual query toolbar, vertical editor/results split, optional bottom history dock, and status bar. Preserve this information architecture.
- `desktop/app/query_workspace.h` owns one active query/result lifecycle. Results and Messages are shared workspace tabs rather than results retained independently per editor. Preserve that behavior.
- Current app-owned surfaces include the main window, navigator, editor, completion popup, search/replace panel, result table and paging, query messages, history dock, connection profiles, Preferences, query settings, export, large-value detail, confirmations, recovery feedback, and empty/loading/error states.
- `desktop/widgets/sql_editor.cpp` derives some syntax colors from the Qt palette. Live theme changes must update it without mutating document state.
- Editor preferences are versioned and persisted asynchronously through Rust storage. Corruption is reported rather than silently replaced. See `crates/storage/src/preferences.rs` and `desktop/app/editor_preferences.cpp`.
- The design docs currently require system palettes and an older muted-teal specimen. `docs/design/workspace.html` and relevant files under `docs/design/` must be brought into agreement with this spec.
- Layout persistence is described in `docs/design/README.md` but is not present in the inspected desktop code.
- Native Qt tests already exercise widget, accessibility-name, model/view, restart, and query-workspace seams. `docs/performance/measurement-contract.md` defines the existing performance boundary.

## Scope

Redesign every currently implemented application-owned desktop surface:

- main workspace and native-shell integration;
- navigator, connection status, filtering, and contextual actions;
- editor chrome, tabs, SQL syntax presentation, completion, and search/replace;
- query and transaction controls;
- execution status, Results, Messages, paging, export entry point, and large-value detail;
- history dock;
- connection-profile, Preferences, query-settings, export, and confirmation dialogs;
- recovery, empty, loading, disabled, success, warning, cancellation, disconnected, and error states;
- accessible toast region for noncritical transient notices;
- application/window icon and small in-product mark;
- design documentation, developer component gallery, visual evidence, and tests.

Preserve existing workflows, database semantics, shortcuts, accessible names and roles, test-facing object names, persistence formats unrelated to this feature, and public model/controller behavior unless this spec explicitly changes them.

## Non-goals

- New database or SQL capabilities.
- Results retained separately per editor tab.
- Telegram source reuse, libraries, theme compatibility, or theme import/export.
- A custom styling language or code generator.
- User-customizable SQL syntax themes.
- Custom title bars, window chrome, native menus, or file dialogs.
- Web or mobile UI.
- Splash screen, full welcome dashboard, decorative illustrations, or fabricated skeleton data.
- Gradients, glass effects, oversized decorative headings, or permanent cards around editor/table work areas.

## Confirmed design decisions

### Visual language

- Use flat layered surfaces. Tonal background differences and restrained 1 px separators establish hierarchy. Reserve shadows for floating menus, dialogs, and toasts.
- Use a cobalt/azure identity seeded by `#2F7DD3`. This is an input to theme resolution, not permission to use the seed directly where it fails contrast.
- Blue represents selection, keyboard focus, links, and primary actions. Success, warning, danger, and neutral states use independent semantic colors plus text and icons.
- Use moderately rounded geometry: approximately 6 px for controls, 8 px for menus/popovers, and 10 px for dialogs. Tables, docks, editor areas, and major data boundaries remain square.
- Use platform UI fonts and the system monospace font. Retain the existing user-configurable editor family and size.
- Use a 4 px base grid. All presentation dimensions resolve through design metrics rather than being scattered through widget code.
- Store a curated, scalable, monochrome SVG icon set in the repository. Begin from a reviewed Lucide-derived subset, retain required attribution/license material, and adapt only as licensing permits. Use 16 px and 20 px as common optical sizes.
- Use icon and text for primary or unfamiliar actions. Icon-only controls are allowed only for universally understood compact actions and require a tooltip, accessible name, visible focus, and keyboard access.
- Create an original geometric app mark combining a simplified stacked-data form with a subtle “C.” It must remain recognizable at 16 px and in monochrome.

### Theme, accent, and density

- Provide System, Light, and Dark modes. System follows operating-system appearance changes live; explicit Light/Dark modes do not.
- Forced system contrast takes rendering precedence without deleting the user's saved theme or accent. Reduced-motion preferences take precedence over animations.
- Provide a curated accent list plus an advanced custom-color choice. Derive action, hover, pressed, selection, focus, and subtle-fill roles from the accent.
- Reject a custom accent that cannot produce the required readable component states. Show an inline reason, keep the invalid draft visible, and retain the last valid preview.
- Text and essential UI states must meet WCAG-style contrast targets: 4.5:1 for ordinary text and 3:1 for large text, focus indicators, icons, and meaningful component boundaries. Do not rely on color alone.
- Compact is the default density: controls about 32 px high, data rows 28–30 px, and compact spacing derived from the 4 px grid.
- Comfortable is optional: controls about 38–40 px high and rows 34–36 px. Density must change component metrics consistently, not through per-screen exceptions.
- Let Qt/OS DPI scaling operate normally; design metrics must remain coherent under supported scale factors.

### Component architecture

- Implement typed C++ design tokens and a central theme manager. Do not introduce Telegram's `.style` compiler or a new custom generator.
- Use a hybrid Qt strategy: style standard controls centrally, and introduce custom widgets/delegates only for database-specific behavior or states that standard controls cannot express cleanly.
- Separate semantic values from component styles. At minimum, centralize colors, typography roles, spacing, sizes, radii, separators, elevation, animation policy, density, and icons.
- No application presentation color or reusable metric may be hard-coded outside approved theme/component definitions. Data-dependent measurements and platform APIs are exempt when they are not visual constants.
- All open app-owned windows and popups must react to theme/density changes. A theme update must not recreate controllers or mutate application data.
- Use restrained functional motion, normally 120–180 ms, for panel appearance, selection feedback, and state transitions. Loading indicators may animate. Disable nonessential animation when reduced motion is requested.

### Workspace composition

- Retain the native menu bar and existing navigator–editor–results information architecture.
- Give the navigator its own compact header, filter, status, and contextual controls. Essential disclosure and status are always visible. Show only common actions such as Refresh or Disconnect on hover and keyboard focus; retain the complete action set in an accessible context menu.
- Keep connections visible in the navigator and retain a compact active-connection selector in the query bar. Navigator selection must not silently change execution context.
- Keep editor tabs above the editor. Tabs show readable title, dirty marker, optional compact connection indicator, and close affordance on the active/hovered tab. Provide scrolling and a keyboard-accessible overflow list; do not shrink titles to illegibility or wrap tabs.
- Keep Run and Cancel as stable adjacent actions. Run does not morph into Cancel. Cancel enables for queued/running work and presents “Cancelling…” while cancellation is pending.
- Preserve separate Results and Messages tabs. Add a compact execution summary strip with state icon and text, elapsed time, affected or loaded rows, current page, memory status when relevant, and applicable actions.
- Use a restrained table grid: strong header separation, subtle row hover/selection, faint horizontal dividers, numeric alignment, and alternating rows only where they materially improve scanning.
- Persist valid navigator width, editor/results split, history size, dock visibility, theme, density, and window placement. Provide Reset layout.
- Retain the 1280×900 default and 960×640 useful minimum. At narrow sizes, allow the navigator to hide, collapse secondary action labels to accessible icons, and put overflow actions in menus while protecting editor/result minimums.

### Feedback and dialogs

- Put persistent or actionable state next to the affected component. Keep detailed database diagnostics in Messages.
- Add one accessible, non-stacking toast region per main window. A newer noncritical notice replaces the previous one. Toasts never move keyboard focus and honor reduced motion.
- Reserve modal dialogs for destructive or blocking choices. Preserve existing nonmodal behavior where specified for Preferences, profiles, export, and detail windows.
- Build a shared dialog shell with title, optional concise description, inline status area, content, and footer actions. Preferences uses left-side section navigation; short dialogs remain single-page.
- Appearance changes preview live in Preferences. Apply persists atomically; Cancel restores the saved mode/density/accent against the current OS state. A failed Apply retains the draft and preview, reports an inline retryable error, and does not claim persistence.
- Use honest contextual loading: labeled loading children in the navigator, compact spinner/status for empty results, and existing safe content retained during refresh only where current behavior permits. Never render fake result rows.
- The empty workspace provides concise Connect, Open SQL file, and New query actions without illustration or fictional data.

## User-visible flows and failure behavior

### Startup and migration

1. Start rendering immediately with safe System, Compact, and default cobalt/azure values; do not block first paint on storage.
2. Load the separate versioned appearance/layout record asynchronously.
3. If the record is valid, apply it without animation to all existing surfaces. Suppress redundant work when it matches the safe defaults.
4. If no record exists, keep safe defaults. This is the additive migration for existing installations; do not alter editor, query, history, profile, or recovery preferences.
5. If the record is corrupt or unsupported, keep the safe in-memory appearance and show a persistent Preferences warning with Retry and Reset. Do not overwrite corrupt data without explicit Reset or a successful user Apply.
6. Reject saved geometry that is out of bounds, implausibly small/large, or entirely outside available screens; fall back to the useful default while retaining other valid appearance values.

### Appearance editing

1. Opening Preferences shows the persisted values and current resolved preview.
2. Theme, density, preset accent, and valid custom-accent changes preview across every open app-owned surface.
3. A System draft tracks OS appearance live; explicit Light/Dark drafts do not.
4. If forced contrast is active, explain that system colors override previewed colors. Retain the draft for later use and continue to preview density.
5. Reject an inaccessible custom accent inline without replacing the last valid preview.
6. Cancel restores saved choices, resolved against current OS accessibility and appearance. Apply submits one versioned atomic save. Save failure leaves the draft open for retry or cancellation.

### Primary database workflow

1. From an empty workspace, Connect opens the existing profile flow, Open SQL file invokes the native picker, and New query focuses a new editor.
2. Connections and database objects appear in the navigator with explicit loading, connected/disconnected, empty, and retryable error states.
3. The user selects an execution connection explicitly, writes SQL, and invokes Run from the stable query bar or existing shortcut.
4. The execution summary and controls visibly progress through queued, running, cancelling, completed, failed, and disconnected states without color-only communication.
5. Results show the existing typed, paged model; Messages retains selectable warnings and diagnostics. Paging, export, value detail, history, and transaction behavior stay governed by current controllers.
6. Noncritical success/information may use a toast; database errors remain inline and in Messages; destructive decisions remain modal.

### Important edge cases

- Switching theme or density while a query, export, search, recovery, profile test, or settings save is active changes presentation only; it must not cancel, repeat, or detach the operation.
- Theme updates preserve editor bytes, cursor, selection, scroll, undo/redo, modified flag, completion lifecycle, and file association.
- Closing Preferences during a pending save follows existing stale-response protections; a late response must not revive a destroyed dialog or overwrite a newer choice.
- OS appearance changes while explicit Light/Dark is selected do not change resolved theme. System mode changes live.
- A missing or invalid icon must not erase the action's accessible identity; treat missing required resources as a build/test failure.
- Keyboard focus and hover must expose the same navigator actions; hover cannot be the only path.
- High contrast, translation expansion, and platform font differences must not clip essential text or controls.

## Acceptance criteria and public test seams

| ID | Observable acceptance criterion | Agreed public seam |
| --- | --- | --- |
| AC1 | Every application-owned surface resolves presentation through semantic tokens; reusable visual constants are not scattered through screen code. | Public theme/metric/icon APIs, build-time resource checks, targeted source-policy check, and component gallery. |
| AC2 | System, Light, and Dark update all open surfaces without restart. System follows live OS changes; explicit modes do not. | Theme-manager change notifications plus top-level Qt widget/popup tests. |
| AC3 | Accent presets and valid custom colors produce readable action, focus, selection, and subtle-fill roles; inaccessible custom colors cannot be applied. | Public accent-validation/resolution API and gallery contrast assertions. |
| AC4 | Compact and Comfortable consistently update controls, rows, spacing, icons, dialogs, and workspace chrome while remaining usable at 960×640 and supported DPI scales. | Public resolved-metric API, component gallery, main-window geometry tests, and narrow reference captures. |
| AC5 | Live appearance updates preserve editor content, cursor, selection, scroll, undo history, modification state, syntax meaning, and file association. | Existing `SqlEditor` public API and QScintilla-facing Qt tests. |
| AC6 | Current connection, navigation, query, cancellation, transaction, paging, copy, export, detail, history, recovery, search, completion, and shutdown behavior remains unchanged. | Existing Qt widget/model/controller tests and `QueryWorkspace` commands/events. |
| AC7 | Run/Cancel and the execution strip visibly and accessibly represent queued, running, cancelling, completed, failed, and disconnected states. | Existing engine event seam observed through public actions, labels, enabled states, and accessibility properties. |
| AC8 | Every reusable control has all applicable normal, hover, focus, pressed, selected, disabled, loading, empty, success, warning, and error presentations in both themes and densities. | Developer component gallery states and keyboard-driven Qt component tests. |
| AC9 | Keyboard-only use exposes every action; accessible names, roles, state, focus indicators, forced contrast, reduced motion, and non-color status remain correct. | Qt accessibility tree, QAction shortcuts, focus traversal, keyboard interaction tests, and native review. |
| AC10 | Appearance preview, Apply, Cancel, atomic persistence, restart, migration, corruption, retry, reset, stale responses, save failure, and OS changes follow the specified flows. | Separate versioned preference API, controller signals, failure injection, and restart-level tests. |
| AC11 | Window/pane state restores only when valid, handles changed screen topology, and can be reset. | MainWindow public geometry/visibility behavior with synthetic screen/layout state. |
| AC12 | Empty, loading, refresh, completion, success, failure, cancellation, and disconnected states do not fabricate data, hide diagnostics, steal focus, or block unrelated work. | Existing model/view and operation-event boundaries plus gallery and workflow tests. |
| AC13 | Main workspace and representative secondary surfaces have coherent hierarchy in Light/Dark and Compact/Comfortable. | Deterministic 1280×900 reference captures, targeted 960×640/narrow captures, and documented visual review. Captures are not required to be pixel-identical across operating systems. |
| AC14 | Windows, macOS, and Linux retain correct structure, keyboard behavior, accessibility, system-theme response, and native-shell integration. | Cross-platform CI structural tests plus pre-release native review in System/Light/Dark and available high-contrast modes. |
| AC15 | The redesign does not weaken existing startup, physical-footprint, editor-latency, event-dispatch, query-memory, or shutdown targets. | Release probe and measurement process in `docs/performance/measurement-contract.md`, compared after each layer with the latest valid baseline. |
| AC16 | Release packages contain the licensed production icon assets but not the component-gallery executable. | Build/install tests, staged-package inventory, and notices/SBOM checks. |

## Technical constraints and likely affected areas

- Preserve dependency direction: Qt owns rendering/input, Rust storage owns persisted metadata, the bridge carries bounded plain data, and database behavior remains out of UI code.
- Add a separate versioned appearance/layout preference contract rather than extending the editor-preference record. Bound strings, collections, custom color input, and serialized layout data; validate before applying; save atomically.
- Keep the design-system implementation inside the desktop layer, likely as a cohesive new module consumed by existing `desktop/app`, `desktop/widgets`, and `desktop/models`. Select final filenames during implementation after inspecting current conventions.
- Update CMake resource/build wiring for SVG assets and the gallery. The gallery is built only under `BUILD_TESTING` or an explicit development option and is excluded from release installation.
- Extend SQL editor palette handling in `desktop/widgets/sql_editor.cpp` without reconstructing the editor or lexer state.
- Refactor `desktop/app/main_window.cpp` composition behind reusable components/layouts while preserving controller ownership and test object names.
- Update every existing `desktop/widgets/*.cpp` surface to use the shared dialog/component vocabulary rather than independent local styling.
- Continue using scalable Qt resources and platform-native window/menu/file-dialog behavior.
- Keep translations through `tr()`. Layouts must tolerate platform fonts and reasonable translation expansion; no English-only geometry assumptions.
- Retain existing design-state documents where they define behavior, update their visual rules, and replace `docs/design/workspace.html` as the modern canonical specimen.
- Add attribution and license metadata for any third-party-derived icon material; verify GPL compatibility and existing release notice/SBOM workflows.

## Delivery, rollout, and compatibility

Implement as direct incremental replacement without a runtime “new UI” feature flag:

1. Foundations.
2. Primitive controls.
3. Database/data components.
4. Layouts and persistence.
5. Complete surfaces and flows.

Each layer must land in independently revertible commits or PRs. Before proceeding upward, complete that layer's implementation, public-seam tests, gallery coverage, Light/Dark/System and both-density checks, keyboard/accessibility evidence, relevant captures, performance comparison, and design documentation.

Appearance/layout migration is additive and backward-tolerant until the full redesign ships. Do not alter unrelated stored preferences. No deployment, packaging, or publication is implicit in implementation; use the repository's existing release gates when release work is separately authorized.

## Risks and mitigations

- **Qt platform-style variation:** keep the native shell, centralize the app-owned style boundary, test structure cross-platform, and avoid cross-platform pixel-identity requirements.
- **QScintilla live theming:** test document/editor state explicitly at its public boundary before broad screen rollout.
- **Accent accessibility:** centralize derivation/validation and cover every semantic component role rather than checking the seed alone.
- **QSS brittleness:** use the approved hybrid of typed tokens, central styling, delegates, and custom widgets; do not force all behavior through a monolithic stylesheet.
- **Screenshot brittleness:** use captures for hierarchy and state evidence while using public widget/accessibility assertions for behavior.
- **Async preference races:** retain generation/token correlation and destroyed-dialog safeguards used by current controllers.
- **Scope size:** enforce bottom-up layer gates and do not mix new database behavior into the UI work.

## Assumptions and deferred decisions

- The current native Qt Widgets architecture remains authoritative.
- `#2F7DD3` is the initial accent seed; the resolver owns accessible role colors for each theme.
- Existing performance targets remain authoritative rather than being renegotiated for the redesign.
- There are no unresolved product decisions in this spec.
- Deferred work is exactly the Non-goals list; reopening any item requires a separate decision/spec update.

## Fresh-session implementation instruction

In a new session, read this entire spec, inspect the current workspace and applicable repository instructions, then invoke `$implement` with this exact spec path. Implement bottom-up and do not skip a layer's completion gates.

```text
Use $implement with docs/specs/2026-09-12-modern-ui-system.md.
```
