# Shadcn Qt design system and preview window

- Status: Confirmed implementation specification
- Date: 2026-09-12
- Source context: User-requested Qt customization, refined and confirmed through `$brainstorm`. The user confirmed the complete scope before this document was written. This session produces the specification only.

## Outcome and conversation decisions

Make ChoscorDB's application-owned UI look like default shadcn/ui, using native Qt Widgets. Developers and analysts retain the existing database workflows; developers gain a comprehensive native preview window for inspecting and fixing the shared design system.

The user explicitly requires bottom-up implementation: reference capture, tokens/fonts/icons, components and variants, gallery review, layouts, then screens. “100%” means matching the pinned reference's colors, dimensions, typography, icons, and applicable interaction states, with documented font-rasterization, native-shell, and accessibility exceptions. It does not require identical pixels across different rendering engines and operating systems.

This specification supersedes conflicting visual decisions in `docs/specs/2026-09-12-modern-ui-system.md`, including cobalt branding, platform UI fonts, accent customization, density modes, and approximate visual acceptance. Preserve that document's nonconflicting behavior, accessibility, persistence, and performance contracts. The new reference supersedes conflicting presentation in `docs/design/workspace.html` and `docs/design/README.md`; update those artifacts during implementation.

## Current workspace

- C++23, Qt 6.8+ Widgets, and QScintilla own the desktop; the Rust engine remains independent of Qt. See `CMakeLists.txt` and `README.md`.
- `desktop/design_system/theme.h` and `theme.cpp` provide typed colors/metrics, palette resolution, fonts, and a broad application stylesheet. They currently expose compact/comfortable density and preset/custom accents.
- `desktop/design_system/theme_manager.*` apply appearance; `desktop/app/appearance_controller.*` handle preview, persistence, layout, and system accessibility policy.
- `desktop/tools/component_gallery.cpp` is a small native gallery containing selected controls and a sample table. Expand or replace this gallery rather than maintaining a separate imitation of production widgets.
- `desktop/widgets/preferences_dialog.cpp` exposes theme, density, accent, and editor settings. `desktop/widgets/dialog_shell.*` provide shared dialog structure.
- Main composition lives in `desktop/app/main_window.*` and `query_workspace.*`; existing components live in `desktop/widgets/` and model/view behavior in `desktop/models/`.
- Licensed Lucide resources already exist in `desktop/resources/icons/`.
- Existing design-system, appearance, modern-UI, editor, model, and workflow tests live in `tests/desktop/`. Capture instructions are in `docs/design/modern-ui-evidence.md`; quality and performance contracts are in `docs/CI.md` and `docs/performance/measurement-contract.md`.

## Reference and foundation requirements

Use the default official shadcn appearance, not a bespoke branded approximation. Starting references inspected during discovery:

