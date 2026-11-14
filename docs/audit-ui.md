# UI audit of the desktop frontend

This page records an audit of the ChoscorDB desktop frontend on 2026-11-13, at commit `98ab62d`.
It lists the UI components, the layout, the interaction states and the defects.
It does not change code.
This page is a snapshot of that commit. Later changes resolve its findings.
`docs/design-system.md` gives the current behavior.
Each finding has an ID, a severity and its evidence, so that a later change can refer to it.

Paths in this page are relative to `desktop/` unless they start with `docs/` or `crates/`.
For the intended rules, read [Design system](design-system.md).

## Method and limits

The audit used five sources:

- The source of `desktop/design_system/`, `desktop/app/` and `desktop/widgets/`.
- 13 captures of real screens from the desktop tests, on the Qt `offscreen` platform.
- 34 Light and Dark captures from the component gallery (`choscordb-component-gallery --export`).
- Contrast ratios (WCAG 2), measured on the captures and calculated from `design_system/colors/colors.cpp`.
- A state probe: a temporary program outside the repository that links the real components.
  It sends real pointer and focus events on the `offscreen` platform and compares the pixels with the rest state.

Three parallel reviews read the source and reported findings.
A second pass then compared each cited line in this page with the source.
The second pass removed one wrong finding and corrected eight line numbers.

| Source group | First pass | Second pass |
| --- | --- | --- |
| `design_system/` QSS files and painters | Full read | Each cited line, and the QSS rules of each state matrix row |
| Dialogs in `widgets/` and the dialog shells | Full read | Each cited line |
| `app/main_window*.cpp`, `app/navigator_controller*.cpp`, `app/object_explorer.cpp`, `app/object_data_workspace.cpp` | Full read | Each cited line |
| `app/query_workspace*.cpp` | Full read of five files, search of four files | Each cited line, and a read of the cell edit, JSON and grid review code |
| `app/main_window_object_actions.cpp`, `app/main_window_quick_search.cpp` | Search for UI text | Read of all UI text and all dialog code |
| `widgets/sql_editor/`, `widgets/editor_completion/`, `widgets/object_erd_widget.cpp` | Search for UI text | Read of all color, menu, popup and input code |
| `design_system/status_line/`, `design_system/right_sheet/` | Search for UI text | Read of the state and layout code |
| `app/updater_sparkle.mm`, `app/macos_title_bar.mm` | Not read | Read of all UI text |
| `models/` | Read of the display roles | Each cited line |
| `desktop/tools/` (component gallery) | Used for the captures | Search for the states that each specimen shows |
| Platform branches (`Q_OS_`) in `app/`, `widgets/`, `design_system/` and `models/` | Not read | Read of the branches in the main window, search of the 20 branches for UI code |

The audit has these limits:

- All captures and probe results come from macOS on the Qt `offscreen` platform. No pointer on a real screen was used.
- Windows and Linux were not run.
  The source has platform branches only for the title bar, the menu bar, the updater and one shortcut.
  All other UI code is the same on each platform, so the findings apply to each platform.
  Font rendering, native menus and window frames on Windows and Linux are unverified.
- The probe uses the real row delegates with synthetic rows, not the real navigator model.
  It covers buttons, fields, choices, tabs, rows, the result grid, the ERD, menus and the toast close button.
  Tooltips, badges and progress bars have no pointer states, so the captures are their evidence.
- A finding with a capture as its only evidence was seen on that capture, not traced to a source line.
- `ui_consistency.py` reports no violation at this commit. It does not find the defects in this page.
- The audit did not run the full `quality.py fast` set.
- The checks in "Checks to add" are proposals. They do not exist, and no gate has these rules today.
- The priority list is a recommendation. It was not tested against an implementation.

## Summary

The foundation is good, but the surface is not yet at the level of current desktop software.
The audit found 148 defects: 17 high, 87 medium and 44 low.

| Area | Result | Main reason |
| --- | --- | --- |
| Tokens and theme | Good | One palette, one type scale, Light and Dark, no local colors in screens. |
| Typography and icons | Good | One typeface, one icon set, two icon sizes. |
| Interaction states | Weak | Hover is almost invisible, pressed is absent, there is no motion. |
| Contrast | Weak | Field borders, empty-state text and placeholders are below the WCAG limits. |
| Dialogs | Not consistent | Buttons, labels, feedback and close behavior change from dialog to dialog. |
| Main window | Not consistent | One command has different labels, and some commands are hidden. |
| Feedback | Not consistent | One event shows in up to three locations. |
| Polish | Weak | The completion popup, the find panel and some tables look unfinished. |

These are the things that work well:

- All colors in QSS come from tokens. The screens contain no `setStyleSheet` call and no literal color.
- The primary button has the full chain of rest, hover and pressed colors, in the painter and in QSS.
- Table, tree, list, quick search and select rows share one hover color and one selection color.
- All field types share one rule for border, radius, height, focus and the invalid state.
- Status colors are the same in toasts, status lines, history badges and cell marks.
- Each icon-only button has a tooltip and an accessible name.
- All UI text uses the "…" character. No text uses three periods.
- The gallery shows each component family in the two themes.

## Comparison with current desktop software

The comparison uses the common patterns of current database clients and code editors.

| Topic | ChoscorDB today | Common pattern | Result |
| --- | --- | --- | --- |
| Color system | Semantic tokens, Light and Dark, one brand color | Same | Equal |
| Density | 28px rows, 32px controls, 13px text | Same range | Equal |
| Hover feedback | A fill change of 1.04:1 to 1.14:1 | A fill change that is clear at a glance | Worse |
| Pressed feedback | Only on the primary button | On each control that you can click | Worse |
| Motion | One 180 ms toast fade | Short transitions on hover, popups and switches | Worse |
| Load indicators | A static icon | A spinner that turns, or a skeleton | Worse |
| Focus ring | 1px or 2px, different in each component | One 2px ring with an offset | Worse |
| Field boundary | A 1.24:1 border | 3:1 or a filled field | Worse |
| Empty states | One line of faint text | An icon, a title, a description and one action | Worse |
| Completion popup | A flat rectangle without icons | A card with a shadow, kind icons and details | Worse |
| Result grid | No sort or filter on SQL results, no striped rows | Sort, column menu and row focus on each grid | Worse |
| Command discovery | Quick switch for objects and tabs only | A command palette that lists all commands | Worse |
| Navigator | Fixed, cannot be hidden | Can be hidden with a shortcut | Worse |
| Tabs | No tooltip, no middle-click close, text dirty mark | Tooltip, middle-click close, dot in place of the close icon | Worse |
| Toasts | Status icon, title, body, pinned failures | Same | Equal |
| Menus | Rounded card, shadow, shortcut column | Same | Equal |

## Layout inventory

| Region | Widget and component | Size tokens | Source |
| --- | --- | --- | --- |
| Window | `QMainWindow` | Default 1280×900, minimum 960×640 | `app/main_window_ui.cpp:67` |
| Menu bar | `QMenuBar` with File, Edit, Query, View, Window, Help | None | `app/main_window_ui.cpp:71` |
| Navigator dock | `QDockWidget`, fixed on the left, `designSurface="sidebar"` | Width 260, minimum 96 | `app/main_window_ui.cpp:205` |
| Navigator tabs | Three checkable Ghost buttons with icons only | `IconSmall` | `app/main_window_ui.cpp:236` |
| `CONNECTIONS` | `SidebarSection` and `QListWidget` with `NavigationProfileDelegate` | Row 28 | `app/main_window_ui.cpp:280` |
| `PINNED` | `SidebarSection` and `NavigationTreeView` | Row 28 | `app/main_window_ui_pins.cpp:17` |
| `SCHEMA & OBJECTS` | `SidebarSection`, filter field and `NavigationTreeView` | Row 28 | `app/main_window_ui.cpp:316` |
| Saved queries panel | `SidebarSection`, filter field and `QTreeWidget` | Row 28 | `app/main_window_ui.cpp:348` |
| Recent history panel | `SidebarSection`, filter field and `QListWidget` with `RecentHistoryRowDelegate` | About 64 for each row | `app/main_window_ui.cpp:371` |
| Document tab bar | `QTabWidget` with `DocumentTabBar` and `TabAddCorner` | Tab 32, width 140 | `app/main_window_ui.cpp:525` |
| SQL toolbar | `QToolBar` with a split Run button and Outline icon buttons | Toolbar 32 | `app/main_window_ui.cpp:417` |
| Find panel | `SearchPanel` above the tab bar | `ControlSmall` | `app/main_window_ui.cpp:776` |
| SQL editor | `SqlEditor` (QScintilla) with the user font | None | `widgets/sql_editor/sql_editor.cpp` |
| Result grid | `QTableView` with `ResultColumnHeader` | Row 28, Header 28 | `app/main_window_ui.cpp:676` |
| Result filter bar | `ResultFilterBar`, only on the object `Data` page | `Control` 32 | `app/result_filter_bar.cpp:28` |
| SQL status line | `design::StatusLine` with source, state, time, size, page and rows | Toolbar 32 | `app/main_window_ui.cpp:699` |
| Object page header | Segmented `QTabBar`, a line, Refresh and the `Data` toolbar | `ControlSmall` | `app/object_explorer.cpp:205` |
| Object pages | Metadata `QTableView`, DDL preview, ERD and `ObjectDataWorkspace` | Row 28 | `app/object_explorer.cpp:212` |
| Object status line | `design::StatusLine` | Toolbar 32 | `app/object_explorer.cpp:263` |
| Toast region | `ToastRegion` at the bottom right | Width 320 | `app/main_window_ui.cpp:868` |

