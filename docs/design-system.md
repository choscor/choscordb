# Design system

This page is the reference for contributors to the desktop frontend.
`desktop/design_system/` is the only source of visual tokens and shared components.
Screens in `desktop/app/` and `desktop/widgets/` use these tokens and components.
Screens do not define their own colors, sizes, radii, font sizes or stylesheet strings.

Visual tokens belong to the C++ desktop frontend and never cross the CXX bridge.
The Rust engine owns behavior, policy, limits and persisted encodings.
Read [Architecture](architecture.md) for this boundary.
For the component ownership map, read the [design system README](../desktop/design_system/README.md).
For changes in `desktop/design_system/`, also follow its [CLAUDE.md](../desktop/design_system/CLAUDE.md).

## Where tokens live

All paths in this table are in `desktop/design_system/`.

| Foundation | File | Contents |
| --- | --- | --- |
| Colors | `colors/colors.h` | `Colors`, `resolveColors`, `colorTokens`, `contrastRatio` and the forced-contrast colors. |
| Sizes | `metrics/metrics.h` | `Dimension`, `Spacing`, `Radius`, `Elevation`, `Motion`, `DialogSize` and `LayoutMetrics`. |
| Typography | `fonts/fonts.h` | `TextStyle`, `FontDesign`, `typographySpec` and `resolveTypography`. |
| Icons | `icons.h` | The `Icon` roles, `iconCatalog`, `themedIcon` and `driverIcon`. |
| Stylesheets | `style/stylesheet.cpp` | The ordered shared cascade and the one `@token` expansion. |
| Catalog | `tokens/tokens.cpp` | The token table of the component gallery. |

QSS files refer to tokens by kebab-case name, for example `@surface-raised`, `@radius-md` or `@font-callout`.
They contain no `%N` placeholders and no literal `font-size` or `border-radius` values.
`scripts/ci/qss_policy.py` enforces these rules.
It also reports a selector and property pair that two files of the shared cascade both define.

## Colors

Each value has one name.
Aliases are not allowed.

| Token | Use | Light | Dark |
| --- | --- | --- | --- |
| `bg` | Window canvas | `#f6f7f8` | `#171d20` |
| `surface` | Panels, editors, editor toolbar, tables, fields, dialogs, popovers | `#ffffff` | `#20272b` |
| `surface-raised` | Tab bars, navigator, non-table headers, footers, hover, kbd, neutral badges | `#eef1f2` | `#283135` |
| `fg` | Primary text | `#222b32` | `#e0e8e8` |
| `fg-muted` | Help, metadata, inactive tabs, placeholders | `#5f6b72` | `#93a2a8` |
| `fg-disabled` | Disabled text: 40% `fg` over `surface` | Computed | Computed |
| `border` | Hairlines, grid, field borders | `#e3e7ea` | `#343e43` |
| `primary` | Default buttons, checked controls, links | `#c2410c` | `#ff8a18` |
| `primary-hover` | Primary hover | `#ad3a0b` | `#ff9b3d` |
| `primary-pressed` | Primary pressed | `#9a330a` | `#f07800` |
| `primary-fg` | Text and glyphs on primary | `#ffffff` | `#1f1206` |
| `selection` | Selected rows, checked segments, text selection | `#fde4d3` | `#4a2c14` |
| `sidebar-hover` | Hovered navigator rows | `#eaedef` | `#2c3539` |
| `sidebar-selection` | Selected navigator rows | `#d9dfe2` | `#3c484f` |
| `ring` | Focus ring, focused field border | Same as `primary` | Same as `primary` |
| `success` / `success-surface` | Success status | `#2b7a4b` / `#e7f3eb` | `#6cc08b` / `#213a2b` |
| `warning` / `warning-surface` | Warning status | `#7d5a00` / `#fff4d1` | `#e8c35a` / `#3d3418` |
| `danger` / `danger-surface` | Errors, destructive text, invalid border | `#b42336` / `#fceaec` | `#f2878f` / `#4a2228` |
| `backdrop` | Modal dim with a 3px blur | `rgba(25,44,54,80/255)` | Same as Light |
| `switch-track` | Switch track in the off state | `#d8dfe0` | `#465359` |
| `code-keyword` | SQL keywords, JSON keys | `#885da7` | `#a984c8` |
| `code-string` | SQL and JSON strings | Same as `primary` | Same as `primary` |
| `code-number` | SQL and JSON numbers | `#2f6aa3` | `#7fa8d6` |
| `code-comment` | SQL comments, JSON literals | `#68737a` | `#9ca6a7` |

- The brand color is orange.
  Green is only for `success`.
  No other token has a hue from 90° to 170°.
