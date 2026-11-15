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

QSS files refer to tokens by kebab-case name, for example `@kumo-elevated`, `@radius-md` or `@font-callout`.
They contain no `%N` placeholders and no literal `font-size` or `border-radius` values.
`scripts/ci/qss_policy.py` enforces these rules.
It also reports a selector and property pair that two files of the shared cascade both define.

## Colors

The colors are the Cloudflare Kumo tokens at commit `eaa9e173`, from `packages/kumo/src/styles/theme-kumo.css`.
Each token has the Kumo name and the exact Kumo Light and Dark value.
`scripts/design/kumo_colors.py` converts the Kumo `oklch` values to sRGB hex values.
A value with alpha uses the `#rrggbbaa` order.
The `code-*` colors come from the Shiki themes `github-light` (Light) and `vesper` (Dark), as Kumo `Code` uses them.
`colors.cpp` contains only the Kumo tokens that a component uses.
Two Kumo tokens can have the same value. This is the Kumo design.

| Token | Use | Light | Dark |
| --- | --- | --- | --- |
| `kumo-canvas` | Window canvas | `#fbfbfb` | `#030303` |
| `kumo-elevated` | Tab bars, navigator, headers, footers, kbd | `#f8f8f8` | `#060606` |
| `kumo-recessed` | Dialog backdrop at 80% | `#f3f3f3` | `#0b0b0b` |
| `kumo-overlay` | Command palette backdrop and highlight | `#fbfbfb` | `#262626` |
| `kumo-base` | Panels, editors, tables, fields, dialogs, popovers | `#ffffff` | `#0f0f0f` |
| `kumo-control` | Fields, selects and text areas | `#ffffff` | `#171717` |
| `kumo-tint` | Hover and selected rows, hover fills | `#f5f5f5` | `#262626` |
| `kumo-fill` | Pressed fills | `#e5e5e5` | `#262626` |
| `kumo-contrast` | Checked checkbox and radio fill | `#060606` | `#fafafa` |
| `kumo-badge-inverted` | Primary badge fill | `#0a0a0a` | `#ffffff` |
| `kumo-brand` | Primary buttons, checked controls, the focus ring | `#056dff` | `#045ede` |
| `kumo-brand-hover` | Primary hover | `#1447e6` | `#1447e6` |
| `kumo-line` | Field, check box, radio and switch rings | `#0a0a0a1a` | `#333333` |
| `kumo-hairline` | Hairlines, grid, dividers | `#e9e9e9` | `#262626` |
| `kumo-focus` | Outline button hover ring at 25% | `#0b0b0b` | `#e9e9e9` |
| `kumo-shadow-edge` | Switch thumb edge shadow | `#0000001f` | `#ffffff1a` |
| `kumo-shadow-drop` | Switch thumb drop shadow | `#00000014` | `#0000004c` |
| `kumo-arrow-edge` | Tooltip arrow edge | `#0a0a0a1a` | `#00000000` |
| `kumo-arrow-stroke` | Tooltip arrow stroke | `#00000000` | `#333333` |
| `kumo-danger` | Destructive fill | `#fb2c36` | `#e7000b` |
| `kumo-success-tint` | Success status fill | `#dcfce791` | `#0c542b33` |
| `kumo-warning-tint` | Warning status fill | `#ffe89433` | `#5731005e` |
| `kumo-danger-tint` | Error status fill | `#ffe2e26b` | `#9900032b` |
| `text-kumo-default` | Primary text | `#171717` | `#f5f5f5` |
| `text-kumo-strong` | Outline button hover text | `#0a0a0a` | `#fafafa` |
| `text-kumo-inverse` | Checkbox check mark | `#f5f5f5` | `#171717` |
| `text-kumo-subtle` | Help, metadata, inactive tabs | `#737373` | `#a1a1a1` |
| `text-kumo-inactive` | Disabled text | `#d4d4d4` | `#525252` |
| `text-kumo-placeholder` | Field placeholder text | `#a1a1a1` | `#737373` |
| `text-kumo-success` | Success text | `#006045` | `#a4f4cf` |
| `text-kumo-danger` | Error text, invalid text | `#c10007` | `#ff6467` |
| `text-kumo-warning` | Warning text | `#bd6500` | `#ff8904` |
| `text-kumo-badge-inverted` | Primary badge text | `#ffffff` | `#000000` |
| `text-kumo-badge-neutral-subtle` | Secondary badge text | `#262626` | `#e5e5e5` |
| `white` | Text and glyphs on `kumo-brand` (Tailwind `text-white`) | `#ffffff` | `#ffffff` |
| `code-keyword` | SQL keywords | `#d73a49` | `#a0a0a0` |
| `code-string` | SQL and JSON strings | `#032f62` | `#99ffe4` |
| `code-number` | SQL numbers, JSON numbers and literals | `#005cc5` | `#ffc799` |
| `code-comment` | SQL comments | `#6a737d` | `#8b8b8b94` |
| `code-key` | JSON keys | `#005cc5` | `#ffc799` |