## Component inventory

The design system has 26 colors, 8 text styles, 8 spacing steps, 3 radii and 2 elevations.
`ui_consistency.py` counts 187 component construction sites in 16 screens, with 100% coverage.

| Family | Components | Source in `design_system/` |
| --- | --- | --- |
| Actions | `Button` (Default, Secondary, Outline, Ghost, Destructive, Link), `ButtonGroup`, `QToolButton` | `button/`, `button_group/`, `tool_button/` |
| Inputs | Line field, password field, shortcut field, text area, select, spin box | `field/`, `text_area/`, `select/`, `spin_box/` |
| Choices | Checkbox, radio button, switch | `checkbox/`, `switch/` |
| Text | `Text`, label roles, badge, keyboard hint, JSON text view | `text/`, `label/`, `badge/`, `kbd/`, `json_text_view/` |
| Navigation | Document tabs, pane tabs, navigator tree, list, connection profile row, history row, column row | `tabs/`, `tree/`, `list/`, `navigation_profile_row/`, `history_row/`, `column_row/` |
| Data | Table, header, result cell delegate | `table/`, `header/`, `item_view/` |
| Layout | Toolbar, dock, splitter, separator, scrollbar | `toolbar/`, `dock/`, `splitter/`, `separator/`, `scrollbar/` |
| Overlays | Menu, tooltip, select popup, quick search, toast region | `menu/`, `tooltip/`, `select/`, `quick_search/`, `toast_region/` |
| Dialogs | Dialog shell, modal dialog, right sheet, dialog sections, confirmation dialog | `dialog_shell/`, `modal_panel/`, `right_sheet/`, `dialog_sections/`, `confirmation_dialog/` |
| Feedback | Status line, panel state, progress bar, field validation | `status_line/`, `panel_state/`, `progress/`, `field/` |

The screens are these:

| Screen | Container | Source |
| --- | --- | --- |
| Main window | `QMainWindow` | `app/main_window_ui.cpp` |
| Connection profile dialog | `DialogShell`, modal | `widgets/profile_dialog/profile_dialog.cpp` |
| SSH host key dialog | `DialogShell` | `widgets/profile_dialog/ssh_host_key_dialog.cpp` |
| Preferences | `DialogShell`, modal | `widgets/preferences_dialog/preferences_dialog.cpp` |
| Export results | `DialogShell`, modal | `widgets/export_dialog/export_dialog.cpp` |
| Query settings | `DialogShell`, not modal | `widgets/query_settings_dialog/query_settings_dialog.cpp` |
| Value detail | `DialogShell`, not modal | `widgets/value_detail_dialog/value_detail_dialog.cpp` |
| Export diagnostics | `DialogShell`, modal | `app/main_window_diagnostics.cpp:218` |
| Update progress | `DialogShell`, modal | `app/updater_windows_linux.cpp:77` |
| Quick search | `QDialog` with `DialogPresentation` | `design_system/quick_search/quick_search_dialog.cpp` |
| Find panel | Inline panel | `widgets/search_panel/search_panel.cpp` |
| History dock | Dock, never shown | `widgets/history_dock/history_dock.cpp` |
| Cell edit, JSON view, object actions | `RightSheet` and `ModalDialog` | `app/query_workspace_cell_edit.cpp`, `app/query_workspace_json.cpp`, `app/main_window_object_actions.cpp` |
| Messages, About, errors, update prompts | `ConfirmationDialog` | `design_system/confirmation_dialog/confirmation_dialog.cpp` |

## State matrix

"Same" means that the state uses the hover value.
"None" means that no rule exists in QSS or in a painter.
No component has a rule for a selection in an inactive window.

| Component | Hover | Pressed | Keyboard focus | Checked or selected | Disabled |
| --- | --- | --- | --- | --- | --- |
| Button, Default | `primary-hover` | `primary-pressed` | 1px inset ring | None | 40% opacity |
| Button, Secondary | `border` | Same | 1px ring | `selection` | 40% opacity |
| Button, Outline | `surface-raised` | Same | 1px ring | `selection` | 40% opacity |
| Button, Ghost | `surface-raised` | Same | 1px ring | `selection` | 40% opacity |
| Button, Destructive | 10% more `danger` | Same | 1px ring | `selection` | 40% opacity |
| Button, Link | Underline | None | 1px ring | `selection` | 40% opacity |
| Stock `QPushButton` | `surface-raised` | Same | Ring border, also for a click | `primary` | `fg-disabled` text only |
| `QToolButton` | `surface-raised` | Same | None | `surface-raised` | `fg-disabled` text only |
| Line field, text area | None | Not applicable | Ring border and 2px frame | Not applicable | `fg-disabled` text only |
| Select | None | None | Ring border and 2px frame | Not applicable | `fg-disabled` text, 50% arrow |
| Select popup row | `surface-raised` | None | None | `selection` | `fg-disabled` |
| Spin box | None | None | Ring border and 2px frame | Not applicable | `fg-disabled` text, 50% arrows |
| Checkbox, radio button | None | None | None in the probe | `primary` fill | 50% opacity, label not changed |
| Switch | None | None | None in the probe | `primary` track | 50% opacity |
| Pane tab | `fg` text | None | None | `surface` segment | `fg-disabled` |
| Document tab | `fg` text | None | 2px ring, `Small` radius | `surface`, bold | `fg-disabled` |
| Tab close mark | None | None | None | Not applicable | 35% opacity |
| Table cell | `surface-raised` | None | None | `selection` | None |
| Table header section | None | None | None | None | None |
| Tree and list row | `surface-raised` | None | None | `selection` | None |
| Navigator row | `sidebar-hover` | None | None | `sidebar-selection` | None |
| Connection profile row | `sidebar-hover` | None | 2px ring, also for a click | `sidebar-selection`, outline and check icon | None |
| History row | `surface` | None | 1px ring, also for a click | `surface` and outline | None |
| Quick search row | `surface-raised` | None | None | `selection` | None |
| Menu item | `surface-raised` | None | Same as hover | Check icon | `fg-disabled` |
| Scrollbar handle | `fg-muted` | Same | Not applicable | Not applicable | None |
| Splitter handle | `ring` | Same | None | Not applicable | None |
| Badge, keyboard hint, progress, tooltip | Not applicable | Not applicable | Not applicable | Not applicable | None |

## Measured states

The state probe gave these results in Light.
The Dark results show the same pattern.
"Fill step" is the contrast ratio between the rest fill and the new fill.
"None" means that no pixel changed.