- [Theming](https://ui.shadcn.com/docs/theming)
- [Components](https://ui.shadcn.com/docs/components)
- [Default button documentation](https://ui.shadcn.com/docs/components/base/button)

Before implementing tokens, resolve the official default specimen and record its upstream source revision, style/preset identity, theme values, font assets, component source paths, and reference captures. Use one consistent reference across components; do not mix styles or silently follow subsequent upstream changes. The exact revision and numeric values are a bounded implementation investigation, not permission to redesign the agreed appearance. If the official defaults are ambiguous, document the selected default and its evidence before coding.

Create typed shared definitions for semantic colors, typography, spacing, dimensions, radii, borders, shadows/elevation, focus treatment, motion, and icons. Preserve shadcn semantic surface/foreground distinctions, including background, card, popover, primary, secondary, muted, accent, destructive, border, input, ring, and sidebar roles where applicable. Add documented database/status roles where the reference has no equivalent. Convert web measurements and colors deliberately into Qt logical units and colors; record the mapping. Do not retain the previous cobalt palette or density metrics by accident.

Bundle the licensed reference UI font and required notices, with Unicode fallback. Preserve the user's SQL editor font family and size settings. Use the reference icon treatment and licensed scalable assets. Keep presentation constants in token/component definitions; application screens compose those definitions rather than introducing local visual overrides.

Retain only System, Light, and Dark as user-facing appearance choices. System follows OS changes live. Remove density, accent preset, and custom-color controls and their rendering effects. Components may still have explicit reference sizes; these are component variants, not a user density preference.

## Component coverage and behavior

Inventory every application-owned control and surface before completing the component layer. Every inventory item must map to a reusable component, composition, or explicit native exception. “Everything” means everything used by ChoscorDB, not a port of unrelated shadcn catalog components.

Coverage includes:

- Buttons, tool/icon buttons, links, button groups, toggles, and their applicable variants/sizes. Buttons include default, secondary, outline, ghost, destructive, and link; include the pinned reference's text and icon size variants.
- Labels, descriptions, headings, text, keyboard hints, icons, badges, and status indicators.
- Text/password/numeric inputs, text areas, selects/comboboxes, checkboxes and other used choice controls, editor preference controls, shortcut entry, field groups, forms, validation, and help/error text.
- Tabs, toolbars, navigation/sidebar/tree rows, scroll areas, scrollbars, splitters, separators, headers, tables, lists, and pagination.
- Dialogs/modals, existing nonmodal windows, popovers, completion popups, dropdown/context menus, tooltips, confirmations, and toast feedback.
- Empty, loading, progress, success, warning, error, cancellation, and disconnected presentations.
- Database-specific compositions: connection rows, SQL editor chrome/search/completion, query and transaction controls, result headers/cells, NULL versus empty values, large-value placeholders, paging, messages, history, and execution summaries.

For each component, define applicable variants, sizes, and states, including normal, hover, pressed, focus-visible, disabled, selected/checked/mixed, read-only, invalid, and loading where meaningful. Include icon/text combinations, long labels, Unicode, and constrained widths. Do not create meaningless state combinations simply to fill a matrix.

Use standard Qt controls where they can match the reference, with centralized styling and custom widgets, styles, or delegates where necessary. Preserve public model/controller behavior, shortcuts, accessible names/roles, and test-facing object names where the associated feature remains. Do not choose a global stylesheet-only shortcut that leaves component variants or native-looking subcontrols unfinished.

## Dedicated native design-system preview

Provide a standalone developer executable and a development-only app menu entry that opens the preview. It must run without a database connection, credentials, or saved user profile and must not alter production preferences. Exclude the executable and development entry point from release packages.

Use the same production tokens and components. Provide searchable navigation and these sections:

| Section | Required content |
| --- | --- |
| Tokens | Semantic swatches and values, spacing, sizes, radii, borders, elevation, and motion definitions |
| Typography | Families, sizes, weights, line heights, and Unicode fallback specimens |
| Icons | Names, supported sizes, alignment, and light/dark rendering |
| Components | Every inventoried component, variant, size, and applicable state |
| Compositions | Forms, modal/nonmodal content, menus, toolbars, and feedback states |
| Database UI | Navigator rows, editor chrome, results, paging, and query status using clearly synthetic fixtures |

Provide side-by-side Light/Dark comparisons with independent rendering so one specimen does not change the other's theme. Support interactive controls as well as deterministic state specimens. State forcing for specimens must exercise production state rendering without modifying production behavior.

Expose copyable token names/values and source locations associated with definitions/components. Source paths must remain accurate after refactors. The preview is read-only with respect to design definitions: developers edit shared source, rebuild, and inspect. No live token editing, persistence, or code generation is required.

Provide reproducible screenshot export for the selected specimen or comparison. Document the capture environment, logical size, scale, font, theme, and deterministic fixture/state setup. Capture failures must be visible and must not claim success. Interactive modal/menu examples must open their actual Qt surfaces so focus, dismissal, and placement can be inspected.

## Application flows and edge cases

- Rebuild spacing, grouping, toolbars, forms, and dialogs while retaining the navigator–editor–results information architecture and current database workflows. Keep the established default/minimum window sizes unless measured component needs require a documented adjustment and user review.
- Application modals use shadcn-style centered panels and a dimmed parent. Contain keyboard focus and restore it when closed. Preserve destructive-confirmation cancellation rules; backdrop/Escape handling must never dispatch an affirmative action. Existing nonmodal windows remain nonmodal with matching styled content.
- Retain native title bars, OS menu bars, and system file pickers. Style application-owned context/dropdown menus and popup content. Native exceptions do not permit standard-looking app controls to escape coverage.
- Theme changes update all open windows, menus, editor presentation, and popups without resetting documents, selection, query state, or controllers.
- Preserve live theme preview, Apply persistence, Cancel restoration, and inline retryable save failures. System-mode cancellation resolves against the current OS appearance.
- Forced contrast, reduced motion, keyboard access, screen-reader semantics, Unicode fallback, and Qt/OS DPI scaling take precedence over literal visual matching. Record these exceptions. Existing contrast/accessibility contracts remain applicable; document any conflict with the default specimen rather than silently weakening accessibility.
- Keep errors adjacent to affected fields/components and detailed diagnostics in their existing surfaces. Preserve honest loading, NULL versus empty distinction, bounded data rendering, and cancellation behavior. Gallery fixtures must never trigger database operations.

## Migration and compatibility

Preserve saved System/Light/Dark selection and valid saved geometry, dock visibility, and splitter layout. Ignore obsolete density/accent values for rendering. Read legacy records compatibly; choose the smallest safe persistence change after inspecting current storage/bridge contracts. Do not erase unrelated settings or reset valid theme/layout simply because obsolete fields exist.

Preserve corruption/unsupported-record warnings and explicit retry/reset behavior. Do not silently overwrite corrupt preferences. Keep editor settings, connection profiles, credentials, query settings, history, and recovery data unchanged. No new database capabilities, storage architecture, webview, or Qt Quick rewrite are in scope.

Maintain Windows, macOS, and Linux support and the existing performance contract. Bundled font/icon assets must include license notices and work offline. Existing app identity assets need not be redesigned; do not retain conflicting branded colors in shared control styling.

## Ordered delivery and review gate

1. Inventory current surfaces and pin the official default reference, source revision, captures, fonts, and Qt measurement mapping.
2. Implement and preview shared tokens, typography, and icons.
3. Implement every app-used component, its applicable variants/states, and database-specific compositions. Complete the native preview window and automated component checks.
4. Present the concrete working gallery, reference comparisons, coverage matrix, test results, and documented deviations to the user. Obtain the user's gallery review before beginning layout/screen migration. Earlier layers must be complete and reviewable before this checkpoint is requested.
5. After gallery acceptance, rebuild reusable layouts and migrate every application screen, dialog, and popup. Complete preference migration and release integration as needed without bypassing the earlier gate.
6. Update design documentation, capture final evidence, and run relevant workflow, packaging, accessibility, and performance checks.

Foundational plumbing and the development menu integration may change earlier where needed to make the gallery concrete. This is not permission to start production screen redesign ahead of gallery acceptance. Avoid shipping a half-migrated visual system as the final result.

## Observable acceptance and public test seams

| ID | Observable outcome | Agreed test seam |
| --- | --- | --- |
| AC1 | One pinned default shadcn reference defines the implemented appearance; mappings and exceptions are recorded. | Reference manifest/source provenance, token tables, and documented comparison review. |
| AC2 | Light/Dark tokens, typography, icons, and metrics match the selected reference and are shared by production and preview. | Public token/theme APIs, font/resource loading, and rendered token/type/icon specimens. |
| AC3 | Every inventoried app-used component has its documented variants, sizes, and applicable states; no unexplained native styling remains. | Inventory-to-gallery coverage matrix, production widget public properties/signals, Qt input events, and reference screenshots. |
| AC4 | The preview launches independently with no database/profile, is searchable, shows side-by-side themes, and exposes copyable values/source locations. | Standalone process launch with isolated settings, preview navigation and clipboard interactions, visible specimens. |
| AC5 | Gallery examples remain interactive; deterministic exports reproduce the requested states and report export errors. | Actual Qt widget input/focus/popups, screenshot export command or UI, output files and visible failure status. |
| AC6 | User gallery acceptance occurs after automated checks and before production layout/screen migration. | Recorded review decision linked to concrete gallery evidence and layer completion. |
| AC7 | Every application-owned screen/popup adopts the shared components while retaining the navigator–editor–results structure and workflows. | MainWindow and existing end-to-end desktop workflow tests, plus reviewed full-screen and secondary-surface captures. |
| AC8 | Only System/Light/Dark remains in appearance controls; legacy theme/layout survives while accent/density no longer affect rendering. | Preferences UI, public appearance controller/bridge behavior, persisted legacy fixtures, and restart tests. |
| AC9 | Theme preview/apply/cancel, OS changes, and save failures work across all open surfaces without changing documents or query state. | Preferences actions and public theme/system-event seams; existing workspace and appearance tests. |
| AC10 | Modal focus containment/restoration and cancellation work; existing nonmodal flows remain nonmodal. | Keyboard/mouse input at actual dialog boundaries and observable focus/action signals. |
| AC11 | Accessibility overrides, scaling, Unicode, long content, and narrow layouts remain usable. | Qt accessibility interface, keyboard-only workflows, system-policy adapter seams, and native captures/review at supported scales and minimum window size. |
| AC12 | Visual comparisons match colors/geometry/type/icon/state treatment, allowing only recorded exceptions. | Deterministic reference/Qt specimens on a documented capture environment; human review of differences rather than a permissive unexplained whole-image threshold. |
| AC13 | No regressions in database semantics, bounded rendering, editor performance, startup, memory, or shutdown. | Existing desktop/Rust workflow suites and `docs/performance/measurement-contract.md` probes versus the latest valid baseline. Report pre-existing failures separately. |
| AC14 | Release packages include required fonts/icons/notices and exclude developer gallery/menu functionality. | Staged package inventory, release build launch/menu inspection, and resource-loading checks. |

Test behavior at public boundaries, not private implementation details. Reuse existing tests and fixtures; revise tests whose assertions encode removed appearance choices. Build meaningful rendering/state regressions and perform native visual review; structural tests alone do not establish shadcn fidelity. Run repository-required quality gates for affected code using `docs/CI.md`.

## Likely affected areas

`desktop/design_system/`, `desktop/tools/component_gallery.cpp`, `desktop/widgets/`, `desktop/app/main_window.*`, `desktop/app/appearance_controller.*`, `desktop/app/query_workspace.*`, editor presentation, relevant view delegates, `desktop/resources/`, `CMakeLists.txt`, and `tests/desktop/`. Appearance persistence may require compatible changes through `desktop/bridge/` and Rust storage/bridge contracts after inspection. Update `docs/design/` and developer build/capture instructions.

Keep dependency direction intact and Qt presentation in the desktop layer. Final class/file organization and the choice among QSS, Qt styles, subclasses, and delegates are implementation decisions constrained by visual fidelity, accessibility, maintainability, and performance.

## Risks, assumptions, and explicit deferrals

- The exact upstream default revision, numeric token/component values, font package, and screenshot tooling must be pinned at the first delivery step. No custom aesthetic remains to be chosen.
- Qt/browser rasterization differs; compare on a controlled environment and document differences. Do not promise literal cross-platform pixel equality.
- Native popups, dimmed modals, side-by-side theme isolation, complex tables, and QScintilla may require dedicated implementations beyond stylesheets. Preserve Qt semantics and bounded rendering.
- Some database-specific elements have no shadcn equivalent. Compose them from approved tokens/components and expose them for gallery review instead of inventing undocumented screen-only styles.
- No live design editor, whole-catalog port, unrelated feature work, or new UI technology is authorized.
- No further product decisions remain open. The required future gallery review is an explicit implementation checkpoint; it has not yet occurred.

## Fresh-session instruction

Read this entire specification, inspect the current workspace and applicable repository instructions, then invoke `$implement` with this spec path. Treat prior implementation details as observations to verify, not guaranteed current facts. Follow the ordered delivery and stop for the concrete gallery review before production layout/screen migration.

```text
Use $implement with docs/specs/2026-09-12-shadcn-qt-design-system.md.
```