`docs_policy.py` compares the token names in this table with `colorTokens()` in `colors/colors.cpp`.
A desktop test compares each name and value with a fixed Kumo table.

- Kumo values have a contrast below 4.5:1 for `text-kumo-inactive`, `text-kumo-placeholder` and `text-kumo-warning` in Light.
  The values stay the same as Kumo.
- `kumo-line` is below 3:1 on `kumo-base`. It is the Kumo hairline. The field fill and the label also mark a field.
- `white` on the Kumo emphasis gradient is below 4.5:1 at the light end of the brand and danger gradients.
  The gradient stays the same as Kumo. The flat `kumo-brand` and `kumo-danger` tokens pass.
- `text-kumo-default` has 4.5:1 or more on `kumo-canvas`, `kumo-base`, `kumo-elevated` and `kumo-tint`.
- `text-kumo-subtle`, `text-kumo-success`, `text-kumo-danger` and the `code-*` colors have 4.5:1 or more on `kumo-base`.
- `white` has 4.5:1 or more on `kumo-brand` and `kumo-brand-hover`.
- The forced-contrast colors replace each token with a role of the platform palette.

Some colors are computed in `colors.cpp` from a token and an opacity (Tailwind `/NN`). They are not tokens:

- The dialog backdrop is `kumo-recessed` at 80% (`bg-kumo-recessed opacity-80`).
- A toast fill is the status tint over `kumo-base`: `kumo-success-tint/20`, `kumo-warning-tint/50` and `kumo-danger-tint/50`.
- A status line with an error is `kumo-danger-tint` over `kumo-base`.
- A field ring with focus is `kumo-focus/50`. A field ring with an error and focus is `kumo-danger/50`.
- A disabled field is `kumo-control/50` with a `kumo-line/50` ring. This replaces the Kumo `opacity-50`, because QSS has no opacity.
- `button-emphasis-*` are the Kumo emphasis mixes. `Buttons` below gives them.

## Sizes

`Dimension` is the only source of sizes.
The values are logical pixels. They are the Kumo class values (`h-9` is 36 px).

| Dimension | Value | Kumo source | Use |
| --- | --- | --- | --- |
| `ControlExtraSmall` / `ControlSmall` / `Control` | 20 / 26 / 36 | Button and input `xs` / `sm` / `base` (`h-5`, `h-6.5`, `h-9`) | Buttons, fields, selects and spin boxes. Icon-only buttons are square. |
| `Row` | 34 | Sidebar menu button (`h-8.5`) | Navigator, lists, tree rows and connection profiles. A two-line history row is 2 × `Row`. |
| `TableRow` / `Header` | 32 / 36 | Compact table cell and head | Result and object grids and their headers |
| `MenuRow` | 33 | Dropdown item (`py-1.5` with `text-base` line) | Menu, select and quick search items |
| `Tab` / `DocumentTab` | 36 / 30 | Tabs `base` and `sm` | Pane tabs and document tabs |
| `DocumentTabWidth` | 168 | Layout constant | Document tab width. An `Untitled query` title fits without elision. |
| `Toolbar` | 36 | Control height | Workspace toolbar and dock titles |
| `IconSmall` / `Icon` | 14 / 16 | `size-3.5` / `size-4` | Icons |
| `IconLarge` | 20 | `size-5` | The status icon of a confirmation, `Three` from its text |
| `Logo` | 28 | `size-7` | Database logos on the square driver choices of the connection dialog |
| `Checkbox` | 16 | `size-4` | Checkbox and radio indicators |
| `Badge` | 20 | Badge line box | Badges |
| `SwitchWidth` × `SwitchHeight`, `SwitchThumb`, `SwitchRadius` | 36 × 18, 18, 5 | Switch `base` (`h-4.5 w-9`, `w-4.5`, `rounded-[5px]`) | Switch track and thumb |
| `Progress` / `Scrollbar` | 8 / 8 | Meter `h-2` | Progress bars and scrollbars |
| `ToastProgress` | 82 | Layout constant | Toast progress bar width |
| `ToastWidth` | 340 | Toast viewport | Toast width |
| `MenuWidth` | 144 | Dropdown `min-w-36` | Menu minimum width |
| `ModalWidth` / `QuickSearchWidth` / `SheetWidth` / `CompletionPopupWidth` / `TableColumn` | 700 / 672 / 480 / 420 / 160 | Layout constants, except `QuickSearchWidth`, the Kumo command palette `max-w-2xl` | Layout widths |
| `TableColumnMin` / `TableColumnMax` | 80 / 400 | Layout constants | The limits of `fitColumnsToContents` for a data table column |