| Component | Hover | Pressed | Keyboard focus | Disabled |
| --- | --- | --- | --- | --- |
| Button, Default | Fill step 1.20 | Fill step 1.42 | Ring | Fill changes to `#e6b39e` |
| Button, Secondary | Fill step 1.10 | Equal to hover | Ring | Fill step 1.08, text dim |
| Button, Outline | Fill step 1.14 | Equal to hover | Ring | Text and border dim |
| Button, Ghost | Fill step 1.14 | Equal to hover | Ring | Text dim |
| Button, Destructive | Fill step 1.19 | Equal to hover | Ring | Fill step 1.10, text dim |
| Button, Link | Underline only | Equal to hover | Ring | Text dim |
| Stock `QPushButton` | Fill step 1.14 | Fill of hover and a ring border | Ring border | Text dim |
| `QToolButton` | Fill appears | Equal to hover | None | Text dim |
| Line field, empty | None | Ring border from the click | Ring border | None |
| Text area, empty | None | Ring border from the click | Ring border | None |
| Select | None | Not measured | Ring border | Text and arrow dim |
| Spin box | None | Not measured | Ring border | Text and arrows dim |
| Checkbox | None | None | None | Mark dims |
| Radio button | None | None | None | Mark dims |
| Switch | None | None | None | Track dims |
| Pane tab, not current | Text color only | Becomes current | None | Text dim |
| Document tab, not current | Text color only | Becomes current | Not valid in the probe | Text dim |
| Tab close mark | None | None | None | Mark dims |
| Tree row | Fill step 1.14 | Becomes selected, fill step 1.22 | None | None |
| List row | Fill step 1.14 | Becomes selected, fill step 1.22 | None | None |
| Stock table cell | None | Becomes selected, fill step 1.22 | None | None |
| Table header section | None | None | None | None |
| Scrollbar handle | None in the probe | Changes | None | None |
| Navigator row | Fill step 1.04 | Becomes selected, fill step 1.19 | None | None |
| Connection profile row | Fill step 1.04 | Becomes selected, with a ring from the click | Ring | None |
| History row | Fill step 1.14 | Becomes selected, with a ring from the click | Ring | None |
| Result grid cell | Fill step 1.14 | Becomes selected, fill step 1.22 | None | None |
| `ResultColumnHeader` section, clickable | None | None | Not measured | Not measured |
| ERD table box | None | None | Not measured | Not measured |
| Menu item | Fill step 1.14 | Equal to hover | Equal to hover | No hover |
| Toast close button | Fill becomes `surface-raised` | Equal to hover | Not measured | Not measured |

The probe confirms the state matrix, with three additions:

- A checkbox, a radio button and a switch showed no ring for keyboard focus.
  The painter has code for this ring, but the code did not run.
- An empty line field and an empty text area did not change at all when disabled.
- A disabled tree and a disabled list did not change at all.
- A selected row shows no change on hover.
  This applies to the navigator, the connection profile list, the history list and the result grid.
- A history row on hover and a selected history row have the same fill. Only a 1px outline separates them.
- In Dark, the navigator hover step is 1.06, and the selection step is 1.41.
- A result header section that you can click shows no hover and no pressed change.
  The sort mark changes 35 pixels of the section.
- An ERD table box opens an object on a click.
  It shows no hover, no pressed and no selected change.
- The hover fill of the toast close button is equal to the surface of a neutral toast.

## Findings: interaction states

| ID | Severity | Finding | Evidence |
| --- | --- | --- | --- |
| S1 | High | Hover is almost invisible. The probe measured a fill step of 1.14 on rows and buttons and 1.04 on navigator rows. | State probe, `design_system/colors/colors.cpp:44` |
| S2 | High | Ghost and `QToolButton` hover uses the color of the surface below them in status lines, toasts and dialog footers. The probe confirms this for the close button of a neutral toast. | `design_system/button/button.cpp:153`, `design_system/status_line/status_line.cpp:398` |
| S3 | High | The focus ring is different in each component: 1px on buttons, 2px on fields, none on tables, trees, lists and quick search rows. | `design_system/button/button.cpp:215`, `design_system/field/field.cpp:83`, `design_system/item_view/item_view_style_sheet.qss:44` |
| S4 | High | A checked control has three looks: `selection` on `Button`, `primary` on a stock button, hover gray on `QToolButton`. | `design_system/button/button.cpp:165`, `design_system/button/button_style_sheet.qss:15`, `design_system/tool_button/tool_button_style_sheet.qss:19` |
| S5 | High | One navigator shows three selection looks: a gray fill, a gray fill with an outline and a check icon, and a white card with an outline. | `design_system/tree/navigation_tree_view.cpp:66`, `design_system/navigation_profile_row/navigation_profile_row.cpp:26`, `design_system/history_row/history_row.cpp:57` |
| S6 | Medium | Only the primary button has a pressed state. In the probe, each other button variant is equal to its hover look when pressed. | State probe, `design_system/button/button.cpp:147` |
| S7 | Medium | Fields, selects, spin boxes, checkboxes, switches and table headers have no hover state. | `design_system/field/field_state_style_sheet.qss` |
| S8 | Medium | There are five disabled treatments: 40%, 50% and 35% opacity, `fg-disabled` text only, and no change. | `design_system/button/button.cpp:178`, `design_system/checkbox/checkbox_indicator.cpp:28`, `design_system/tabs/tab_indicator.cpp:19` |
| S9 | Medium | A disabled primary button keeps a pale brand fill. It looks like an active control with a soft color. The label has 1.8:1. | Gallery `buttons`, capture `unified-sql-960.png` |
| S10 | Medium | An empty disabled field is equal, pixel for pixel, to an empty field. A read-only field looks the same as a field that you can edit. | State probe, gallery `fields`, `design_system/field/field_state_style_sheet.qss:5` |
| S11 | Medium | A disabled list row is not dim. The label of a disabled checkbox is not dim. | Gallery `lists-navigation`, `design_system/field/field_caption_style_sheet.qss:1` |
| S12 | Medium | The motion tokens are not used. The only animation is a toast fade with a literal 180 ms and no reduced-motion check. | `design_system/toast_region/toast_region.cpp:63`, `design_system/tokens/tokens.cpp:80` |
| S13 | Medium | The `Loader` icon does not turn. It has 16px in the status line and 12px in section headers. | `design_system/status_line/status_line.cpp:437`, `widgets/sidebar_section/sidebar_section.h:61` |
| S14 | Medium | The probe shows a ring after a pointer click on connection profile rows, history rows and stock buttons. The design rule is keyboard focus only. | `design_system/navigation_profile_row/navigation_profile_row.cpp:57`, `design_system/history_row/history_row.cpp:105` |
| S15 | Medium | A checked `Button` has no hover and no pressed feedback. A checked Destructive button loses its danger color. | `design_system/button/button.cpp:165` |
| S16 | Medium | The menu highlight is gray. The select popup and the completion popup highlight is orange. | `design_system/menu/menu_style_sheet.qss:20`, `design_system/select/select_popup_view_style_sheet.qss:17` |
| S17 | Medium | The tab close mark is a painted cross. It shows no hover and no pressed change in the probe. The toast close control is a Ghost button with hover. | `design_system/tabs/tab_indicator.cpp:14` |
| S18 | Low | No control sets a pointer cursor. The Link button and the foreign-key icon in a cell do not show a hand. | `design_system/table/table_style.cpp:139` |
| S19 | Low | Tree row hover has no `:!selected` guard. Table and list rows have it. The result depends on the rule order. | `design_system/tree/tree_state_style_sheet.qss:1` |
| S21 | High | A checkbox, a radio button and a switch show no ring for keyboard focus in the probe. The painter code for the ring does not run. | State probe, `design_system/field/focus_indicator.cpp:10` |
| S22 | Medium | `QToolButton`, pane tabs, tree rows, list rows and table cells show no mark for keyboard focus in the probe. | State probe |
| S23 | Medium | A disabled tree and a disabled list show no change in the probe. | State probe |
| S24 | Medium | A history row on hover has the same fill as a selected history row. Only a 1px outline shows the selection. | State probe, `design_system/history_row/history_row.cpp:57` |
| S25 | Low | No selected row changes on hover. Thus the pointer position is not visible on a selected row. | State probe |
| S26 | Medium | A result header section that sorts on a click shows no hover and no pressed change. Its sort mark is small. | State probe, `app/result_column_header.h:56` |
| S27 | Medium | An ERD table box opens an object on a click. It shows no hover, pressed or selected change. | State probe, `widgets/object_erd_widget.cpp:496` |
| S20 | Low | No row keeps a different selection color when the window is not active. | `design_system/theme.cpp:27` |

## Findings: contrast and visual rules

