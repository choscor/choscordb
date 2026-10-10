# Compact design system

The reference for contributors to the Qt frontend. `desktop/design_system/` is
the only source of visual tokens and shared components; screens in
`desktop/app/` and `desktop/widgets/` use them and define no colors, sizes,
radii, font sizes or stylesheet strings of their own. The decisions behind it
are recorded in `docs/specs/2026-10-10-compact-design-system.md`.

Visual tokens are frontend-owned in C++. They never cross the CXX bridge; Rust
owns behavior, policy, limits and persisted encodings.

## Where tokens live

| Foundation | File | Contents |
| --- | --- | --- |
| Colors | `colors/colors.h` | `design::Colors`, `resolveColors`, `colorTokens`, `contrastRatio`, forced-contrast resolution |
| Sizes | `metrics/metrics.h` | `Dimension`, `Spacing`, `Radius`, `Elevation`, `Motion`, and `LayoutMetrics` for window defaults and initial pane sizes |
| Typography | `fonts/fonts.h` | `TypographyRole`, `typographySpec`, `resolveTypography` |
| Stylesheets | `style/stylesheet.cpp` | The ordered shared cascade and the one `@token` expansion |
| Catalog | `tokens/tokens.cpp` | The gallery's token table |

QSS files refer to tokens by kebab-case name (`@surface-raised`,
`@control-height`, `@radius-md`, `@font-dense`). They contain no positional
`%N` placeholders and no literal `font-size` or `border-radius` values;
`scripts/ci/qss_policy.py` enforces this and reports a selector/property pair
defined in two files of the shared cascade.

## Colors

One name per value; aliases are not allowed. Phase 2 values:

| Token | Use | Light | Dark |
| --- | --- | --- | --- |
| `bg` | Window canvas | #f6f7f8 | #171d20 |
| `surface` | Panels, editors, tables, fields, dialogs, popovers | #ffffff | #20272b |
| `surface-raised` | Tab bars, headers, footers, hover, kbd, neutral badges | #eef1f2 | #283135 |
| `sidebar` | Navigator surface | #fafbfb | #1c2428 |
| `fg` | Primary text | #222b32 | #e0e8e8 |
| `fg-muted` | Help, metadata, inactive tabs, placeholders | #5f6b72 | #93a2a8 |
| `fg-disabled` | Disabled text (40% `fg` over `surface`) | computed | computed |
| `border` | Hairlines, grid, field borders | #e3e7ea | #343e43 |
| `primary` | Default buttons, checked controls, links | #c2410c | #ff8a18 |
| `primary-hover` | Primary hover | #ad3a0b | #ff9b3d |
| `primary-pressed` | Primary pressed | #9a330a | #f07800 |
| `primary-fg` | Text and glyphs on primary | #ffffff | #1f1206 |
| `selection` | Selected rows, checked segments, text selection | #fde4d3 | #4a2c14 |
| `ring` | Focus ring, focused field border | = `primary` | = `primary` |
| `success` / `success-surface` | Success status | #2b7a4b / #e7f3eb | #6cc08b / #213a2b |
| `warning` / `warning-surface` | Warning status | #7d5a00 / #fff4d1 | #e8c35a / #3d3418 |
| `danger` / `danger-surface` | Errors, destructive text, invalid border | #b42336 / #fceaec | #f2878f / #4a2228 |
| `backdrop` | Modal dim (3px blur) | rgba(25,44,54,0.31) | same |
| `switch-track` | Switch off track | #d8dfe0 | #465359 |
| `code-keyword` | SQL keywords, JSON keys | #885da7 | #a984c8 |
| `code-string` | SQL/JSON strings | = `primary` | = `primary` |
| `code-number` | SQL/JSON numbers | #2f6aa3 | #7fa8d6 |
| `code-comment` | SQL comments, JSON literals | #68737a | #9ca6a7 |

- The brand is orange. Green is reserved for `success`; no other token falls in
  the 90°–170° hue range.
- Every text pair is at least 4.5:1 (WCAG 2) in both themes: `fg` on `bg`,
  `surface`, `surface-raised` and `selection`; `fg-muted` on `bg`, `surface`
  and `surface-raised`; `primary-fg` on each primary state; `primary` on
  `surface`; each status color on its own surface and on `surface`; `code-*` on
  `surface`. `ring` is at least 3:1 on `bg` and `surface`.