- Each text pair has a contrast of 4.5:1 or more (WCAG 2) in the two themes.
- `fg` has this contrast on `bg`, `surface`, `surface-raised`, `selection` and `sidebar-selection`.
- `fg-muted` has this contrast on `bg`, `surface`, `surface-raised` and `sidebar-hover`.
- On `sidebar-selection`, `fg-muted` has 3:1 or more, and muted detail text changes to `fg`.
- `primary-fg` has 4.5:1 or more on each primary state.
- Each status color has 4.5:1 or more on its own surface and on `surface`.
- Each `code-*` color has 4.5:1 or more on `surface`.
- `primary` and `ring` have 3:1 or more on `bg` and `surface`.

Some colors are computed and are not tokens:

- The switch thumb is `surface` in Light and white in Dark.
- Destructive fills are `danger` at 10% alpha in Light and 20% in Dark.
  Hover adds 10%.

## Sizes

`Dimension` is the only source of sizes.
The values are logical pixels on a 4px grid.

| Dimension | Value | Use |
| --- | --- | --- |
| `ControlExtraSmall` / `ControlSmall` / `Control` | 24 / 28 / 32 | Buttons, fields, selects and spin boxes. Icon-only buttons are square. |
| `Row` | 28 | Result and object grids, navigator, lists, history, connection profiles |
| `Header` | 28 | Table and grid headers |
| `Tab` | 32 | Pane and document tabs |
| `DocumentTabWidth` | 140 | Document tab width |
| `Toolbar` | 32 | Workspace toolbar and dock titles |
| `QuickSearchRow` | 44 | Quick search result rows |
| `IconSmall` / `Icon` | 12 / 16 | Icons |
| `Checkbox` | 16 | Checkbox and radio indicators |
| `Badge` | 20 | Badges |
| `SwitchWidth` × `SwitchHeight`, `SwitchThumb` | 32 × 18, 12 | Switch |
| `Progress` / `Scrollbar` | 4 / 10 | Progress bars and scrollbars |
| `ToastProgress` | 82 | Toast progress bar width |
| `ModalWidth` / `QuickSearchWidth` / `SheetWidth` / `CompletionPopupWidth` / `TableColumn` | 700 / 640 / 480 / 420 / 160 | Layout widths |

- **Spacing:** `Half` 2, `One` 4, `OneHalf` 6, `Two` 8, `Three` 12, `Four` 16, `Six` 24, `Eight` 32.
- **Radius:** `Small` 4, `Medium` 6 and `Large` 10.
- `Small` is for badges, kbd, menu items and progress bars.
- `Medium` is for controls, rows, quick search rows, toasts and tooltips.
- `Large` is for menus, popovers, dialogs and sheets.
- **Elevation:** `Popover` and `Dialog`.
- **Motion:** 150 ms for interactions and 100 ms for popups, with `cubic-bezier(0.4, 0, 0.2, 1)`.
  Reduced motion sets the duration to 0.

Tables, trees and document tab bars have square corners.
The current document tab rounds its top corners at `Medium` and has no top rule.
Pane tabs are a segmented control.
The track is `surface-raised` at `Large`, and it has the size of its tabs.
The selected tab is a `surface` segment at `Medium`, with an inset of `Half`.

`DialogSize` gives the initial size of each dialog type.
`LayoutMetrics` gives window defaults, minimum sizes and initial pane sizes.
These values are layout constants and are not on the design scale.

## Typography