| ID | Severity | Finding | Evidence |
| --- | --- | --- | --- |
| V1 | High | Empty-state text has 1.98:1 in Light and 2.4:1 to 2.5:1 in Dark. `showPanelState` sets the `PlaceholderText` role, and Qt then halves the alpha of the QSS color. | `design_system/panel_state/panel_state.cpp:24`, capture `native-workspace.png` |
| V2 | High | The field border is the only boundary of fields, Outline buttons and empty checkboxes. It has 1.24:1 in Light and 1.38:1 in Dark. | `design_system/colors/colors.cpp:55` |
| V3 | Medium | Placeholder text has about 3.0:1 on the captures. The design page gives 4.5:1 for `fg-muted`. | Gallery `fields` |
| V4 | Medium | The off switch track has 1.35:1 on `surface`. In Dark, the white thumb on the `primary` track has 2.36:1. | `design_system/switch/switch_indicator.cpp:16` |
| V5 | Medium | The invalid border and the focus border are almost the same color in Dark (1.03:1). | `design_system/field/field_state_style_sheet.qss:9` |
| V6 | Medium | Three text pairs are below 4.5:1: the Light default badge (4.24:1) and the Dark Destructive button at rest (4.39:1) and on hover (3.58:1). | `design_system/badge/badge_style_sheet.qss:9` |
| V7 | Medium | Text selection in fields uses `primary`. The design page and the palette use `selection`. | `design_system/field/field_selection_style_sheet.qss:2`, `design_system/theme.cpp:34` |
| V8 | Medium | Field values use weight 500, and labels use weight 400. Thus the value looks bold next to its label. | Gallery `fields`, [Design system](design-system.md) |
| V9 | Medium | The radio button is 18px. The design page gives 16px. | `design_system/control_style.cpp:134` |
| V10 | Medium | Two icon sizes are not tokens: 14px in the tree and a literal 12px in tool buttons. | `design_system/tree/tree_style_sheet.qss:3`, `design_system/tool_button/tool_button_style_sheet.qss:2` |
| V11 | Medium | There are two tooltip looks. The painted tooltip has an 8px radius and an arrow. The QSS tooltip has a 6px radius and no arrow. | `design_system/tooltip/tooltip.cpp:105`, `design_system/table/table_style.cpp:227` |
| V12 | Medium | The menu and the select popup are different: radius 10 and 6, shadow and no shadow, and different row padding. | `design_system/menu/menu_style_sheet.qss:7`, `design_system/select/select_popup_style_sheet.qss:4` |
| V13 | Medium | The completion popup is a flat rectangle with no radius, shadow or kind icon. No QSS rule and no painter refer to its name, `sqlCompletionPopup`. | Capture `native-completion.png`, `widgets/editor_completion/editor_completion.cpp:23` |
| V14 | Low | Select popups, tooltips and toasts have no shadow. Menus and dialogs have one. | `design_system/menu/menu.cpp:92` |
| V15 | Low | Size literals are outside `metrics/`: button padding, field padding, menu widths, badge sizes, dock sizes and tooltip sizes. | `design_system/button/button.cpp:15`, `design_system/menu/menu_style_sheet.qss:2`, `design_system/dock/dock_style.cpp:16` |
| V16 | Low | The value 12px is a literal in six QSS files, because no `@space-3` token exists. | `design_system/style/stylesheet.cpp:84` |
| V17 | Low | `ring` and `code-string` are aliases of `primary`. The design page does not permit aliases. | `design_system/colors/colors.cpp:63` |
| V18 | Low | Six Light grays are between `#d8` and `#f6`. Some pairs have 1.00:1 to 1.04:1 and do the same work. | `design_system/colors/colors.cpp:44` |
| V19 | Low | `Spacing::Six`, the four title text styles and `DialogSize::Short` have no caller in the screens. | `design_system/metrics/metrics.cpp:8` |
| V20 | Low | The select popup rows are defined two times with different values. | `design_system/select/select_style_sheet.qss:28`, `design_system/select/select_popup_view_style_sheet.qss:11` |
| V22 | Medium | Only the `buttons` specimen of the gallery shows hover, pressed and focus. No other component family has a specimen for these states. | `tools/preview/preview_window.cpp` |
| V21 | Low | The row height is not 28 in three lists: 29 in menus and select popups, and about 64 in history rows. | `design_system/history_row/history_row.cpp:37` |

## Findings: component gallery

The component gallery in `desktop/tools/` is a tool for developers.
The UI rules for screens do not apply to it, so these findings have low severity.

| ID | Severity | Finding | Evidence |
| --- | --- | --- | --- |
| G1 | Low | The gallery controls are stock `QPushButton` widgets, not `design::Button`. | `tools/preview/preview_window.cpp:606`, `tools/preview/preview_window.cpp:650` |
| G2 | Low | The gallery sets literal sizes, for example 44, 75, 190 and 220. | `tools/preview/preview_window.cpp:511`, `tools/preview/preview_window.cpp:627` |
| G3 | Low | The token table cuts its `Source` column at the default capture width. | Gallery `tokens` |

## Findings: dialogs

| ID | Severity | Finding | Evidence |
| --- | --- | --- | --- |
| D1 | High | A success shows in three different locations. Preferences and export use a toast. Query settings, the SSH host key dialog and diagnostics write it in the dialog. | `widgets/preferences_dialog/preferences_dialog.cpp:559`, `widgets/query_settings_dialog/query_settings_dialog.cpp:94` |
| D2 | High | Two sibling dialogs save differently. Preferences has `Save preferences` and closes. Query settings has `Apply` and stays open. | `widgets/preferences_dialog/preferences_dialog.cpp:228`, `widgets/query_settings_dialog/query_settings_dialog.cpp:45` |
| D3 | High | `Cancel` and `Close` have no fixed meaning. Preferences has `Close` with a draft. Export has `Close` and `Cancel export`. | `widgets/preferences_dialog/preferences_dialog.cpp:224`, `widgets/export_dialog/export_dialog.cpp:110` |
| D4 | High | The connection profile dialog drops a draft with no prompt on Cancel, on Escape and on a click outside it. | `widgets/profile_dialog/profile_dialog.cpp:86`, `design_system/dialog_presentation/dialog_presentation.cpp:148` |
| D5 | Medium | The footer order changes. Diagnostics puts `Cancel` after the primary button. The disconnect message puts the destructive button first. | `app/main_window_diagnostics.cpp:258`, capture `native-disconnect.png` |
| D6 | Medium | One role has different variants. `Restore defaults` is Outline in preferences and Secondary in query settings. | `widgets/preferences_dialog/preferences_dialog.cpp:221`, `widgets/query_settings_dialog/query_settings_dialog.cpp:49` |
| D7 | Medium | `Clear Local Diagnostics` is Outline. `Clear history…` and `Reset appearance and layout` are Destructive. | `app/main_window_diagnostics.cpp:262`, `widgets/history_dock/history_dock.cpp:148` |
| D8 | Medium | The SSH host key dialog can show two primary buttons side by side. | `widgets/profile_dialog/ssh_host_key_dialog.cpp:82` |
| D9 | Medium | Messages for one decision have different buttons: `Yes` and `No` in one, `Yes` and `Cancel` in the other. | `widgets/export_dialog/export_dialog.cpp:242`, `app/main_window_diagnostics.cpp:370` |
| D10 | Medium | Some destructive messages use `Yes`. Others use a verb, for example `Drop` and `Discard`. | `widgets/history_dock/history_dock.cpp:323`, `app/main_window_object_actions.cpp:103` |
| D11 | Medium | Only the rename dialog and the grid review dialog set a default button. Two update messages set no safe default. | `app/main_window_object_actions.cpp:147`, `app/updater_windows_linux.cpp:325` |
| D12 | Medium | Form labels have three styles: above the field, at the left, and at the left with a colon. | `widgets/profile_dialog/profile_dialog.cpp:108`, `widgets/export_dialog/export_dialog.cpp:69` |
| D13 | Medium | Mnemonics are in the export dialog and in a part of the connection profile dialog only. Two export labels use the letter D. | `widgets/export_dialog/export_dialog.cpp:70`, `widgets/export_dialog/export_dialog.cpp:75` |
| D14 | Medium | Errors show in different components. Diagnostics uses a plain label with no error role for errors, progress and success. | `app/main_window_diagnostics.cpp:275` |
| D15 | Medium | Validation has two models. Export disables its primary button. The connection profile dialog validates when you click. | `widgets/export_dialog/export_dialog.cpp:183`, `widgets/profile_dialog/profile_dialog_actions.cpp:185` |
| D16 | Medium | The busy state has three forms: a second modal dialog, a status line and a plain label. Only export and update have a cancel control. | `widgets/profile_dialog/profile_dialog.cpp:35` |
| D17 | Medium | Only placeholder text marks an optional field, and the placeholder styles are mixed: examples, instructions and label copies. | `widgets/profile_dialog/profile_dialog.cpp:148`, `widgets/profile_dialog/profile_dialog.cpp:192` |
| D18 | Medium | The connection profile dialog title is always `New connection`, also when you edit a saved connection profile. | `widgets/profile_dialog/profile_dialog.cpp:80` |
| D19 | Medium | `Restore defaults` and `Reset appearance and layout` have no message and no text that tells you they wait for a save. | `widgets/preferences_dialog/preferences_dialog.cpp:238` |
| D20 | Medium | Preferences shows `Retry load` and a destructive reset as the first controls of the Appearance page. | Capture `native-preferences.png`, `widgets/preferences_dialog/preferences_dialog.cpp:106` |
| D21 | Medium | Status lines in dialogs have an empty gap at the left where the icon goes. | Captures `native-profile-password.png`, `native-value-detail.png` |
| D22 | Medium | The value detail table does not fill the dialog width. Its headers are centered, but result grid headers are at the left. | Capture `native-value-detail.png` |
| D23 | Low | The port is a spin box with arrows. Its label is 3px higher than the labels next to it. | Capture `native-profile-password.png` |
| D24 | Low | The spin boxes in query settings are taller than the 32px fields in other dialogs. | Capture `native-query-settings.png` |
| D25 | Low | Four small dialogs take no size token. Diagnostics uses the export size. `ConfirmationDialog` uses the literal 440. | `design_system/confirmation_dialog/confirmation_dialog.cpp:166` |
| D26 | Low | Each dialog sets its own body spacing, because `DialogSections` sets 0. Message padding is 16, and section padding is 12. | `design_system/dialog_sections/dialog_sections.cpp:79` |
| D27 | Low | Query settings is not modal, and preferences is modal. The two dialogs edit the same two values. | `app/query_settings.cpp:60`, `widgets/preferences_dialog/preferences_dialog.cpp:153` |
| D28 | Low | Only the export primary button has an icon. `Previous` and `Next` have text in two screens and icons only in the pager. | `widgets/export_dialog/export_dialog.cpp:350`, `widgets/search_panel/search_panel.cpp:40` |
| D29 | Low | `Clear history…` uses the `Close` icon, which is also the icon to close a dialog. | `widgets/history_dock/history_dock.cpp:149` |
| D30 | Low | `RightSheet` has generic accessible names, for example `Sheet title`. | `design_system/right_sheet/right_sheet.cpp:51` |
| D31 | Medium | `RightSheet` does not use `DialogSections`. Its padding is 16, and dialog padding is 12. Its close button is 32px. | `design_system/right_sheet/right_sheet.cpp:60`, `design_system/right_sheet/right_sheet.cpp:80` |
| D32 | Medium | The cell edit sheet and the JSON sheet show status in a plain `Text`. All other dialogs use a status line. | `app/query_workspace_cell_edit.cpp:205`, `app/query_workspace_json.cpp:278` |
| D33 | Low | The cell edit sheet has the menu label `Edit cell…` as its title, with the "…". Its primary button is `Save`. | `app/query_workspace_cell_edit.cpp:198`, `app/query_workspace_cell_edit.cpp:219` |