- **Spacing:** `Half` 2, `One` 4, `OneHalf` 6, `Two` 8, `TwoHalf` 10, `Three` 12, `Four` 16, `Six` 24, `Eight` 32.
- **Radius:** `Small` 4, `Medium` 6, `Large` 8 and `ExtraLarge` 12.
- `Small` is for kbd, extra small controls and select options.
- `Medium` is for small controls, menu items, tooltips and selected segments.
- `Large` is for base controls, fields, menus, popovers, quick search rows, sidebar rows and segmented tracks.
- `ExtraLarge` is for dialogs, sheets, toasts and the empty card (Kumo `rounded-xl`).
- Badges and progress bars have full round ends: the radius is half of the height (Kumo `rounded-full`).
- **Elevation:** `Tooltip` (`shadow-md`), `Popover` (`shadow-lg`) and `Dialog` (`shadow-xl` at 3% opacity).
  Each layer has the Tailwind offset, blur, spread and opacity.
  Qt has no spread, so the menu shadow erodes or dilates the alpha mask by the spread before the blur.
- **Backdrop:** a dialog dims the window with `kumo-recessed` at 80%. Qt has no backdrop blur, so there is no blur.
- **Motion:** 100 ms for interactions and 150 ms for popups, with `cubic-bezier(0.4, 0, 0.2, 1)`.
  The Kumo loader turns in 2000 ms (`LoaderTurn`) and its arc grows in 1500 ms (`LoaderDash`), both linear.
  Reduced motion sets the duration to 0.
  Each animation takes its duration from `motionSpec`.
  `design::Spinner` paints the Kumo loader for busy states. With reduced motion, the arc stays still.

Tables, trees and document tab bars have square corners.
Each data table uses `configureDataTable`: it shows striped rows and no grid lines (Kumo `table`).
The striped rows use `kumo-elevated` on the `kumo-base` base. The row header ends at the last row.
The row numbers are centered `Callout` text at weight 400 in `text-kumo-subtle`, with no border between them.
The cell text is `Body`. A header cell is `Headline` text on `kumo-base` with a 1px `kumo-fill` bottom border.
Header cells have no separators and no hover fill, as in Kumo.
The SQL results, the object `Data` page and the object metadata use this one look.
A changed cell has a focus-ring-width bar at its start in `text-kumo-warning`. A new row has only the `kumo-success-tint` fill.
A row to delete has struck-through text. A `NULL` value uses italic `Callout` text.
The sorted column shows the `ArrowUp` or `ArrowDown` icon at `IconSmall` at its end.
When the sort has more than one column, each sorted column also shows its sort position as `Callout` text before the icon.
The header menu and the `Choose columns…` command (Ctrl+Alt+F) show a checkable menu of the columns. The engine refuses to hide the last visible column.
`QDateTimeEdit` uses the spin box rules and the field base rules. The cell edit sheet shows it for a date or time column.
Each `::item` rule of a list, tree or table view sets `border: none`. Qt 6.8 ignores the padding of a table item without a border or a background. Lists and trees use the same rule.
Desktop tests measure the rendered text inset of a table cell, a list row and a tree row.
Document tabs are Kumo `underline` tabs on `kumo-base`, with `Body` text at weight 500.
The current document tab has a 2px `kumo-brand` bottom indicator. A hovered tab is `kumo-tint`.
Pane tabs are a Kumo `segmented` control, with `Body` text at weight 500.
The track is `kumo-recessed` at `Large`, and it has the size of its tabs.
The selected tab is a `kumo-base` segment at `Medium` with a `kumo-line` ring and an inset of `Half`.
Qt cannot draw the Kumo `shadow-sm` of the segment or the `kumo-hairline/70` ring of the track.
`design::SegmentedControl` gives the same look to a row of icon `design::Button` segments.
An unselected segment has `text-kumo-subtle` icons and no hover fill.