ChoscorDB uses eight text styles.
Their names come from the Apple macOS [text styles](https://developer.apple.com/design/human-interface-guidelines/typography).
All styles use the bundled Geist family in `desktop/resources/fonts/`, under the OFL 1.1 license.
Thus each platform shows the same typeface.

The sizes follow the Geist scale: 14px body text, 13px dense UI text and a 12px minimum.
Below 12px, Geist is hard to read on low-DPI screens.
All sizes are logical pixels.
Titles and headlines use weight 600.
Fonts prefer vertical-only hinting.
This keeps small text sharp on Windows and Linux and does not change the Geist spacing.

| `TextStyle` | Size/line height, weight | Use |
| --- | --- | --- |
| `LargeTitle` | 28/36, 600 | Reserved for larger titles |
| `Title1` | 22/28, 600 | Reserved for larger titles |
| `Title2` | 18/24, 600 | Reserved for larger titles |
| `Title3` | 16/24, 600 | Reserved for larger titles |
| `Headline` | 14/20, 600 | Headings and dialog titles (`@font-headline`) |
| `Body` | 14/20, 400 | Default UI text |
| `Callout` | 13/18, 400; field values 500 | Table cells and headers, tree, list and menu items, tooltips, fields, tab labels, secondary detail, field errors (`@font-callout`) |
| `Caption` | 12/16, 500 | Badges and kbd (`@font-caption`) |

Navigator section labels use `Callout` at weight 500 in `fg-muted`.

`FontDesign::Monospaced` sets a style in Geist Mono, as the SwiftUI `.monospaced()` modifier does.
Read-only SQL previews use `Body` with `designRole="codePreview"`.
SQL excerpts in history rows use `Callout`.
QSS reads the size and the weight of each style as `@font-<style>` and `@font-<style>-weight`.

The SQL editor uses the font that the user saved in the preferences.

## Icons

- Each UI icon is an unmodified upstream Lucide 1.61.0 file.
- `desktop/resources/icons/SOURCE-LUCIDE.json` pins the Lucide commit and the SHA-256 of each file.
- ChoscorDB renders Lucide icons at the Lucide stroke width of 2.
- `themedIcon` changes `currentColor` to the theme color of the role.
- There is no icon stroke token.

The app mark and the database logos are the only adapted icons.
`SOURCE-LUCIDE.json` records the app mark in its `adaptations` field.
`SOURCE-DATABASE-LOGOS.json` records the PostgreSQL, SQLite and MySQL logos.
Each adapted icon is an SVG with a square `viewBox` and no `width` or `height` attribute.
These icons keep their brand colors.

Icons render only at `Dimension::IconSmall` (12) or `Dimension::Icon` (16).
`scripts/ci/ui_policy.py` enforces the icon rules:

- Each icon file must be an SVG with a provenance record.
- Each icon file must match the SHA-256 in its record.
- Each adapted icon must have a square `viewBox` and no `width` or `height`.
- Each record must have a file.
- Each icon in the Qt resource file must have a provenance record.
- Icon sizes in `themedIcon`, `setIconSize` and `pixmap` must be icon-size tokens.

These rules accept no `ui-ok` marker.
Map object kinds to icons with `desktop/app/object_kind_icon.h`.
Use `design::Icon` roles in place of Qt standard icons, except in native macOS menus.

## State model

- **Hover:** Rows, menu items, ghost buttons and outline buttons use `surface-raised`.
  Secondary buttons rest on `surface-raised`, so their hover changes to `border`.
  Tab bars rest on `surface-raised`, so a hovered tab label changes to `fg`.
- **Selected:** Selected rows, checked buttons and checked segments use `selection` with `fg` text.
  This applies to tree, list, table, quick search, history and connection profile rows.
  Selected rows are not bold.
- **Navigator:** The navigator rests on `surface-raised`, as the tab bars do.
  Its rows change to `sidebar-hover` and `sidebar-selection`.
  Its tabs change to `surface`, as a selected tab does.
- **Menu highlight:** `surface-raised` with `fg`.
  Disabled items use `fg-disabled`.
- **Primary:** `primary`, then `primary-hover`, then `primary-pressed`.
  The text is always `primary-fg`.
- **Focus:** A 2px `ring` shows only for focus from the keyboard.
  Fields change their border to `ring`.
- **Status:** Toasts, status lines and history badges use a `<status>-surface` fill with `<status>` text.
  Toasts also show a status icon.
  The neutral status uses `surface-raised` with `fg-muted`.
  Danger toasts stay until the user closes them.
- **Driver badge:** The Badge `driver` variant has a `surface-raised` fill, a `border` outline and `fg` text.
  It shows next to the unmodified driver logo.
  Connection profile rows show the logo on the same neutral tile.

## Screen rules

- Use `TextStyle::Headline` for dialog titles.
- Use `design::DialogSections` for the header, body and footer of a dialog.
- Use `design::Button` for dialog buttons, with the Outline variant for Cancel and Close.
- Do not use stock `QDialogButtonBox` buttons.
- Use `FieldValidation` or `designRole="fieldError"` for errors.
- Use the description role for help text.
- Use `design::popupContextMenu` or `execContextMenu` for menus.
- Use `ConfirmationDialog` for message boxes.
- Use spacing, radius, metric and icon-size tokens in every argument position, including paint geometry and column widths.
- Set only values that the design system styles on `designRole`, `variant`, `state` and `designSurface`.

`scripts/ci/ui_policy.py` and `scripts/ci/ui_consistency.py` check these rules.
Read [Continuous integration and quality gates](ci.md) for these gates.

## Add to the system

- A new token needs a real second use and a Light and Dark gallery specimen.
- Use an existing role, token or component first.
- Add a component only when presentation or behavior repeats with the same contract.
- Run `python3 scripts/ci/quality.py fast` and `ctest --preset dev`.