## Findings: main window

| ID | Severity | Finding | Evidence |
| --- | --- | --- | --- |
| W1 | High | The Cancel button of a query is always hidden, and Cancel has no default shortcut. Only the Query menu and the grid menu can stop a query. | `app/main_window_ui.cpp:474`, `app/main_window_ui.cpp:485` |
| W2 | High | The object `Data` page shows an `Apply…` button for staged edits. SQL results show no edit control. | `app/object_data_workspace.cpp:140`, `app/main_window_ui.cpp:750` |
| W3 | High | Sort and filter are only on the object `Data` page. SQL result headers do not react to a click. | `app/query_workspace_view.cpp:140` |
| W4 | High | One event class goes to different locations: a messages pane that you cannot open, an inline box, a toast or a modal dialog. | `app/main_window_workspace.cpp:469`, `app/object_data_workspace.cpp:54`, `app/main_window.cpp:467` |
| W5 | High | One failure shows in three locations at the same time: the message box, the status line and a toast. | Capture `unified-object-1280.png` |
| W6 | Medium | A toast covers the page controls and the row count of the status line. | Captures `unified-object-1280.png`, `native-navigator-sql.png` |
| W7 | Medium | A connection that is not available shows the same sentence two times, with no Reconnect button. | Capture `app-dark.png`, `app/object_explorer.cpp:271` |
| W8 | Medium | Refresh has five labels and no shortcut. | `app/main_window_ui.cpp:224`, `app/navigator_controller.cpp:553`, `app/object_explorer.cpp:286` |
| W9 | Medium | A failed navigator row shows `Refresh` two times in its menu. | `app/navigator_controller.cpp:486`, `app/navigator_controller.cpp:553` |
| W10 | Medium | Run has three names: `Run`, `Run current` and `Run selection or current statement`. `Run all` is first in the menus. | `app/main_window_ui.cpp:433` |
| W11 | Medium | The `+` button has the tooltip `New connection`, but it starts the menu command `New SQLite session…`. | `app/main_window_ui.cpp:75`, `app/main_window_ui.cpp:220` |
| W12 | Medium | Save, Cancel and six grid edit buttons are hidden widgets that only hold state. | `app/main_window_ui.cpp:429`, `app/query_workspace_grid_commands.cpp:115` |
| W13 | Medium | A pinned object has only `Unpin` in its menu. The same object in the navigator has the full menu. | `app/main_window_pins.cpp:309`, `app/navigator_controller.cpp:553` |
| W14 | Medium | Menus disagree on "…" and on destructive commands. `Drop` and `Rename` open dialogs but have no "…", and no separator isolates `Drop`. | `app/navigator_controller.cpp:599`, `app/main_window_workspace.cpp:735` |
| W15 | Medium | The grid menu ends with `Cancel`, which stops the query. It reads as a command to close the menu. | `app/query_workspace.cpp:296` |
| W16 | Medium | The SQL editor shows the stock QScintilla menu: `Undo`, `Redo`, `Cut`, `Copy`, `Paste`, `Delete`, `Select All`. It has no Run, Find or Complete command. | State probe, `widgets/sql_editor/sql_editor.cpp:58` |
| W17 | Medium | Result headers, the history list and the saved queries tree have no context menu. | `app/query_workspace.cpp:50` |
| W18 | Medium | The three toolbars use three button styles: Outline at 28, Ghost at 32 and Ghost at 28. Only one has a separator. | `app/main_window_ui.cpp:441`, `app/result_filter_bar.cpp:42`, `app/object_explorer.cpp:259` |
| W19 | Medium | Document tabs have no tooltip for a cut title and no close on middle-click. | `app/main_window_ui.cpp:572` |
| W20 | Medium | The dirty mark is the text ` •` after the title. A cut title hides it. Object tabs with staged edits have no mark. | `app/main_window.cpp:460` |
| W21 | Medium | The navigator tabs are three icons with no label. They are buttons, so the arrow keys do not move between them. | `app/main_window_ui.cpp:242` |
| W22 | Medium | Some empty-state text is a C++ literal. The sibling text comes from the Rust engine. | `app/main_window_ui.cpp:299`, `crates/core/src/panel_machine.rs:145` |
| W23 | Medium | Each panel shows a load differently: a header icon, a row mark, a status line or text only. | `app/main_window_workspace.cpp:226`, `app/object_explorer.cpp:596` |
| W24 | Medium | The object filter waits 150 ms. The saved queries and history filters run on each key. No filter has a clear button. | `app/navigator_controller.cpp:86`, `app/main_window_workspace.cpp:219` |
| W25 | Medium | Navigator rows have no tooltip for a cut name, and the horizontal scrollbar is off. | `models/navigator_model.cpp:226`, `app/main_window_ui.cpp:336` |
| W26 | Medium | The navigator opens an object when the current row changes, so the arrow keys open tabs. A pinned object opens on a click. | `app/main_window_navigator.cpp:203`, `app/main_window_pins.cpp:268` |
| W27 | Medium | SQL results show grid lines. The object `Data` page and the metadata tables do not. No grid has striped rows. | `app/main_window_ui.cpp:691`, `app/object_data_workspace.cpp:45` |
| W28 | Medium | Only a background color marks an edited, added or deleted cell. `NULL` uses a default italic font, not the `Callout` style. | `design_system/table/table_style.cpp:131`, `models/result_table_model.cpp:254` |
| W29 | Medium | In the grid, `Ctrl+N` adds a row and `Ctrl+S` applies edits. In the rest of the window, these keys make a query and save a file. | `app/query_workspace_grid_commands.cpp:24`, `app/main_window_ui.cpp:74` |
| W30 | Medium | Three commands with keys are not in the shortcut catalog. Thus a user shortcut can collide with them. | `app/main_window_ui.cpp:84`, `models/shortcut_catalog.cpp:61` |
| W31 | Medium | You cannot hide the navigator. At the 960px minimum width, it leaves about 700px for the workspace. | `app/main_window_ui.cpp:207` |
| W32 | Medium | The splitter between the SQL editor and the result grid can collapse a pane. Only `Reset layout` brings the pane back. | `app/main_window_ui.cpp:524` |
| W33 | Medium | The View menu has ten commands and no separator. On macOS, the View commands move out of the View menu. | `app/main_window_ui.cpp:176`, `app/main_window_workspace.cpp:848` |
| W34 | Medium | The find panel opens above the tab bar and moves the full workspace down. Its `Replace all` button is a primary button. | Capture `native-search.png`, `widgets/search_panel/search_panel.cpp:58` |
| W35 | Medium | A normal completed query makes the full status line green. The status line then looks like an alert. | Capture `native-workspace.png` |
| W36 | Medium | Unmarked literals are in the screens: column widths 80, 320 and 400, timers of 150 to 250 ms, and history limits. | `app/object_explorer.cpp:720`, `app/navigator_controller.cpp:97`, `app/main_window_lifecycle.cpp:234` |
| W37 | Low | The SQL editor shows a horizontal scrollbar when the text is shorter than the view. | Capture `native-workspace.png` |
| W38 | Low | The row header column continues as an empty strip below the last row. | Capture `native-workspace.png` |
| W39 | Low | The `Data` toolbar stays in the header, disabled, on the other object pages. | `app/object_explorer.cpp:802` |
| W40 | Low | The reason for a blocked edit is a tooltip on the full grid. | `app/query_workspace.cpp:59` |
| W41 | Low | The Refresh button of the navigator shows only on hover. The Add button always shows. | `app/main_window_ui.cpp:403` |
| W42 | Low | The Window menu follows the macOS pattern on all platforms. | `app/main_window_ui.cpp:123` |
| W43 | Low | The layout state does not keep the navigator tab or the result column widths. | `app/appearance_controller.cpp:139` |
| W45 | Medium | The ERD has no zoom control and no hint on the screen. Only the accessible description gives the keys. | `widgets/object_erd_widget.cpp:43` |
| W46 | Medium | The keyboard hint component has no use in ChoscorDB. No screen shows a key hint, and quick search shows none. | `design_system/kbd/kbd_style_sheet.qss` |
| W47 | Low | The SQL editor does not mark the current line. Its fold marks use the boxed tree style of old editors. | `widgets/sql_editor/sql_editor.cpp:43` |
| W48 | Low | `Updates` is a menu in the menu bar with update commands only. The common location is the Help menu. | `app/updater_windows_linux.cpp:148`, `app/updater_sparkle.mm:69` |
| W44 | Low | The `+` button of the tab bar moves in a deferred call. After new tabs open, it can cover a tab for one frame. | Capture `native-navigator-sql.png`, `design_system/tabs/tab_add_corner.h:69` |

