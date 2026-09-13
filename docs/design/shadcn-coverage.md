# Application surface inventory and gallery coverage

Inventory baseline: `288ce7f`, inspected 2026-09-12. Source paths below identify
production seams, not duplicate gallery implementations. Required gallery IDs
are the navigation/specimen contract. **Rendered does not mean user-approved.** The 36 specimens have runtime
exports in the [capture index](shadcn-reference/qt-capture-index.json), with
verification and remaining limits in the [evidence record](shadcn-gallery-evidence.md). Production screen migration requires the explicit
user gallery review in the implementation specification.

| Gallery ID | App-owned surface and source | Shared mapping / applicable states |
| --- | --- | --- |
| [tokens](shadcn-reference/qt-tokens.png) | `desktop/design_system/theme.h`, `theme.cpp` | Every semantic color light/dark; spacing, sizes, radius, border, elevation, focus, motion; copyable values and source |
| [typography](shadcn-reference/qt-typography.png) | `theme.cpp`; all QLabel text | Body, label, description, heading, muted/help/error text, keyboard hints; Unicode, long labels, narrow widths, selection/copy where offered |
| [icons](shadcn-reference/qt-icons.png) | `desktop/design_system/icons.*`, `desktop/resources/icons/` | Named Lucide assets, 12/14/16/20/24; semantic light/dark colors, disabled, text alignment |
| [buttons](shadcn-reference/qt-buttons.png) | `main_window.cpp:247`, `:405`; all dialog action rows | Default, secondary, outline, ghost, destructive, link × meaningful text/icon sizes; normal, hover, pressed, keyboard focus, disabled, loading; long/Unicode labels |
| [tool-buttons](shadcn-reference/qt-tool-buttons.png) | `main_window.cpp:290`, `:322` | QToolBar action button, overflow QToolButton, menu indicator; icon-only, text/icon, checked, disabled, focus, pressed, expanded |
| [button-groups](shadcn-reference/qt-button-groups.png) | `search_panel.cpp:26`, `main_window.cpp:422` | Adjacent query/search/paging actions; shared joins and separators, individual keyboard access |
| [fields](shadcn-reference/qt-fields.png) | `profile_dialog.cpp:40`, `preferences_dialog.cpp:82`, `export_dialog.cpp:28` | QLineEdit text/password, placeholder, selected text, focus, disabled, read-only, invalid; labels, field groups, adjacent help/error |
| [numeric-fields](shadcn-reference/qt-numeric-fields.png) | `profile_dialog.cpp:78`, `query_settings_dialog.cpp:21`, `preferences_dialog.cpp:114` | QSpinBox field and up/down subcontrols; min/max, keyboard, disabled, invalid; accessible field labels |
| [textareas](shadcn-reference/qt-textareas.png) | `main_window.cpp:437`, `history_dock.cpp:73` | QPlainTextEdit editable/read-only, selection, scrollbars, placeholder, diagnostics; bounded content |
| [selects](shadcn-reference/qt-selects.png) | `main_window.cpp:292`, `:312`, `profile_dialog.cpp:50`, `export_dialog.cpp:27` | QComboBox trigger, arrow, actual popup rows; selected/hover/focus/disabled, long values, constrained popup placement |
| [checks-toggles](shadcn-reference/qt-checks-toggles.png) | `profile_dialog.cpp:68`, `:89`, `search_panel.cpp:29`, `history_dock.cpp:28` | QCheckBox unchecked/checked/mixed, disabled, keyboard focus; checkable toolbar/toggle actions where used |
| [editor-preferences](shadcn-reference/qt-editor-preferences.png) | `preferences_dialog.cpp:110` | QFontComboBox preview/popup, font size, system-monospace checkbox; SQL preview maintains editor settings |
| [shortcuts](shadcn-reference/qt-shortcuts.png) | `preferences_dialog.cpp:132`, `models/shortcut_catalog.*` | QKeySequenceEdit empty/recording/value/conflict/help; modifier combinations, focus and clear behavior |
| [lists-navigation](shadcn-reference/qt-lists-navigation.png) | `preferences_dialog.cpp:45`, `profile_dialog.cpp:32` | QListWidget rows; selected/hover/focus/disabled, empty, long/Unicode item text |
| [tabs](shadcn-reference/qt-tabs.png) | `main_window.cpp:360`, `:394` | QTabWidget/QTabBar tab, active/inactive, dirty/close affordance, overflow menu, constrained width, keyboard focus |
| [sidebar-tree](shadcn-reference/qt-sidebar-tree.png) | `main_window.cpp:238`, `:271`, `models/navigator_model.*` | Dock/sidebar header/filter, connection/database/schema/table/column rows, expand indicators, selected/hover/focus, loading/error/disconnected, empty filters |
| [scrolling](shadcn-reference/qt-scrolling.png) | `preferences_dialog.cpp:127`; all editor/model views | QScrollArea and vertical/horizontal scrollbar groove/thumb/arrows; hover/drag/disabled, overflow and DPI |
| [separators-splitters](shadcn-reference/qt-separators-splitters.png) | `main_window.cpp:359`, dialog/form layouts | QSplitter handle, dock separators, horizontal/vertical divider; drag hit area, keyboard where supported |
| [tables](shadcn-reference/qt-tables.png) | `main_window.cpp:417`, `history_dock.cpp:45`, `value_detail_dialog.cpp:26` | QTableView horizontal/vertical headers, sort/resize, corners, cells/selection/focus, grid, scrollbars; same delegate behavior in gallery |
| [paging](shadcn-reference/qt-paging.png) | `main_window.cpp:422`, `history_dock.cpp:78`, `value_detail_dialog.cpp:35` | Previous/next buttons, count/range labels, first/last/unknown-total/loading/disabled |
| [dialogs](shadcn-reference/qt-dialogs.png) | `widgets/dialog_shell.*`, `preferences_dialog.cpp:37`, `profile_dialog.cpp:21`, `query_settings_dialog.cpp:17` | Actual modal panel, heading/description/form/footer, dim parent, keyboard containment/restoration, Escape/backdrop cancellation |
| [nonmodal](shadcn-reference/qt-nonmodal.png) | `export_dialog.cpp:32`, `value_detail_dialog.cpp:28` | Existing modeless export/value-detail windows retain modality; matching content, live theme updates |
| [confirmations](shadcn-reference/qt-confirmations.png) | `query_workspace.cpp:253`, `:344`, `:573`; `main_window.cpp:510`, `:632`, `:720`, `:873`, `:918`; `profile_dialog.cpp:219`, `:468`; `history_dock.cpp:139`; `export_dialog.cpp:184` | Actual QMessageBox/app confirmation surfaces: SQL execution, disconnect/close, discard, recovery/history retry, I/O errors, delete profile/history, replace export; safe default and rejection paths |
| [menus](shadcn-reference/qt-menus.png) | `main_window.cpp:328`, `:377`, `query_workspace.cpp:178`; SQL editor context menu | Actual application QMenu dropdown/context popup, item/icon/shortcut/check/submenu/separator; hover, disabled, focus, dismissal/placement |
| [completion](shadcn-reference/qt-completion.png) | `editor_completion.cpp:15` | Actual QCompleter popup `sqlCompletionPopup`, candidate kind/detail rows, selected/keyboard navigation, loading/empty; never engine calls in gallery |
| [tooltip-popover](shadcn-reference/qt-tooltip-popover.png) | Widget help/tooltips; `editor_completion.cpp` | Actual QToolTip and app popup surface, text wrapping, semantic popover colors, placement/dismissal |
| [feedback](shadcn-reference/qt-feedback.png) | `toast_region.cpp:5`, `dialog_shell.cpp:25`, `main_window.cpp:397` | Toast, inline status, badge/status icon; empty/loading/progress/success/warning/error/cancelled/disconnected; adjacent field validation and detailed diagnostics |
| [connection-form](shadcn-reference/qt-connection-form.png) | `profile_dialog.cpp:21` | Synthetic profile list plus SQLite/Postgres fields, password/credential checkbox, TLS select, test/connect/save/delete statuses; no credential access |
| [sql-editor](shadcn-reference/qt-sql-editor.png) | `sql_editor.cpp:41`, `search_panel.cpp:17` | Real SqlEditor chrome, line numbers, selection/caret, syntax, find/replace panel and validation, read-only, loading, bounded document fixtures |
| [query-controls](shadcn-reference/qt-query-controls.png) | `main_window.cpp:290`, `query_workspace.cpp` | Connection select, run/cancel, mode, commit/rollback, overflow; ready/running/cancelled/transaction/disconnected states with synthetic fixture only |
| [results](shadcn-reference/qt-results.png) | `models/result_table_model.*`, `models/value_preview_model.*` | Real result/value models and views: headers, typed cells, NULL versus empty, large-value placeholders, pending/loading/error, bounded paging |
| [messages-summary](shadcn-reference/qt-messages-summary.png) | `main_window.cpp:397`, `:437`, `query_workspace.cpp` | Execution summary and messages for success/error/cancel/loading/disconnected, row/time counts, honest unknowns |
| [history](shadcn-reference/qt-history.png) | `history_dock.cpp:24`, `models/history_model.*` | Record toggle, clear/refresh, history table, SQL preview notice and earlier/later text, page range, open-query action, empty/loading/error |
| [recovery](shadcn-reference/qt-recovery.png) | `main_window.cpp:584`, `:632`, `:720` | Retry/start-new inline notice; recovery/history close error confirmations; preserve explicit reset/retry and cancellation |
| [appearance-form](shadcn-reference/qt-appearance-form.png) | `preferences_dialog.cpp:59`, `app/appearance_controller.*` | System/Light/Dark preview/apply/cancel, corruption/load/save retry/reset; obsolete density/accent controls removed only in gated migration |
| gallery | `desktop/tools/component_gallery.cpp` | Searchable navigation, independent light/dark panels, copy token/source, interactive examples, deterministic capture and visible export errors |
| [native-exceptions](shadcn-reference/qt-native-exceptions.png) | `main_window.cpp` native menubar/titlebar; `profile_dialog.cpp:134`, `main_window.cpp:522`, `:535`, `export_dialog.cpp:88` file pickers | OS title bars, OS menu bar, system file pickers only; app-owned menus/dialog contents remain covered |

## Evidence and review gate

Each row requires an actual gallery specimen using production rendering, applicable
state checks, and reference/Qt comparison where applicable. Sections are Tokens,
Typography, Icons, Components, Compositions, Database UI. Synthetic database
fixtures must not connect, load credentials, mutate settings, or run queries.
State forcing must use production rendering without altering production behavior.

Record exact executable command, isolated settings environment, capture size,
DPR, font, theme, state and output path with each capture. Offscreen Qt tests
establish behavior; native keyboard/focus/accessibility and visual review remain
separate evidence. Test passing and coverage rows are not substitutes for the
user's required gallery acceptance.

Initial reference evidence is linked in [shadcn-reference.md](shadcn-reference.md).
The user approved the gallery on 2026-09-13. See the
[production migration evidence](shadcn-implementation-evidence.md) for subsequent
screen adoption, workflow captures and verification.