Fields, text areas, spin boxes and selects use the Kumo `input` look.
The fill is `kumo-control` with a 1px `kumo-line` ring, `Large` corners, `Three` padding and `Body` text.
The placeholder is `text-kumo-placeholder`. A field has no hover change. A select changes to `kumo-tint` on hover.
A select shows the Phosphor `caret-up-down` icon in `text-kumo-subtle`, `Three` from its end.
Its popup has a `kumo-line` ring and `OneHalf` padding.

A checkbox is `kumo-base` with a `kumo-hairline` ring. When it is on, it is `kumo-contrast` with a `text-kumo-inverse` bold check.
A radio is `kumo-base` with a `kumo-line` ring. When it is on, it is `kumo-contrast` with an 8px `kumo-base` dot.
Checkboxes, radios and switches have no hover change. When disabled, they have 50% opacity.
The label is `Two` from the indicator.

A switch uses the Kumo palette colors in `switchColors`: `blue-500`/`blue-600` when on, `neutral-200`/`neutral-700` when off.
The track and the thumb have `SwitchRadius` corners. The thumb has the `kumo-shadow-edge` and `kumo-shadow-drop` shadows.

A menu is the Kumo `dropdown` popup: `kumo-control` with a 1px `kumo-line` ring, `Large` corners and `OneHalf` padding.
Items use `Body` text and `Medium` corners. A button menu opens `Two` below its button (Kumo `sideOffset`).
A tooltip is `kumo-base` with a 1px `kumo-line` outline, `Medium` corners, `OneHalf` × `TwoHalf` padding and `Callout` text.
It opens `TwoHalf` from its owner. The Kumo 20 × 10 arrow points at the owner, with `kumo-arrow-edge` and `kumo-arrow-stroke` edges.

Quick search is the Kumo `command-palette`: a `QuickSearchWidth` panel with a `kumo-line` ring and `Large` corners on a `kumo-overlay` backdrop at 80%.
The header is `kumo-base` with `Four` × `Three` padding, the `Search` icon in `text-kumo-subtle` and a bare input.
The result list is `kumo-base` with `Two` padding and a `kumo-hairline` top edge.
A result row is `MenuRow` high with `Large` corners, and it shows the title, a `·` separator and the subtle `Callout` detail on one line.
Kumo uses a dash. The `ui_policy.py` separator rule keeps `·`.
The highlighted row is `kumo-overlay`. The footer is `kumo-elevated` with `Four` × `Three` padding, the status in `Caption` and the key hints.
Kumo also shows a trailing arrow on the highlighted row. ChoscorDB shows the `Return` key hint in the footer.

`DialogSize` gives the initial size of each dialog type:

- `Short`: confirmations, renames and update progress. Its width is 384, the Kumo base dialog `w-96`.
- `Form`: export, diagnostics and SSH host keys.
- `Detail`: value details, status details and the grid review.
- `Preferences` and `Profiles`: the dialog with the same name.

`LayoutMetrics` gives window defaults, minimum sizes and initial pane sizes.
These values are layout constants and are not on the design scale.

## Typography