## Findings: text and terms

| ID | Severity | Finding | Evidence |
| --- | --- | --- | --- |
| T1 | Medium | Menus mix sentence case and title case. `Close tab` and `Close Others` are in the same window. | `app/main_window_ui.cpp:84`, `app/main_window_ui.cpp:612` |
| T2 | Medium | Diagnostics and the updater use title case. All other dialogs use sentence case. | `app/main_window_diagnostics.cpp:220`, `app/updater_windows_linux.cpp:149` |
| T3 | Medium | One dialog uses the words "connection" and "profile" for the same thing: `New connection` and `Save profile`. | `widgets/profile_dialog/profile_dialog.cpp:70`, `widgets/profile_dialog/profile_dialog.cpp:352` |
| T4 | Medium | One feature has two names: `Quick switch…` and `Quick search`. | `app/main_window_ui.cpp:186`, `design_system/quick_search/quick_search_dialog.cpp:124` |
| T5 | Medium | The status line shows engine terms: `0 KiB visible`, `1 rows` and `handle is stale`. | Captures `native-workspace.png`, `unified-object-1280.png` |
| T6 | Low | One panel has two names: `Recent history` and `Query history`. One list has two names: `Saved queries` and `Saved SQL files`. | `app/main_window_ui.cpp:179`, `app/main_window_ui.cpp:244` |
| T7 | Low | Command labels change between screens: `Export` and `Export…`, `Apply` and `Apply…`, `Delete rows` and `Delete selected`. | `app/main_window_ui.cpp:725`, `app/object_data_workspace.cpp:127` |
| T8 | Low | The button that opens a file dialog is `Browse…` in two dialogs and `Choose file…` in one. | `widgets/profile_dialog/profile_dialog.cpp:161`, `widgets/profile_dialog/ssh_host_key_dialog.cpp:64` |
| T9 | Low | Error text has three forms: `Could not …`, `Unable to …` and `… failed`. | `widgets/export_dialog/export_dialog.cpp:235`, `widgets/value_detail_dialog/value_detail_dialog.cpp:235` |
| T10 | Low | Tooltips mix forms. Some are sentences with a period, and others are short labels. | `widgets/profile_dialog/ssh_host_key_dialog.cpp:61`, `app/main_window_ui.cpp:224` |
| T11 | Low | Status text uses three separators: " — ", " · " and ": ". | `app/query_workspace.cpp:681`, `app/object_explorer.cpp:569` |
| T12 | Low | Toast titles mix generic words (`Success`, `Error`) with specific titles (`SQL generated`). | `app/main_window.cpp:39`, `app/main_window_navigator.cpp:262` |
| T13 | Low | The dialog uses the word "ZIP". The term list uses "diagnostics bundle". | `app/main_window_diagnostics.cpp:265`, [Style guide](style-guide.md) |
| T15 | Low | Progress text shows a raw byte count, for example `%1 bytes`. Other text uses KiB. | `app/query_workspace_json.cpp:386`, `widgets/export_dialog/export_dialog.cpp:300` |
| T14 | Low | The words "session" and "settings" are in UI text where the term list does not permit them. | `app/main_window_navigator.cpp:192`, `widgets/query_settings_dialog/query_settings_dialog.cpp:18` |

## Deviations from the design system page

These findings show that the code and [Design system](design-system.md) do not agree.
Correct the code or the page.

| Finding | The page says | The code does |
| --- | --- | --- |
| S5 | History rows and connection profile rows use `selection`. | They use `surface` or `sidebar-selection` with an outline. |
| S3, S14 | A 2px ring shows only for keyboard focus. | Buttons use 1px. Some rows show the ring after a click. |
| S4 | Checked buttons use `selection`. | Stock buttons use `primary`, and tool buttons use `surface-raised`. |
| S12 | Motion is 150 ms and 100 ms. | One animation uses 180 ms. Nothing uses the tokens. |
| V3 | Placeholders use `fg-muted` at 4.5:1. | Placeholders have about 3.0:1. |
| V7 | Text selection uses `selection`. | Fields use `primary`. |
| V9, V10 | The radio button is 16px. Icons are 12px or 16px. | The radio button is 18px. The tree uses 14px icons. |
| V17 | Each value has one name. | `ring` and `code-string` are equal to `primary`. |
| D1, W5 | One outcome shows in one location. | A success or a failure can show in two or three locations. |
| V21 | Rows are 28px. | Menu rows and select rows are 29px. History rows are about 64px. |

## Priority

Do the work in this order.
Each group removes a class of defects, not one defect.

1. Make states visible.
   Add hover and pressed tokens for neutral surfaces, with a clear step from the surface color.
   Then apply the motion tokens and make the `Loader` icon turn.
   This step corrects S1, S2, S6, S7, S12 and S13.
2. Correct contrast.
   Remove the `PlaceholderText` role from `showPanelState`.
   Make the field border darker, and make the disabled look neutral.
   This step corrects V1, V2, V3, V4, S9 and S10.
3. Use one focus ring and one selection look.
   Use a 2px ring for keyboard focus on each control and row.
   This step corrects S3, S4, S5 and S14.
4. Write one dialog contract.
   Put the footer order, the button variants and the label position in one helper.
   Put the close prompt and the feedback location in the same helper.
   Add the contract to [Design system](design-system.md).
   This step corrects D1 to D19.
5. Give each event one location.
   Add a `Results` and `Messages` switch.
   Move the toast region above the status line.
   This step corrects W4, W5, W6 and W7.
6. Show the hidden commands.
   Show Cancel while a query runs.
   Give SQL results the edit, sort and filter controls of the object `Data` page.
   This step corrects W1, W2, W3 and W12.
7. Use one label for one command.
   Take each label, shortcut and tooltip from one command table.
   This step corrects W8 to W11 and T1 to T7.
