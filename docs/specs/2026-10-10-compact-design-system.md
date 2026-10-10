# Compact ChoscorDB design system: one stylesheet path, own tokens, orange brand
- Status: Ready for implementation
- Date: 2026-10-10
- Source: Design-system review of `desktop/design_system/` (session of 2026-10-09/10) and the user's answers recorded below. Evidence: `desktop/design_system/{colors,metrics,fonts,tokens,style}/`, the component `*.qss` files, `button/button.cpp`, `crates/storage/src/appearance.rs`, `docs/design/shadcn-reference.md`, `CLAUDE.md`.

## Outcome and current state

Beneficiaries: ChoscorDB desktop users, who get a denser, more consistent UI with a single orange brand color; and contributors and agents, who get one small token vocabulary with one stylesheet path and clear ownership rules.

Current state (verified in the workspace):

- **Two global stylesheets style the same selectors with different values.** `style/application_stylesheet.cpp` fills 24 positional `%1…%24` placeholders (in 18 `*.qss` files). `style/control_stylesheet.cpp` replaces named `@token` placeholders (in 30 files) and is appended after it, so whichever rule comes later wins. Conflicts:
  - Button height 33 vs 31, padding 8 vs 12.
  - Field padding 8 vs 9.
  - Menu padding 4 vs 5.
  - Pane tab height 35 vs 32, padding 6 vs 9.
  - Tree item height 33 vs 22.
  - Tooltip: `text` on `elevatedSurface` with a border vs `background` on `foreground` without one.

  `Button` also paints itself in `button.cpp`, a third source.
- **`SemanticColors` (`colors/colors.h`) has 62 fields for about 20 distinct values.** The shadcn names and older aliases both exist: `primary` = `action` = `actionHover` = `actionPressed` = `ring` = `focus` = `success` = `sidebarPrimary` = `sqlString` = `jsonString`; `border` = `input` = `separator` = `sidebarBorder`; `card` = `popover` = `surface`; `secondary` = `muted` = `elevatedSurface`; `accent` = `selection` = `subtleAccent`. These have no uses: `sidebarRing`, `sidebarPrimary`, `cardForeground`, `neutral`.
- **Metrics are duplicated and don't line up.** `DesignMetrics` (`metrics/metrics.h`) repeats `Dimension`/`Spacing`/`Radius` values as fields. Specifically:
  - Result and object grids use `sqlResultRowHeight`/`objectDataRowHeight` = 35, while `Dimension::TableRow` = 29.
  - Fields are 31 while buttons and selects are 33.
  - Heights are odd numbers (25/29/33/37/35), off the 4px grid.
  - `Radius::ThreeExtraLarge`, `FourExtraLarge`, `Elevation::Large`, `connectionHeaderHeight` and `dialogElevation` have no uses, and `Radius::Medium` has none outside its definition.
  - The `*.qss` files type font sizes (10/11/12/14px) and radii (0/2/4/5/6/7/8px) as literals.