ChoscorDB uses four text styles.
Their names come from the Apple macOS [text styles](https://developer.apple.com/design/human-interface-guidelines/typography).
The sizes come from the Kumo type scale (`theme-kumo.css`): `text-xs` 12/16, `text-sm` 13/15 (`1/0.85`), `text-base` 14/21.
Kumo `text-lg` (16/24) has no ChoscorDB use, so it has no style.
All UI styles use Inter 4.1, bundled in `desktop/resources/fonts/` under the OFL 1.1 license.
`SOURCE-INTER.json` records the release, the archive and the SHA-256 of each file.
Thus each platform shows the same typeface.

As on the Kumo documentation site, each UI style has a letter spacing of −0.01 em and the OpenType features `cv02`, `cv03`, `cv04` and `calt`.
Qt keeps a letter spacing in 1/64 px steps, so the value is the nearest step.
All sizes are logical pixels.
Fonts prefer vertical-only hinting.
This keeps small text sharp on Windows and Linux.

| `TextStyle` | Size/line height, weight | Kumo class | Use |
| --- | --- | --- | --- |
| `Headline` | 14/21, 600 | `text-base font-semibold` | Headings and dialog titles (`@font-headline`) |
| `Body` | 14/21, 400 | `text-base` | Default UI text, table cells, menu items, fields and tab labels |
| `Callout` | 13/15, 400 | `text-sm` | Tree and list items, tooltips, secondary detail, field errors (`@font-callout`) |
| `Caption` | 12/16, 500 | `text-xs font-medium` | Badges (`@font-caption`) |

Sidebar section labels are uppercase `Caption` text at weight 600 in `text-kumo-subtle`, with 0.04 em tracking. The accessible text keeps its mixed case.
Call `design::Text::setSectionLabel()` to apply it. It is not a ninth text style.
The source text, the `tr()` text and the accessible name keep their case.

`FontDesign::Monospaced` sets a style in the Kumo monospace stack, as the SwiftUI `.monospaced()` modifier does.
The stack is `ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace`.
ChoscorDB does not bundle a monospace font. It uses the first installed family of the stack (`SF Mono` for `ui-monospace`).
If no family of the stack is installed, it uses the system fixed font (`monospace`).
Monospaced text has no letter spacing and no Inter features.
Read-only SQL previews use `Body` with `designRole="codePreview"`.
Read-only message logs use `designRole="log"`: no border, fill or focus ring, with `Three` × `Four` padding on `kumo-base`. The text stays selectable.
When an object pane shows its panel state, the pane page is hidden, so an empty table header does not show below the message.
SQL excerpts in history rows use `Callout`.
QSS reads the size and the weight of each style as `@font-<style>` and `@font-<style>-weight`.

The SQL editor uses the font that the user saved in the preferences.

## Icons

- Each UI icon is an unmodified Phosphor file of the regular weight, from `@phosphor-icons/core` 2.1.1.
  Kumo (commit `eaa9e173`) resolves `@phosphor-icons/react` 2.1.10, which uses this core release.
- `desktop/resources/icons/SOURCE-PHOSPHOR.json` pins the npm tarball and the SHA-256 of each file.
- Where Kumo uses a specific icon for a role, ChoscorDB uses the same icon.
  These are `x` to close, `check` and the `caret-*` carets.
  The toast icons are `check-circle`, `warning` and `warning-octagon`.
- No ChoscorDB role matches a Kumo `weight="bold"` use, so all icons are regular.
- Phosphor icons are filled paths. `themedIcon` changes `currentColor` to the theme color of the role.
- There is no icon stroke token.

The app mark and the database logos are the only adapted icons.
`SOURCE-PHOSPHOR.json` records the app mark, adapted from the Phosphor `paw-print`, in its `adaptations` field.
`SOURCE-DATABASE-LOGOS.json` records the PostgreSQL, SQLite and MySQL logos.
Each adapted icon is an SVG with a square `viewBox` and no `width` or `height` attribute.
These icons keep their brand colors.

Icons render only at `Dimension::IconSmall` (14, Kumo `size-3.5`) or `Dimension::Icon` (16, Kumo `size-4`).
There are two exceptions. A database logo on a driver choice is at `Dimension::Logo`.
The status icon of a confirmation is at `Dimension::IconLarge`.
A driver choice is a square button with `Three` padding around the logo. Its tooltip and accessible name give the driver name.
Sidebar rows show `Body` text and `Icon` icons.
`scripts/ci/ui_policy.py` enforces the icon rules:

- Each icon file must be an SVG with a provenance record.
- Each icon file must match the SHA-256 in its record.
- Each adapted icon must have a square `viewBox` and no `width` or `height`.
- Each record must have a file.
- Each icon in the Qt resource file must have a provenance record.
- Icon sizes in `themedIcon`, `setIconSize` and `pixmap` must be icon-size tokens.

These rules accept no `ui-ok` marker.
Map object kinds to icons with `desktop/app/object_kind_icon.h`.
Use `design::Icon` roles. Do not use Qt standard icons. Do not show icons in menu bar menus.

## State model

- **Hover:** Rows, menu items and ghost buttons change to `kumo-tint` (Kumo `hover:bg-kumo-tint`).
  A status line and a toast set `designSurface="banner"`. On a banner, Ghost and tool buttons use `kumo-fill`, because `kumo-tint` is lighter than the banner.
- **Pressed:** Kumo has no pressed fill for buttons, rows, tool buttons and tabs.
- **Selected:** Selected rows, checked buttons and checked segments use `kumo-tint` with `text-kumo-default` text (Kumo table `selected` and sidebar `active`).
  A hovered selected row stays `kumo-tint`, as in Kumo. A hovered checked button also stays `kumo-tint`.
  A checked Default or Destructive button keeps its emphasis fill. It does not use the checked tint.
  A selected row keeps `kumo-tint` when the window is not active. Kumo has no inactive selection.
  This applies to tree, list, table, history and connection profile rows. Quick search rows use `kumo-overlay`, as in Kumo.
  Selected rows are not bold.
- **Navigator:** The navigator rests on `kumo-elevated`.
  Its rows change to `kumo-tint` on hover and when they are selected.
  Group rows, for example `Tables` and `Views`, have no icon and use `text-kumo-subtle`, as the sidebar group label does.
- **Menu highlight:** `kumo-tint` with `text-kumo-default` (Kumo `data-highlighted:bg-kumo-tint`).
  Disabled items use `text-kumo-inactive`.
- **Primary:** The Kumo brand emphasis gradient, with a lighter top edge on hover. The text is always `white`.
- **Focus:** A 2px `kumo-brand` ring shows only for focus from the keyboard.
  The window keyboard focus attribute sets it. Item views set the keyboard focus flag on each row, so the flag alone does not.
  Rows, cells, tree rows, pane tabs, tool buttons and fields show the ring. A field with any focus also has a `kumo-focus/50` ring.
  Painted rows use `showsKeyboardFocus` (`field/focus_indicator.h`) to read the window attribute.
- **Disabled:** A painted control has 50% opacity, as in Kumo.
  A control that the stylesheet styles uses `text-kumo-inactive`, because QSS has no opacity.
  Fields also use the faded fill and ring that the field rules give.
- **Tab close:** The Phosphor `x` icon at `IconSmall`. Hover and pressed use `kumo-tint`, as the Ghost dismiss button does.
  The `interaction-states` gallery specimen shows these states for each family:
  hover, pressed, selected and focus.
- **Status:** Toasts use the computed toast fill with `text-kumo-<status>` text.
  History badges use `kumo-<status>-tint` with `text-kumo-<status>` text.
  A status line is the Kumo `secondary` banner for neutral and success states: `kumo-contrast` at 5% over `kumo-base`.
  Its text is `Callout` in `text-kumo-default` at 70% (`secondaryBanner`). Only its outcome text uses `text-kumo-success`.
  A failed status line uses `kumo-danger-tint` over `kumo-base` with `text-kumo-danger` text.
  Toasts also show a status icon.
  The neutral status uses `kumo-elevated` with `text-kumo-subtle`.
  Danger toasts stay until the user closes them.
- **Toast card:** A toast is the Kumo `toast`. It is `ToastWidth` wide and has `Spacing::Four` padding.
  It is `kumo-base` with a 1px `kumo-line` ring and `ExtraLarge` corners, `Eight` from the corner of its host.
  A status toast has its status tint fill and a ring of its status color at 30%. Qt cannot draw the Kumo 0.3px ring.
  Row 1 has the status icon and the title. The icon is `Half` below the top padding.
  The title has weight 500 and the status color.
  The shared ghost Close button (`design::createDismissButton`) is `Two` from the top and end edges.
  Row 2 has the body in `text-kumo-default` at 70%.
- **Previous and next:** A page control (result pager, history pager) shows only the chevron icons.
  The tooltip gives the label.
  A control that moves between items (find, value detail) shows a chevron icon and its text label.
- **Primary button icon:** A dialog footer button shows text only. It has no icon.
- **Toast title:** The title of a success toast names the result, for example `SQL generated` or `Object renamed`.
  The title of a warning or an error toast is the severity: `Warning` or `Error`.
  Do not use `Success` as a title.
- **One outcome, one place:** A success or a warning outcome shows only in a toast.
  A dialog status line shows only work in progress, errors and the neutral cancelled state.
  If no toast host is available, the status line keeps the outcome.
  `addStatusLine` puts the status line at the body bottom. Content that expands vertically keeps all the space above it.
- **Event locations:** Each class of event has one location.

  | Event | Location |
  | --- | --- |
  | Query failure | The Messages pane has the full text. The status line has only the headline and the recovery hint. |
  | Query result | The result grid and the status line metrics. |
  | Failed command or file operation | The shared error dialog (`MainWindow::reportError`). |
  | Background error or blocked action | A replacing toast (`MainWindow::reportNotice`). |
  | Success of a command | A success toast. |
  | Work in a dialog | The dialog status line. |

- **Loading:** The spinner shows each load in the place of its result.
  A sidebar section shows it in its header. A panel with a status line shows it in the status line.
  A navigator row shows the `IconSmall` `Loader` mark, because a row has no space for a spinner.
- **Replacing toast:** `ToastRegion::showReplacingToast` shows background errors and blocked-action warnings.
  A new call replaces the text and adds to the count in the title, for example `Warning · 2 notices`.
  It never queues a second copy.
  It waits while other notices show, and it stays until the user closes it.
- **Status line notice:** `StatusLine::showNotice` shows a short notice in place of the message.
  The caller gives the expiry time. Rust owns the time (`EngineAdapter::panelNoticeMs`).
  The notice clears only if it still shows. New content stops the expiry.
  A notice with no expiry stays until new content replaces it.
- **Row status:** `ColumnRowDelegate` paints a row that has a `RowMark` in its mark role.
  The navigator delegate gives `NavigatorModel::StatusRole` as the mark role.
  It paints the name and one trailing `IconSmall` icon: `Loader` for loading, `Error` for failed and `Warning` for unavailable.
  A failed row has a `kumo-danger-tint` fill, and its name and icon use `text-kumo-danger`.
  The status text is only in the tooltip and the accessible description, never in the name.
- **Panel state:** `design::createPanelStateText` makes the one centered placeholder of a panel.
  `design::showPanelState` shows the empty, no-match or failed kind, or hides the placeholder.
  The failed kind uses the `fieldError` role and the `error` state. The other kinds use the description role.
  Rust gives the text and the kind through the panel `Presentation`.
  The gallery specimen `panel-state` shows each kind in the Light and Dark themes.
  `QuickSearchDialog::setSearchState` shows the kind below the results, and the object explorer shows it above its pages.
  The `ui-policy` gate rejects a screen label that sets an empty, no-match or failed text or state itself.
- **Badge:** A badge is the Kumo `badge`: `Badge` high with full round ends, `Two` padding and `Caption` text.
  The default is the Kumo `primary` badge: `kumo-badge-inverted` with `text-kumo-badge-inverted` text.
  `secondary` is `kumo-fill` with `text-kumo-badge-neutral-subtle`. `outline` is `kumo-base` with a `kumo-fill` border.
  `error`, `success` and `warning` use the status tint with the status text.
- **Driver badge:** A driver name uses the `outline` badge.
- **Status tone:** `StatusTone` names a semantic tone: `Neutral`, `Brand`, `Success`, `Warning` or `Danger`.
  `statusToneColor` gives the text color of the tone. `statusToneBadgeVariant` gives its badge variant.
  A saved connection row paints a dot in the tone color. The connection tag in the result footer is a badge.
- **Progress:** A progress bar is the Kumo `meter`. Its track is `kumo-fill` and `Progress` high, with full round ends.
  The indicator is `kumo-brand`.
- **Spinner:** The spinner paints the Kumo `loader`: a track ring at 10% and a round-capped arc in `text-kumo-subtle`.
  The arc turns in `Motion::LoaderTurn` and grows in `Motion::LoaderDash`. With reduced motion, the arc stays still.
- **Empty state:** A panel state is centered text with no card, border or fill.
  It must not look like a field in a narrow pane.
  It has `Six` × `Four` padding. The text is `text-kumo-subtle`, or `text-kumo-danger` when it failed.
- **Form label:** `addFormRow` gives the caption the `formLabel` role, the Kumo `label`: `Body` at weight 500 in `text-kumo-default`.
- **Kbd:** A key is Kumo inline code: monospaced `Callout` at weight 400 on `kumo-base`.
  It has a `kumo-hairline` border and `Small` corners.
  It shows next to the unmodified driver logo.
  Connection profile rows show the logo on the same neutral tile.

## Buttons

`design::Button` paints the Kumo button (`components/button/button.tsx`).
The shared cascade paints stock `QPushButton` controls with the same values.

| Variant | Rest | Hover | Text |
| --- | --- | --- | --- |
| `Default` | Brand emphasis gradient, brand ring | Lighter gradient start | `white` |
| `Destructive` | Danger emphasis gradient, danger ring | Lighter gradient start | `white` |
| `Secondary` | `kumo-base`, `kumo-line` ring | `kumo-tint` | `text-kumo-default` |
| `Outline` | Transparent, `kumo-line` ring | `kumo-focus` ring at 25% | `text-kumo-strong` on hover |
| `Ghost` | Transparent | `kumo-tint` | `text-kumo-default` |
| `Link` | Transparent | Underline | `text-kumo-brand` |

- Kumo mixes the emphasis colors in OKLCH with `color-mix`.
  `scripts/design/kumo_colors.py` calculates the `button-emphasis-*` values from the Kumo tokens.
- The ring is 1px. The top inner row of an emphasis button uses the lighter emphasis color.
  Qt has no inset shadow, so this row replaces the Kumo inner highlight.
- Sizes `ExtraSmall`, `Small` and `Default` are 20, 26 and 36 high, with the radii `Small`, `Medium` and `Large`.
- The padding is 6, 8 or 12. A leading icon decreases the start padding to 4, 6 or 10.
- The label is `Caption` for the two small sizes and `Body` for `Default`, at weight 500.
- A disabled painted button has 50% opacity, as in Kumo. Its colors do not change.
- Sidebar tabs are the segments of a `design::SegmentedControl`, with the pane tab look.
  They show only icons, because the sidebar is narrow. The tooltip and the accessible name give the label.
  The arrow keys move between them.
- **Refresh:** One `Refresh` command reloads each panel. F5 refreshes the navigator when it has focus.
  The object pane and history refresh with their `Refresh` button.
- The cascade gives painted buttons a transparent background. Thus a Ghost or Outline button shows its surface.

## Dialog contract

`design::DialogSections` owns the layout of each dialog.

- **Surface:** A modal dialog is the Kumo `dialog`: `kumo-base` with a 1px `kumo-line` ring and `ExtraLarge` corners.
  The header and the body have `Six` padding, and the body has a `Four` gap. The footer has `Four` × `Six` padding.
  A non-modal dialog is a native window, so it has no ring.
  The backdrop is `kumo-recessed` at 80%, with no blur.
  The dialog shadow is the Kumo `shadow-m` override. Qt has no spread, so each layer shape shrinks by its spread before the blur.
  A confirmation is the Kumo base dialog: 384 wide, with `Six` padding and a `Four` gap.
  A right sheet has a 1px `kumo-line` edge at its start. Its body has the dialog body padding, so the content starts at the title. Its footer has square corners.

- **Footer:** `addAction` takes a `DialogActionRole`, and the role sets the place and the variant.
  Utilities (`Utility` is Secondary, `DestructiveUtility` is Destructive) are at the left.
  At the right, the dismiss action (`Dismiss`, Outline) comes before the decision (`Primary` or `Destructive`).
  The `Primary` action is the default button.
  If the decision is destructive, the dismiss action is the default button.
- **Words:** `Cancel` closes and drops a draft. `Close` closes a dialog that has no draft.
  A button label is a verb, for example `Save connection` or `Discard changes`. Do not use `Yes` or `No`.
- **Confirmations:** `ConfirmationDialog::confirm` and `addActionButton` take a `Consequence`.
  A destructive action has the Destructive variant, and `Cancel` is the default button.
- **Forms:** `addForm` and `configureForm` put each label at the left of its field, with no colon.
  `addFormRow` gives each label a different mnemonic.
  `FieldNeed::Optional` adds `(optional)` to the label. A placeholder shows only an example value.
  A secret field that has a saved secret shows `Saved password` or `Saved private key` as its placeholder.
  Help text is a description row below the field. A read-only row with no value is hidden.
  A search or filter field has a placeholder that starts with its verb and ends with `…`.
- **Validation:** The dialog validates when you click the primary action.
  `FieldValidation` shows the error below the field and moves the focus to the field.
- **Feedback:** `addStatusLine` adds the status line at the bottom of the body.
  The status line shows progress with the spinner, errors and the result of a utility action.
  When the primary action succeeds, `DialogShell::closeWithSuccess` closes the dialog and shows a toast with a specific title.
- **Drafts:** `DialogShell` asks before it drops changes to its fields on Cancel, on Escape or on a click outside.
- **Busy state:** A dialog shows busy work in its status line. It does not open a second dialog.

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