8. Complete the tabs, the navigator and the overlays.
   This step corrects W19, W20, W21, W31, V11, V12 and V13.

## Checks to add

No quality gate reports the defects in this page.
The checks in this table are proposals and do not exist.
The state probe of this audit is a model for the first three checks.
When a group is corrected, add a check so that the defect cannot come back.

| Defect class | Check |
| --- | --- |
| Contrast of a rendered text (V1, V3) | A desktop test that reads the pixel colors of a panel state and a placeholder and calculates the ratio. |
| Token contrast (V2, S1) | A design system test that requires a minimum ratio for `border`, hover and selection steps. |
| Missing state (S6, S7) | A gallery test that requires a different pixel result for rest, hover and pressed. |
| Unused motion (S12) | A `ui_policy.py` rule that rejects `setDuration` with a literal. |
| Dialog contract (D2 to D12) | A `ui_consistency.py` rule for the footer order, the variants and the mnemonics of each dialog. |
| Label drift (T1, T7, W8) | A `ui_policy.py` rule that rejects title case in `tr()` text and two labels for one command ID. |
| Shortcut catalog (W29, W30) | A desktop test that requires each `QAction` with a key to be in the catalog. |
| C++ empty-state text (W22) | A `cpp_ownership.py` rule for a `tr()` literal in `createPanelStateText`. |

## Resolution

This table gives the status of each finding after the audit.
"Fixed" means that the code changed. "Decision" means that `docs/design-system.md` records the current behavior as a Kumo rule.
Each decision row keeps a Kumo value or behavior. A code change for these rows would break the Kumo design system that the desktop app uses.