- **Typography:** 10 roles across five sizes. `Heading` and `DialogTitle` are identical (14/20/600), and `Ui` and `Base` differ only in line height.
- **Visual flaws:**
  - The Default (primary) button has no hover or pressed change.
  - List hover equals list selection (`accent`); the sidebar list selects with `muted`; menus highlight with solid `primary`.
  - `secondary`/`muted` are pure grey (#f2f2f2, #303030) while every other neutral is blue-slate tinted.
  - The success toast is a solid fill while every other status is tinted.
  - Driver badges keep light colors in Dark, with text contrast of 2.8:1 (SQLite) and 3.3:1 (PostgreSQL).
- **Dead appearance settings:** Rust persists `density` (Compact/Comfortable) and `accent` (presets, default Cobalt, or custom) in `AppearanceLayout` (`crates/storage/src/appearance.rs`, `APPEARANCE_LAYOUT_VERSION = 1`, `deny_unknown_fields`) and passes them through `crates/bridge/src/appearance.rs` → `EngineAdapter` → `ThemeManager`. C++ never applies either: `resolveMetrics(Density, …)` ignores its argument and no code reads the accent.

Desired state: one named-token stylesheet path, one ChoscorDB token vocabulary, a compact 4px-grid scale, an orange brand with green only for success, one state model, no dead tokens or settings, and CLAUDE.md rules that keep it that way.

## Decisions and requirements

### Decision ledger (all settled)

| ID | Decision | Answer (source: user unless noted) |
| --- | --- | --- |
| Q1 | Scope and phasing | Two phases in one spec. Phase 1 consolidates and keeps the rendering pixel-identical; phase 2 is the visual refresh. |
| Q2 | Token naming | ChoscorDB's own short role names. Drop the shadcn and legacy aliases; `docs/design/shadcn-reference.md` becomes historical. |
| Q3 | Size scale | Controls 24 / 28 / 32 (fields = selects = buttons = 32); every data row and header 28; tabs 32. |
| Q4 | Radius scale | 4 / 6 / 10. |
| Q5 | State model | Quiet unified model: hover = `surface-raised`, selected = `selection`, menu highlight = `surface-raised`, primary gets real hover and pressed colors, focus = 2px `ring`, keyboard only. |
| Q6 | Neutrals | Tint `surface-raised` to the slate canvas hue; one neutral ramp. |
| Q7 | Driver badges | One neutral outline badge beside the full-color driver logo. |
| Q8 | Status styling | Always tinted surface + colored text + icon; solid fills only for primary actions. |
| Q9 | Typography | 7 roles (below). |
| Q10 | Dead API | Remove Rust `density` and `accent` with a storage migration; delete `DesignMetrics` duplicates, unused tokens and enum values. |
| Q11 | Forced contrast | Preserved: `resolveForcedContrastColors` keeps mapping the OS palette onto every new token (evidence: `colors.cpp`). |
| Q12 | Outward docs | Repo docs only. Re-syncing the claude.ai design-system artifact is a separate later request. |
| Q13 | Token owner | Visual tokens stay in C++ `desktop/design_system/`. Rust owns behavior, policy, limits, persisted encodings and backend contracts. CLAUDE.md is updated to say so. |
| Q14 | Brand color | Orange primary (light #c2410c, dark #ff8a18, from the app mark). No green except `success`. Every other former-green token (selection, ring, SQL/JSON strings) becomes orange. Warning moves to yellow/ochre, danger to crimson. The user corrected "no green at all" to "success = green". |

Rejected alternatives: one big-bang change (hard to tell regressions from intended changes); keeping or namespacing the shadcn names; moving tokens to Rust or generating C++ from Rust (adds a bridge contract or codegen with no consumer); the exact icon orange #F4511E (white text only 3.5:1); theme-aware driver tints; wiring up Density.

### Token vocabulary (phase 1 introduces names, phase 2 changes values)

`design::Colors` (renamed from `SemanticColors`) holds exactly these fields. Kebab-case names are used in QSS (`@surface-raised`) and the gallery's token catalog.

| Token | Use | Phase 2 Light | Phase 2 Dark | Replaces |
| --- | --- | --- | --- | --- |
| `bg` | Window canvas | #f6f7f8 | #171d20 | background, canvas |
| `surface` | Panels, editors, tables, fields, dialogs, popovers | #ffffff | #20272b | card, popover, surface, field |
| `surface-raised` | Tab bars, headers, footers, hover, kbd, neutral badges | #eef1f2 | #283135 | secondary, muted, elevatedSurface |
| `sidebar` | Navigator surface | #fafbfb | #1c2428 | sidebar |
| `fg` | Primary text | #222b32 | #e0e8e8 | foreground, *Foreground (text), text |
| `fg-muted` | Help, metadata, inactive tabs, placeholders | #5f6b72 | #93a2a8 | mutedForeground, mutedText, neutral |
| `fg-disabled` | Disabled text (40% `fg` over `surface`) | computed | computed | disabled |
| `border` | Hairlines, grid, field borders | #e3e7ea | #343e43 | border, input, separator, sidebarBorder |
| `primary` | Default buttons, checked controls, links | #c2410c | #ff8a18 | primary, action, sidebarPrimary |
| `primary-hover` | Primary hover | #ad3a0b | #ff9b3d | actionHover |
| `primary-pressed` | Primary pressed | #9a330a | #f07800 | actionPressed |
| `primary-fg` | Text and glyphs on primary | #ffffff | #1f1206 | primaryForeground, actionText |
| `selection` | Selected rows, checked segments, text selection background | #fde4d3 | #4a2c14 | accent, selection, subtleAccent, sidebarAccent |
| `ring` | Focus ring, focused field border | #c2410c | #ff8a18 | ring, focus, sidebarRing |
| `success` / `success-surface` | Success status | #2b7a4b / #e7f3eb | #6cc08b / #213a2b | success, successSurface |
| `warning` / `warning-surface` | Warning status | #7d5a00 / #fff4d1 | #e8c35a / #3d3418 | warning, warningSurface |
| `danger` / `danger-surface` | Error text, destructive button text, invalid border | #b42336 / #fceaec | #f2878f / #4a2228 | danger, destructive, dangerSurface |
| `backdrop` | Modal dim (3px blur) | rgba(25,44,54,0.31) | same | backdrop |
| `switch-track` | Switch off track | #d8dfe0 | #465359 | switchTrack |
| `code-keyword` | SQL keywords, JSON keys | #885da7 | #a984c8 | sqlKeyword, jsonKey |
| `code-string` | SQL/JSON strings | = `primary` | = `primary` | sqlString, jsonString |
| `code-number` | SQL/JSON numbers | #2f6aa3 | #7fa8d6 | sqlNumber, jsonNumber (moves off amber so it reads apart from orange strings) |
| `code-comment` | SQL comments, JSON literals | #68737a | #9ca6a7 | sqlComment, jsonLiteral |

Notes:
- The switch thumb is `surface` in Light and #ffffff in Dark. Destructive fills are `danger` at 10% (Light) or 20% (Dark) alpha, and hover adds 10%; these are computed, not tokens.
- Removed without replacement: `sidebarForeground` and the other `*-foreground` duplicates (use `fg`); `sidebarAccentForeground`; the six `sqlite/postgres-badge-*` colors.
- Phase 1 keeps today's values under the new names. Where one new name replaces aliases that today hold different values (`danger` vs `destructive`, `selection` vs today's menu `primary`), phase 1 keeps a private, file-local constant only for as long as needed to stay pixel-identical, and phase 2 removes it.

Contrast was checked in this session (WCAG 2), and every pair is at least 4.5:1 in both themes: `fg` on `bg`/`surface`/`surface-raised`/`selection`; `fg-muted` on `bg`/`surface`/`surface-raised` (lowest 4.83 Light); `primary-fg` on `primary`/`primary-hover`/`primary-pressed`; `primary` on `surface` (5.18 / 6.42); each status color on its own surface and on `surface`; `code-*` on `surface`. `ring` is at least 3:1 on `bg` and `surface`.

**Sizes** (the `Dimension` enum is the only source; values are logical px):

| Token | Value |
| --- | --- |
| `control-xs` / `control-sm` / `control` | 24 / 28 / 32 (buttons, fields, selects, spin boxes; icon-only buttons are square) |
| `row` | 28 (result grid, object grid, navigator, lists, history, saved connections, quick-search compact rows) |
| `header` | 28 |
| `tab` | 32 (pane and document tabs; document tabs stay 124 wide) |
| `toolbar` | 32 |
| `quick-search-row` | 44 |
| `icon-sm` / `icon` | 12 / 16 |
| `checkbox` 16, `switch` 32×18 with a 12px thumb, `progress` 4, `scrollbar` 10 | |
| Layout widths | keep `modal-width` 700, `quick-search-width` 640, `sheet-width` 480, `completion-width` 420, `table-column` 160 |

**Spacing:** 2, 4, 6, 8, 12, 16, 24, 32. Remove `Quarter` (1px) and `TwoHalf` (10px); their single uses move to 2 and 8/12.

**Radius:** `sm` 4 (badges, kbd, menu items, toast dismiss, progress), `md` 6 (controls, rows, quick-search rows, toasts, tooltips), `lg` 10 (menus, popovers, dialogs, sheets). Tables, trees and tab bars stay square.

**Elevation:** `popover` (today's Medium) and `dialog`. Remove `Large`.

**Motion:** unchanged (150ms interaction, 100ms popup, `cubic-bezier(0.4, 0, 0.2, 1)`, 0 under reduced motion).

**Typography:** `TypographyRole` has exactly these roles, using the platform UI/fixed fonts as today:

| Role | Spec | Replaces / used for |
| --- | --- | --- |
| `Caption` | 10/14, 600, +1.3px, uppercase in content | SectionCaption; badges (no uppercase) and kbd |
| `Small` | 11/16, 400 | Small, NavigationDetail, field errors, tab labels, quick-search details |
| `Dense` | 12/16, 400 (500 for field labels via `setWeight`) | Field; table cells and headers, tree, list, menu items, tooltips |
| `Body` | 13/18, 400, −0.12px | Ui, Base |
| `Title` | 14/20, 600, −0.12px | Heading, DialogTitle |
| `Mono` | 13/20 fixed font | Monospace (SQL previews, `designRole="codePreview"`); the SQL editor keeps the user's saved font |
| `Metadata` | 12/16 fixed font | Metadata |

QSS files contain no literal `font-size`; a style that needs a size gets it from a role through the shared stylesheet assembly.

### State model (phase 2)

- Hover: `surface-raised` for rows, menu items, tabs, and ghost/outline/secondary buttons.
- Selected: `selection` + `fg` for every tree, list, table, quick-search and history row and for checked button-group segments. Selected tree rows are not bold.
- Menu highlight: `surface-raised` + `fg`; disabled items `fg-disabled`.
- Primary: `primary` → `primary-hover` → `primary-pressed`, text always `primary-fg`.
- Focus: 2px `ring`, only for keyboard-originated focus (keep the existing `Button` focus-reason logic); fields switch their border to `ring`.
- Status (toasts, status lines, history badges): `<status>-surface` fill + `<status>` text + icon. Neutral uses `surface-raised` + `fg-muted`. Danger toasts stay pinned until dismissed.
- Driver badge: one `Badge` variant `driver` with a `surface-raised` fill, `border` outline and `fg` text, next to the unmodified driver logo (`design::driverIcon`). Connection-row logo tiles use the same neutral tile.

### Phase 1: consolidation with pixel-identical output

1. Merge `application_stylesheet.cpp` into one assembly that uses named `@token` placeholders only. Every `%N` placeholder and every `.arg()` chain goes. Each selector is defined once, with the value that wins today, so the assembled cascade renders identically.
2. Rename `SemanticColors` to `Colors` with the vocabulary above (today's values) and migrate all call sites, including `resolveForcedContrastColors` (Q11), `tokens/tokens.cpp` and the gallery.
3. Delete `DesignMetrics` fields that duplicate `Dimension`/`Spacing`/`Radius`. Callers read the enums; layout-only constants (window defaults and minimums, initial pane sizes, label character limits) stay in one `LayoutMetrics` struct with no duplicates. Grid row heights become `Dimension::Row`; in phase 1 it is temporarily 35 for the grids and the navigator keeps its own entry until phase 2 unifies them at 28.
4. Delete unused tokens and enum values: `Radius::ThreeExtraLarge`, `FourExtraLarge`, `Elevation::Large`, `connectionHeaderHeight`, `dialogElevation`, `sidebarRing`, `sidebarPrimary`, `cardForeground`, `neutral`, and any other value `source_inventory.py` reports as uncalled after the renames.
5. Replace every literal radius, font size and size in the `*.qss` files with tokens or role-derived placeholders, keeping today's values.
6. Remove `Density` and `Accent` end to end:
   - `crates/storage/src/appearance.rs`: remove the fields, enums and `AppearanceChoiceError` variants, and bump `APPEARANCE_LAYOUT_VERSION` to 2.
   - The loader accepts version 1 by dropping `density` and `accent` and continues to reject other unknown fields and versions. The next save writes version 2.
   - Remove the fields from the bridge DTO in `crates/bridge/src/appearance.rs` and the transport tests, from `EngineAdapter` (`engine_adapter.h/.cpp`, `engine_adapter_storage.cpp`), from `ThemeManager::density/setDensity` and from the `resolveMetrics` parameter.
   - Rust tests cover the migration; the C++ tests change only where they referenced the removed API.
7. Update the CLAUDE.md files and docs (see below).

Failure behavior: an appearance row at version 1 with invalid or extra fields other than `density`/`accent` is still `CorruptAppearance`; version ≥3 is still `UnsupportedAppearanceVersion`. No user-visible change in phase 1 other than the dropped (already inert) settings.

### Phase 2: visual refresh

1. Apply the phase 2 color values, the orange brand, green only for `success`, and the code colors.
2. Apply the size, spacing, radius, elevation and typography scales above; delete the phase 1 temporary constants.
3. Apply the state model, status styling and driver badge.
4. Update every gallery specimen in `desktop/tools/preview/preview_window.cpp` and the matching checks in `tests/desktop/preview_test.cpp`, in Light and Dark, as `desktop/design_system/CLAUDE.md` requires.
5. Update the app mark only if needed: it stays as is, and its orange now matches the brand.

### Documentation and instruction updates (end of phase 1, values finalized in phase 2)

- **Root `CLAUDE.md`:**
  - Add a short "Design system" section to "Desktop UI ownership" stating:
    - `desktop/design_system/` is the single, compact source of visual tokens (the vocabulary above) and components.
    - Every screen in `desktop/app/` and `desktop/widgets/` uses its tokens, roles and components, with no local colors, sizes, radii, font sizes or stylesheet strings.
    - There is one stylesheet assembly with named tokens only.
    - Adding a token requires a real second use and a gallery specimen.
    - Aliases (two names for one value) are not allowed.
  - Restate in "Rust and C++ ownership" that Rust is the source of truth for behavior, policy, limits, persisted encodings and backend contracts (typed DTOs over the CXX bridge), while visual tokens are frontend-owned in C++ and never cross the bridge.
  - Replace the `TypographyRole::DialogTitle` mention with `TypographyRole::Title`.
  - Keep the rest of the file intact.
- **`desktop/design_system/CLAUDE.md`:** name the token files, the 7 roles, the 4/6/10 radii and the state model, and forbid `%N` placeholders and literal sizes in QSS.
- **`desktop/design_system/README.md`:** update the foundations section and the ownership/specimen tables (the Badge `driver` variant; Status line and Toast styling).
- **New `docs/design/design-system.md`:** the token tables, scales and state model from this spec, as the reference for contributors.
- **`docs/design/shadcn-reference.md`:** add a top note that it is historical and superseded by `docs/design/design-system.md`.

Non-goals: moving tokens into Rust; changing the SQL editor's user font setting; new components; a Comfortable density; re-syncing the claude.ai design-system artifact (Q12, separate request); changing component behavior, object names, accessibility names, focus order or signals.

## Acceptance criteria and public test seams

| Criterion | Observable seam |
| --- | --- |
| Phase 1 renders identically | Offscreen gallery captures (`desktop/tools/preview/preview_capture.cpp`) of every specimen in Light and Dark before and after phase 1 are pixel-identical, the same method used for the 2026-09-13 extraction recorded in `desktop/design_system/README.md`. |
| One stylesheet path, no positional placeholders | `python3 scripts/ci/qss_policy.py` gains a rule (with a failing fixture in `test_qss_policy.py`) rejecting `%<digit>` placeholders and literal `font-size`/`border-radius` in `*.qss`. `application_stylesheet.cpp` no longer exists or contains no `.arg(` chain. |
| Each selector defined once | A new check in `qss_policy.py` (with a fixture) reports any selector and property pair defined in two `*.qss` files of the shared cascade. |
| Token vocabulary is exactly the specified set | `tests/desktop/design_system_test.cpp` asserts the `designTokens()` color names equal the table above for both themes and that forced contrast maps every token. |
| No dead tokens or duplicate metrics | `python3 scripts/ci/source_inventory.py` passes with no new `EXCEPTIONS`; `DesignMetrics` duplicate fields are gone (header-check target `choscordb-header-check` builds). |
| Density/Accent removed with migration | Rust tests in `crates/storage/tests/appearance.rs`: a v1 row with `density`/`accent` loads, saves as v2 without them; a v1 row with another unknown field is `CorruptAppearance`; v3 is `UnsupportedAppearanceVersion`. `crates/bridge/tests/transport.rs` no longer carries the fields. |
| Phase 2 contrast | A test in `design_system_test.cpp` asserts `contrastRatio` ≥ 4.5 for every text/surface pair listed under the token table and ≥ 3 for `ring` on `bg`/`surface`, in both themes. |
| Orange brand, green only for success | `design_system_test.cpp` asserts `primary`, `ring` and `code-string` are the specified oranges and that no other color token falls in the green hue range (90°–170°) except `success` and `success-surface`. |
| Compact sizes | `design_system_test.cpp` asserts `Dimension` values (control 24/28/32, row 28, header 28, tab 32); `tests/desktop/workspace_results_test.cpp` and `object_data_workspace_test.cpp` assert the grid default section size is `Dimension::Row` (28). |
| Primary hover/pressed | `tests/desktop/control_style_test.cpp` (or `components_test.cpp`) renders a Default `design::Button` at rest, hover and pressed and asserts three different fill colors equal to `primary`, `primary-hover`, `primary-pressed`. |
| Unified selection and hover | `control_style_test.cpp` asserts selected-row and hover fills for tree, list and table equal `selection` and `surface-raised`, and that the menu highlight is `surface-raised`. |
| Status styling | `tests/desktop/preview_test.cpp` checks toast and status-line specimens use `<status>-surface` fills in both themes. |
| Typography has 7 roles | `tests/desktop/typography_test.cpp` asserts the role list and specs above; `ui_consistency.py` passes. |
| Driver badge | `preview_test.cpp` checks the Badge `driver` specimen and the connection-row specimen use neutral tokens and a driver logo in both themes. |
| Gallery coverage | Every specimen is updated in Light and Dark, and `preview_test.cpp` passes. |
| Repository gates | `python3 scripts/ci/quality.py fast` passes (cpp_ownership, ui_policy, qss_policy, ui_consistency, perf_policy, source_inventory and the Python tests); `ctest --preset dev` passes; `cargo test` passes for `crates/storage` and `crates/bridge`. |
| Docs updated | The root `CLAUDE.md`, `desktop/design_system/CLAUDE.md`, `desktop/design_system/README.md`, the new `docs/design/design-system.md` and the `shadcn-reference.md` note contain the content listed above (review in the PR). |

## Constraints and risks

- **QSS precedence:** the merged cascade must preserve today's winner for each selector in phase 1. Diff the assembled stylesheet text and compare gallery captures before deleting anything.
- **Call-site churn:** about 100 color reads and the typography role uses (including 15 `DialogTitle` uses) change. Keep phase 1 to renames and deduplication; land phase 2 as separate commits per area (colors, sizes, states, typography) so captures show intended changes only.
- **Row height 35 → 28** shows about 25% more rows. Verify column-width sampling, the elision and status-line fitting (`StatusLine::fitContent`), and that perf paths still precompute per page (`perf_policy.py`).
- **Platform fonts:** at 12/16 and 13/18 they differ across macOS, Windows and Linux. Native visual checks on all three platforms are required before release, per the release rules in CLAUDE.md. A passing macOS run doesn't establish the other platforms.
- **Storage migration** is one-way: v2 files are unreadable by older builds. That's acceptable because the dropped fields were inert; mention it in the release notes.
- **Forced contrast:** every new token needs a mapping in `resolveForcedContrastColors`; the contrast tests above don't cover OS palettes, so check the forced-contrast gallery manually on Windows.
- **Assumption (from Q6 + Q14):** the neutrals keep their cool blue-slate hue (about 200–210°), which complements the orange. The former green tints (`accent` #ccebdc / #254b38) are removed, not re-tinted.
- **Deferred minor detail:** the exact switch-thumb and destructive-tint alphas can be tuned during phase 2 if the stated contrast floors still hold. This doesn't change scope or criteria.
- **Out of scope, deferred (Q12):** re-syncing the claude.ai artifact "ChoscorDB Design System" after phase 2.
- **Affected paths:**
  - `desktop/design_system/**`
  - `desktop/app/**` and `desktop/widgets/**` (call sites)
  - `desktop/tools/preview/**`
  - `tests/desktop/**`
  - `crates/storage/src/appearance.rs` and `crates/storage/tests/appearance.rs`
  - `crates/bridge/src/appearance.rs` and `crates/bridge/tests/transport.rs`
  - `desktop/bridge/engine_adapter*`
  - `scripts/ci/qss_policy.py` and `scripts/ci/test_qss_policy.py`
  - `cmake/DesktopComponents.cmake`
  - `CLAUDE.md`, `desktop/design_system/{CLAUDE.md,README.md}`, `docs/design/`

Read this whole spec and inspect the current workspace before implementing.
