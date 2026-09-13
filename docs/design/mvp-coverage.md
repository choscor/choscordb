# MVP surface and state coverage

The [MVP flow specification](../specs/2026-09-13-mvp-ui-flows.md) is authoritative.
This inventory carries forward production component seams, superseding the
historical Nova visual mapping. Line numbers inherited below are baseline
navigation hints; source paths and named gallery IDs identify the seams.
Fixtures are synthetic, database-free and never persist user settings.

Each specimen supports Light/Dark and applicable input states. Deterministic
exports supplement actual interactive controls; screenshots do not establish
focus, accessibility or flow acceptance. The compact UI and full-size captures
must be checked at 1280×900 and 960×640 before requesting checkpoint 1.

| Gallery ID | App-owned surface and source | Shared mapping / applicable states |
| --- | --- | --- |
| `tokens` | `desktop/design_system/theme.h`, `theme.cpp` | Every semantic color light/dark; spacing, sizes, radius, border, elevation, focus, motion; copyable values and source |
| `typography` | `theme.cpp`; all QLabel text | Body, label, description, heading, muted/help/error text, keyboard hints; Unicode, long labels, narrow widths, selection/copy where offered |
| `icons` | `desktop/design_system/icons.*`, `desktop/resources/icons/` | Named Lucide assets, 12/14/16/20/24; semantic light/dark colors, disabled, text alignment |
| `buttons` | `main_window.cpp:247`, `:405`; all dialog action rows | Default, secondary, outline, ghost, destructive, link × meaningful text/icon sizes; normal, hover, pressed, keyboard focus, disabled, loading; long/Unicode labels |
| `tool-buttons` | `main_window.cpp:290`, `:322` | QToolBar action button, overflow QToolButton, menu indicator; icon-only, text/icon, checked, disabled, focus, pressed, expanded |
| `button-groups` | `search_panel.cpp:26`, `main_window.cpp:422` | Adjacent query/search/paging actions; shared joins and separators, individual keyboard access |
| `fields` | `profile_dialog.cpp:40`, `preferences_dialog.cpp:82`, `export_dialog.cpp:28` | QLineEdit text/password, placeholder, selected text, focus, disabled, read-only, invalid; labels, field groups, adjacent help/error |
| `numeric-fields` | `profile_dialog.cpp:78`, `query_settings_dialog.cpp:21`, `preferences_dialog.cpp:114` | QSpinBox field and up/down subcontrols; min/max, keyboard, disabled, invalid; accessible field labels |
| `textareas` | `main_window.cpp:437`, `history_dock.cpp:73` | QPlainTextEdit editable/read-only, selection, scrollbars, placeholder, diagnostics; bounded content |
| `selects` | `main_window.cpp:292`, `:312`, `profile_dialog.cpp:50`, `export_dialog.cpp:27` | QComboBox trigger, arrow, actual popup rows; selected/hover/focus/disabled, long values, constrained popup placement |
| `checks-toggles` | `profile_dialog.cpp:68`, `:89`, `search_panel.cpp:29`, `history_dock.cpp:28` | QCheckBox unchecked/checked/mixed, disabled, keyboard focus; checkable toolbar/toggle actions where used |
| `editor-preferences` | `preferences_dialog.cpp:110` | QFontComboBox preview/popup, font size, system-monospace checkbox; SQL preview maintains editor settings |
| `shortcuts` | `preferences_dialog.cpp:132`, `models/shortcut_catalog.*` | QKeySequenceEdit empty/recording/value/conflict/help; modifier combinations, focus and clear behavior |
| `lists-navigation` | `preferences_dialog.cpp:45`, `profile_dialog.cpp:32` | QListWidget rows; selected/hover/focus/disabled, empty, long/Unicode item text |
| `tabs` | `main_window.cpp:360`, `:394` | QTabWidget/QTabBar tab, active/inactive, dirty/close affordance, overflow menu, constrained width, keyboard focus |
| `sidebar-tree` | `main_window.cpp`, `:271`, `models/navigator_model.*` | Sidebar header/filter, connection/database/schema/table/column rows, expand indicators, selected/hover/focus, loading/error/disconnected, empty filters |
| `scrolling` | `preferences_dialog.cpp:127`; all editor/model views | QScrollArea and vertical/horizontal scrollbar groove/thumb/arrows; hover/drag/disabled, overflow and DPI |
| `separators-splitters` | `main_window.cpp:359`, dialog/form layouts | QSplitter handle, dock separators, horizontal/vertical divider; drag hit area, keyboard where supported |
| `tables` | `main_window.cpp:417`, `history_dock.cpp:45`, `value_detail_dialog.cpp:26` | QTableView horizontal/vertical headers, sort/resize, corners, cells/selection/focus, grid, scrollbars; same delegate behavior in gallery |
| `paging` | `main_window.cpp:422`, `history_dock.cpp:78`, `value_detail_dialog.cpp:35` | Previous/next buttons, count/range labels, `paging`: known first/last/disabled; `paging-unknown`: unknown total with known availability; `results-loading`: empty loading table |
| `dialogs` | `widgets/dialog_shell.*`, `preferences_dialog.cpp:37`, `profile_dialog.cpp:21`, `query_settings_dialog.cpp:17` | Actual modal panel, heading/description/form/footer, dim parent, keyboard containment/restoration, Escape/backdrop cancellation |
| `nonmodal` | `export_dialog.cpp:32`, `value_detail_dialog.cpp:28` | Large-value detail remains nonmodal; Export moves to the modal composition after checkpoint 1; matching content, live theme updates |
| `confirmations` | `query_workspace.cpp:253`, `:344`, `:573`; `main_window.cpp:510`, `:632`, `:720`, `:873`, `:918`; `profile_dialog.cpp:219`, `:468`; `history_dock.cpp:139`; `export_dialog.cpp:184` | Actual QMessageBox/app confirmation surfaces: SQL execution, disconnect/close, discard, recovery/history retry, I/O errors, delete profile/history, replace export; safe default and rejection paths |
| `menus` | `main_window.cpp:328`, `:377`, `query_workspace.cpp:178`; SQL editor context menu | Actual application QMenu dropdown/context popup, item/icon/shortcut/check/submenu/separator; hover, disabled, focus, dismissal/placement |
| `completion` | `editor_completion.cpp:15` | Actual QCompleter popup `sqlCompletionPopup`, candidate kind/detail rows, selected/keyboard navigation, loading/empty; never engine calls in gallery |
| `tooltip-popover` | Widget help/tooltips; `editor_completion.cpp` | Actual QToolTip and app popup surface, text wrapping, semantic popover colors, placement/dismissal |
| `feedback` | `toast_region.cpp:5`, `dialog_shell.cpp:25`, `main_window.cpp:397` | Toast, inline status, badge/status icon; empty/loading/progress/success/warning/error/cancelled/disconnected; adjacent field validation and detailed diagnostics |
| `connection-form` | `profile_dialog.cpp:21` | `connection-form` / `connection-sqlite`: switchable PostgreSQL/SQLite fields, retained drafts, password/credential checkbox, TLS select and validation. Test/Save/Connect/delete backend outcomes are screen-specific and deferred; shared success/error/loading presentations are in `feedback` and `buttons` |
| `sql-editor` | `sql_editor.cpp:41`, `search_panel.cpp:17` | Real SqlEditor chrome, line numbers, selection/caret, syntax, find/replace panel and validation, read-only, loading, bounded document fixtures |
| `query-controls` | `main_window.cpp:290`, `query_workspace.cpp` | Connection select, run/cancel, mode, commit/rollback, overflow; `query-controls`: ready/running; `query-cancelling`: pending cancellation with disabled Run/Cancel until manual fixture acknowledgment; transaction controls are local. Disconnected rendering is in `feedback`; actual guards/transactions await screen migration |
| `results` | `models/result_table_model.*`, `models/value_preview_model.*` | Real ResultTableModel: headers, typed cells, NULL versus empty, large-value placeholders; `results-loading` and `results-error` provide empty pending/failure tables and manual completion/retry; `value-window` uses ValuePreviewModel for bounded text/binary chunks |
| `messages-summary` | `main_window.cpp:397`, `:437`, `query_workspace.cpp` | Execution summary and messages for success/error/cancel/loading/disconnected, row/time counts, honest unknowns |
| `history` | `history_dock.cpp:24`, `models/history_model.*` | Record toggle, clear/refresh, history table, SQL preview notice and earlier/later text, page range, open-query action, empty/loading/error |
| `recovery` | `main_window.cpp:584`, `:632`, `:720` | Retry/start-new inline notice; recovery/history close error confirmations; preserve explicit reset/retry and cancellation |
| `appearance-form` | `preferences_dialog.cpp:59`, `app/appearance_controller.*` | System/Light/Dark preview/apply/cancel, corruption/load/save retry/reset; legacy density/accent values remain inert |
| gallery | `desktop/tools/component_gallery.cpp` | Searchable navigation, independent light/dark panels, copy token/source, interactive examples, deterministic capture and visible export errors |
| `native-exceptions` | `main_window.cpp` native menubar/titlebar; `profile_dialog.cpp:134`, `main_window.cpp:522`, `:535`, `export_dialog.cpp:88` file pickers | OS title bars, OS menu bar, system file pickers only; app-owned menus/dialog contents remain covered |