| ID | Status | Resolution |
| --- | --- | --- |
| S1 | Decision | Kumo `kumo-tint` hover. `stateStepsKeepTheirMinimumContrast` sets the floor. |
| S2 | Fixed | Banner surfaces use `kumo-fill` hover. Test: `bannerButtonsShowAVisibleHoverFillUnderThePointer`. |
| S3 | Fixed | One 2px ring from `focusSpec`. Test: `keyboardFocusShowsTheRingOnRowsCellsTabsAndToolButtons`. |
| S4 | Fixed | Checked controls use `kumo-tint`. Test: `checkedControlsShareTheKumoTint`. |
| S5 | Fixed | All navigator rows use `kumo-tint` with no outline. A pixel scan across each selected row finds no outline color. Tests: `sidebarTreeSelectionHasRoundedGrayFill`, `selectedConnectionRowUsesNeutralColors` and `selectedHistoryRowUsesSidebarSelection`. |
| S6 | Decision | Kumo has no pressed fill. See the state model. |
| S7 | Decision | Kumo fields, checks and headers have no hover. See the field and table rules. |
| S8 | Fixed | Painted controls fade to 50%. Stylesheet controls use `text-kumo-inactive`. Test: `disabledControlsFollowTheDocumentedDisabledRule`. |
| S9 | Decision | A disabled button has 50% opacity, as in Kumo. |
| S10 | Fixed | Read-only and disabled fields have their own looks. Test: `fieldsExposeInvalidBorderAndPreserveReadOnlyAndPopupInput`. |
| S11 | Fixed | Disabled rows and check labels use `text-kumo-inactive`. Test: `disabledRowsAndCheckLabelsUseTheInactiveText`. |
| S12 | Fixed | `motionSpec` gives the duration and the easing curve. Test: `motionEasingDrivesTheButtonHoverAndTheToastFade`. Gate: literal `setDuration`. |
| S13 | Fixed | `design::Spinner` turns. Test: `spinnerPaintsTheKumoLoaderTrackRing`. |
| S14 | Fixed | Rows and stock buttons use the keyboard focus rule. Test: `rowDelegatesShowTheRingOnlyAfterKeyboardFocus`. |
| S15 | Decision | A checked button keeps `kumo-tint` on hover. A checked Default or Destructive button keeps its emphasis fill. See the state model. |
| S16 | Fixed | Menus, selects and completion use `kumo-tint`. Test: `rowsAndMenusShareOneSelectionAndHoverModel`. |
| S17 | Fixed | The tab close mark is the Phosphor `x` icon. Test: `tabCloseMarkIsThePhosphorCloseIcon`. |
| S18 | Fixed | The Link button and the foreign-key cell icon show a hand cursor. |
| S19 | Fixed | Tree hover has the `:!selected` guard. |
| S20 | Decision | Kumo has no inactive selection. See the state model. |
| S21 | Fixed | Check, radio and switch show the ring. Test: `choiceControlsShowTheRingAfterTabFocus`. |
| S22 | Fixed | Tool buttons, tabs, rows and cells show the ring. Test: `keyboardFocusShowsTheRingOnRowsCellsTabsAndToolButtons`. |
| S23 | Fixed | Disabled trees and lists use `text-kumo-inactive`. |
| S24 | Decision | Hover and selection share `kumo-tint`, as in Kumo. |
| S25 | Decision | A hovered selected row stays `kumo-tint`, as in Kumo. |
| S26 | Decision | Headers have no hover fill. The sort mark is the `IconSmall` arrow. |
| S27 | Fixed | ERD boxes have hover, pressed and selected looks. Test: `erdTableBoxShowsHoverPressedAndSelectedLooks`. |
| V1 | Fixed | `showPanelState` sets no placeholder role. Test: `renderedPanelStateKeepsMutedContrastAndPlaceholderUsesKumo`. |
| V2 | Decision | `kumo-line` is the Kumo hairline. See the contrast notes. |
| V3 | Decision | `text-kumo-placeholder` is the Kumo value. See the contrast notes. |
| V4 | Decision | The switch uses the Kumo palette. |
| V5 | Fixed | An invalid field has a danger ring. |
| V6 | Decision | The badge passes. Test: `kumoTextPairsThatPassKeepTheirContrast`. The emphasis gradient is the Kumo value. See the contrast notes. |
| V7 | Fixed | Text selection uses `kumo-tint`. Test: `scopedDarkButtonsAndTextSelectionUseColors`. |
| V8 | Fixed | Values are weight 400. Labels are weight 500. Test: `formLabelsUseTheKumoLabel`. |
| V9 | Fixed | The radio button is `Dimension::Checkbox` (16). |
| V10 | Fixed | Tree and tool button icons use icon tokens. |
| V11 | Fixed | All tooltips use the painted surface. Test: `itemTooltipsUseSharedSurface`. |
| V12 | Fixed | The select popup uses the menu fill. Test: `fieldsUseTheKumoInputSurface`. |
| V13 | Fixed | Completion uses the shared popover. Test: `suggestionsShowKindIconsInTheSharedPopoverLook`. |
| V14 | Fixed | Select popups, tooltips and toasts have shadows. The test reads the shadow pixels below each surface. Test: `selectPopupsTooltipsAndToastsCastAShadow`. |
| V15 | Fixed | Sizes come from `metrics/`. |
| V16 | Fixed | `@space-3` replaces the literal 12px. |
| V17 | Fixed | The aliases are removed. |
| V18 | Decision | The grays are the Kumo grays. |
| V19 | Fixed | Each value has a caller. Gate: unused design values. |
| V20 | Fixed | Select rows have one definition. |
| V21 | Decision | Rows are 34, menu rows are 33 and history rows are two rows. |
| V22 | Fixed | Seven families show their states. Test: `interactionStatesShowEachFamilyInBothThemes`. |
| G1 | Fixed | The gallery uses `design::Button`. Gate: stock button in the gallery. |
| G2 | Fixed | The gallery uses size tokens. Gate: literal metric, including `resizeSection`. |
| G3 | Fixed | The `Source` column stretches and elides. |
| D1 | Fixed | The SSH key success shows in a toast. Test: `sshHostKeyReviewRequiresSelectionAndShowsExactFingerprint`. |
| D2 | Fixed | One Preferences dialog saves and closes. |
| D3 | Fixed | `Cancel` drops a draft. `Close` closes a dialog with no draft. |
| D4 | Fixed | `DialogShell::reject` asks before it drops a draft. Test: `connectionDraftUsesTheSharedDiscardPrompt`. |
| D5 | Fixed | `addAction` sets the footer order. Test: `dialogFooterPlacesEachRoleInOneOrderWithOneVariant`. |
| D6 | Fixed | `Restore defaults` is a Utility action. |
| D7 | Fixed | Clear actions are destructive. |
| D8 | Fixed | Approve and Retry do not show together. |
| D9 | Fixed | Confirmations use verbs. Gate: dialog decision outside the dialog contract. |
| D10 | Fixed | Buttons are verbs. Gate: dialog decision outside the dialog contract. |
| D11 | Fixed | The footer sets the default button. |
| D12 | Fixed | Labels are at the left with no colon. |
| D13 | Fixed | `addFormRow` gives each label a different mnemonic. |
| D14 | Fixed | Dialogs use a status line with roles. A cancelled export is neutral. Tests: `diagnosticsExportLetsUserChooseZipDestination` and `exportCancellationKeepsNeutralLocalFeedback`. |
| D15 | Fixed | Dialogs validate on the click. Tests: the SSH key review and `diagnosticsExportLetsUserChooseZipDestination`. |
| D16 | Fixed | Busy work shows in the dialog status line. |
| D17 | Fixed | `FieldNeed::Optional` adds `(optional)`. |
| D18 | Fixed | The title is `New connection` or `Edit connection`. |
| D19 | Fixed | Restore and reset tell the user to save. |
| D20 | Fixed | Theme is the first row. Retry shows only after a failure. |
| D21 | Fixed | The status text starts at the line inset. Test: `dialogStatusTextStartsAtTheLineInsetWithoutASpinnerGap`. |
| D22 | Fixed | Headers are at the left, and the value column fills the view. |
| D23 | Fixed | The port has no arrows, and labels are centered. |
| D24 | Fixed | Spin boxes use the control height. |
| D25 | Fixed | Each dialog uses a `DialogSize` token. |
| D26 | Fixed | `DialogSections` sets the Kumo padding and gap. |
| D27 | Fixed | Only the modal Preferences dialog edits the values. |
| D28 | Decision | Pagers show icons. Item navigation shows icons and text. Dialog footer buttons show text only, with no icon. See `Previous and next`. |
| D29 | Fixed | `Clear history…` uses the `Trash` icon. |
| D30 | Fixed | The sheet close button names its sheet. Test: `rightSheetEdgeIsTheKumoLine`. |
| D31 | Fixed | `RightSheet` uses `DialogSections`. |
| D32 | Fixed | Both sheets use a status line. |
| D33 | Fixed | The title is `Edit cell`. The primary is `Set value`. |
| W1 | Fixed | `Cancel query` shows while a query runs. It has `Ctrl+.`. |
| W2 | Fixed | SQL results have the grid edit toolbar. |
| W3 | Fixed | Every grid sorts and filters. |
| W4 | Fixed | File errors use `reportError`. See `Event locations`. Test: `editorFileErrorsUseTheSharedErrorFeedback`. |
| W5 | Fixed | The failure text shows only in Messages. Test: `a_failed_query_is_a_result_in_the_messages_pane_without_a_toast`. |
| W6 | Fixed | The toast moves above the status line. |
| W7 | Fixed | The sentence shows one time, with a Reconnect button. |
| W8 | Fixed | One `Refresh` command per panel. See `Refresh`. |
| W9 | Fixed | A failed row shows `Refresh` one time. |
| W10 | Fixed | The labels are `Run` and `Run all`. Test: `eachWindowCommandHasOneLabel`. |
| W11 | Fixed | The `+` button starts `New connection…`. |
| W12 | Fixed | The grid buttons are visible. |
| W13 | Fixed | A pinned object has the full menu. |
| W14 | Fixed | Dialog commands end with `…`. A separator isolates `Drop…`. |
| W15 | Fixed | The grid menu says `Cancel query`. |
| W16 | Fixed | The editor menu has Run, Find and Complete. |
| W17 | Fixed | Headers, history and saved queries have menus. |
| W18 | Fixed | All toolbars use Outline buttons and one separator component. Test: `gridCommandsHaveOneLabelInBothToolbarsAndTheCommandTable`. |
| W19 | Fixed | Tabs have a title tooltip and close on a middle click. |
| W20 | Fixed | The dirty mark stays visible. Object tabs show staged edits. |
| W21 | Decision | Sidebar tabs show icons. The arrow keys move between them. Test: `historyNavigationStaysInSidebar`. |
| W22 | Fixed | Empty-state text comes from Rust. Gate: `panel-state-literal`. |
| W23 | Decision | See `Loading`. |
| W24 | Fixed | All filters wait for the Rust delay and have a clear action. Test: `sidebarFiltersWaitForTypingAndHaveAClearButton`. |
| W25 | Fixed | Navigator rows have a name tooltip. |
| W26 | Fixed | The navigator opens an object on a click or Return. |
| W27 | Fixed | All grids have stripes and no grid lines. |
| W28 | Fixed | Edited cells have an edge bar. `NULL` uses `Callout`. |
| W29 | Fixed | Grid keys use `Ctrl+Shift`. |
| W30 | Fixed | Each keyed command is in the catalog. Test: `everyWindowCommandWithAKeyIsInTheShortcutCatalog`. |
| W31 | Fixed | `Show sidebar` hides the navigator. |
| W32 | Fixed | The splitter panes do not collapse. |
| W33 | Fixed | The View menu has groups. No menu bar command keeps the Qt text heuristic role, so macOS moves only the About, Preferences and Quit commands. Test: `applicationMenusExposeHelpAndAbout`. |
| W34 | Fixed | The find panel is below the editor. `Replace all` is Outline. |
| W35 | Fixed | A success status line is neutral. |
| W36 | Fixed | Timers, the startup poll and the diagnostics watchdog read Rust intervals. Gate: literal timer interval (`setInterval`, `start`, `singleShot` and `sleep_for`). |
| W37 | Fixed | The editor scroll width follows the text. |
| W38 | Fixed | The row header ends at the last row. |
| W39 | Fixed | The Data toolbar hides on other panes. |
| W40 | Fixed | A visible label gives the reason for a blocked edit. |
| W41 | Fixed | The navigator Refresh button always shows. |
| W42 | Fixed | The Window menu is only on macOS. |
| W43 | Fixed | The layout keeps the sidebar tab and the column widths. |
| W44 | Fixed | The `+` button moves in the same call. |
| W45 | Fixed | The ERD has zoom controls and key hints. |
| W46 | Fixed | Quick search and the ERD show key hints. |
| W47 | Fixed | The editor marks the current line and uses plain fold marks. |
| W48 | Fixed | The update commands are in the Help menu. |
| T1 | Fixed | Menus use sentence case. Gate: title case label. |
| T2 | Fixed | All dialogs use sentence case. Gate: title case label. |
| T3 | Fixed | UI text says "connection". Gate: term rule for C++ and Rust text. |
| T4 | Fixed | The feature is `Quick search`. |
| T5 | Fixed | The status line uses `1 row` and sizes. The stale-handle text is a sentence. |
| T6 | Fixed | The panels are `Query history` and `Saved queries`. |
| T7 | Fixed | Both toolbars use the same labels. |
| T8 | Fixed | All file buttons are `Browse…`. |
| T9 | Fixed | Errors start with `Could not`. Gate: error sentence form for C++ and Rust text. |
| T10 | Fixed | Labels have no period. Sentences end with one. Gate: tooltip sentence. |
| T11 | Fixed | Status text uses ` · ` or `: `. Gate: em dash separator. |
| T12 | Decision | A success title names the result. A warning or error title is the severity. See `Toast title`. |
| T13 | Fixed | The dialog says "diagnostics bundle". |
| T14 | Fixed | UI text uses "connection" and "preferences". Gate: term rule for C++ and Rust text. |
| T15 | Fixed | Progress text uses formatted sizes. |

### Checks

| Defect class | Check |
| --- | --- |
| Contrast of a rendered text (V1, V3) | `renderedPanelStateKeepsMutedContrastAndPlaceholderUsesKumo` |
| Token contrast (V2, S1) | `stateStepsKeepTheirMinimumContrast` |
| Missing state (S6, S7) | `interactionStatesShowEachFamilyInBothThemes` and `everyButtonFamilyShowsFeedbackUnderThePointer` |
| Unused motion (S12) | `ui_policy.py` literal `setDuration` rule |
| Dialog contract (D2 to D12) | `dialogFooterPlacesEachRoleInOneOrderWithOneVariant` and the `ui_policy.py` dialog rules |
| Label drift (T1, T7, W8) | `ui_policy.py` title case rule, `eachWindowCommandHasOneLabel` (one label per command ID and per shortcut) and `gridCommandsHaveOneLabelInBothToolbarsAndTheCommandTable` |
| Shortcut catalog (W29, W30) | `everyWindowCommandWithAKeyIsInTheShortcutCatalog` |
| C++ empty-state text (W22) | `cpp_ownership.py` `panel-state-literal` rule |