- Computed, not tokens: the switch thumb (`surface` in Light, white in Dark) and
  destructive fills (`danger` at 10% Light / 20% Dark alpha, hover adds 10%).

## Sizes

`Dimension` is the only size source, in logical pixels on a 4px grid.

| Dimension | Value | Used for |
| --- | --- | --- |
| `ControlExtraSmall` / `ControlSmall` / `Control` | 24 / 28 / 32 | Buttons, fields, selects, spin boxes; icon-only buttons are square |
| `Row` | 28 | Result and object grids, navigator, lists, history, saved connections |
| `Header` | 28 | Table and grid headers |
| `Tab` | 32 | Pane and document tabs |
| `DocumentTabWidth` | 124 | Document tab width |
| `Toolbar` | 32 | Workspace toolbar and dock titles |
| `QuickSearchRow` | 44 | Quick-search result rows |
| `IconSmall` / `Icon` | 12 / 16 | Icons |
| `Checkbox` | 16 | Checkbox and radio indicators |
| `SwitchWidth` × `SwitchHeight`, `SwitchThumb` | 32 × 18, 12 | Switch |
| `Progress` / `Scrollbar` | 4 / 10 | Progress bars, scrollbars |
| `ModalWidth`, `QuickSearchWidth`, `SheetWidth`, `CompletionPopupWidth`, `TableColumn` | 700, 640, 480, 420, 160 | Layout widths |

- **Spacing:** `Half` 2, `One` 4, `OneHalf` 6, `Two` 8, `Three` 12, `Four` 16,
  `Six` 24, `Eight` 32.
- **Radius:** `Small` 4 (badges, kbd, menu items, toast dismiss, progress),
  `Medium` 6 (controls, rows, quick-search rows, toasts, tooltips), `Large` 10
  (menus, popovers, dialogs, sheets). Tables, trees and tab bars are square.
- **Elevation:** `Popover` and `Dialog`.
- **Motion:** 150ms interaction, 100ms popup, `cubic-bezier(0.4, 0, 0.2, 1)`,
  0 under reduced motion.

## Typography

Seven roles on the platform UI and fixed fonts:

| Role | Spec | Used for |
| --- | --- | --- |
| `Caption` | 10/14, 600, +1.3px | Section captions (uppercase in content), badges, kbd |
| `Small` | 11/16, 400 | Secondary detail, field errors, tab labels, quick-search details |
| `Dense` | 12/16, 400; field values 500 | Table cells and headers, tree, list and menu items, tooltips, fields |
| `Body` | 13/18, 400, −0.12px | Default UI text |
| `Title` | 14/20, 600, −0.12px | Headings and dialog titles |
| `Mono` | 13/20, fixed font | Read-only SQL previews (`designRole="codePreview"`) |
| `Metadata` | 12/16, fixed font | SQL excerpts in history rows |

The SQL editor keeps the font the user saved.

## State model

- **Hover:** `surface-raised` for rows, menu items, and ghost and outline
  buttons. Secondary buttons rest on `surface-raised`, so their hover steps to
  `border`; tab bars rest on `surface-raised`, so a hovered tab label turns `fg`.
- **Selected:** `selection` with `fg` text for every tree, list, table,
  quick-search, history and connection row, and for checked buttons and
  segments. Selected rows are not bold.
- **Menu highlight:** `surface-raised` with `fg`; disabled items `fg-disabled`.
- **Primary:** `primary` → `primary-hover` → `primary-pressed`, text always
  `primary-fg`.
- **Focus:** a 2px `ring`, only for keyboard-originated focus; fields switch
  their border to `ring`.
- **Status** (toasts, status lines, history badges): `<status>-surface` fill with
  `<status>` text, and an icon on toasts. Neutral uses `surface-raised` with
  `fg-muted`. Danger toasts stay until dismissed.
- **Driver badge:** the Badge `driver` variant (`surface-raised` fill, `border`
  outline, `fg` text) beside the unmodified driver logo; connection rows draw
  the logo on the same neutral tile.

## Adding to the system

- A new token needs a real second use and a Light/Dark gallery specimen.
- Prefer an existing role, token or component; add a component only when
  presentation or behavior repeats with the same contract.
- Run `python3 scripts/ci/quality.py fast` and `ctest --preset dev`.