## New compositions and behavioral boundaries

- Document tabs use the top green active indicator; object/settings tabs use the
  underline variant. Tab overflow and keyboard selection remain native controls.
- Modal backdrop, panel, form/footer, rejection and focus restoration are shared.
  Export/Preferences screen behavior is migrated only after component review.
- Status extensions include pending Cancelling, cancelled, unavailable metadata,
  retryable failure, disconnected, and unknown-total progress. Never synthesize a
  percentage for an unknown total in production.
- Start, SQL, Object and History screen composition, Quick switch and pinned
  bottom actions are post-checkpoint work, reviewed separately at checkpoint 2.
- Native exceptions are title bars, OS menu bars and system file pickers only.
  App-owned dropdown/context menus, popup content and dialog contents are covered.

## Evidence status

The inherited 36 specimens are extended with `query-cancelling`, `results-loading`, `results-error`, `paging-unknown`, `connection-sqlite` and `value-window`. Updated rendering and native capture
coverage are recorded in [implementation evidence](mvp-implementation-evidence.md).
The inventory is not a claim that every state has passed visual review.

## State evidence at this checkpoint

| State family | Inspectable evidence | Public input / behavior evidence |
| --- | --- | --- |
| Buttons, sizes, normal/hover/pressed/focus/disabled/loading | `buttons` grid in both themes, including exact editor Save/Run specimens | Components tests: real Space/click dispatch, loading suppression, focused-ring contrast, native/custom parity; glyph and geometry witnesses |
| Fields, selections, readonly, invalid, password and disabled | `fields`, `numeric-fields`, `textareas` | Control-style/preview tests: Unicode editing, dynamic invalid border, read-only preservation, spin actions |
| Select, font select and switch | `selects` captures the actual open popup; `editor-preferences`, `checks-toggles` | Native Cocoa input record; selector popup-content witness test; Space toggles actual checkbox switch; custom editor font remains independent |
| Navigation, tabs, menus and scroll | `sidebar-tree`, `lists-navigation`, `tabs`, `menus`, `scrolling` | Keyboard Down changes actual tree selection, document/pane active markers, actual menu activation/dismissal, gallery Tab scrolls focused action into view at960×640 |
| Pending cancellation and terminal acknowledgment | `query-cancelling` | Preview test triggers Run→Cancel; Run/Cancel remain disabled until explicit synthetic acknowledgment; no backend dispatch |
| Empty loading/error results and unknown totals | `results-loading`, `results-error`, `paging-unknown`, `feedback` | Public model starts with zero rows; retry/completion restores known literal NULL/empty rows and an unknown-total label |
| Large values | `results` placeholder plus `value-window` bounded text/binary model; `nonmodal` boundary | Existing ValuePreviewModel and workspace tests verify byte bounds and UTF-8; actual engine leases/invalidation remain production behavior, not simulated claims |
| Connection driver drafts | `connection-form`, `connection-sqlite` | Preview input switches drivers and retains SQLite path; production profile service outcomes remain post-checkpoint evidence |
| Modal/nonmodal, confirmations, theme and restoration | `dialogs`, `nonmodal`, `confirmations`, `tooltip-popover`, `completion` | Modal tests inspect real Tab containment/Escape/backdrop/rejection/focus restoration, blur/shadow, owner resize/theme; shutdown regression tests exercise hidden-owner confirmations |
| Feedback and omitted states | `feedback`, `messages-summary`, `recovery` | Explicit text for empty/loading/success/warning/error/cancelling/cancelled/disconnected/unavailable, unknown-total indicator and synthetic retry. Production recovery/storage outcomes remain in existing workflow suites |

[Native captures, actual exceptions and reproduction](mvp-native/README.md) and
[verification/review outcomes](mvp-implementation-evidence.md) complete this matrix.
Synthetic specimens demonstrate reusable rendering and input; they do not claim
that the D1–D5 screen migration or database workflows have been implemented.
