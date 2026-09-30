# Bottom right toast inventory

Source inventory for the current working tree on 2026-10-01. This lists application screens, every direct toast display call, visual variants, persistence modes, and forwarded message data. It is an inspection record, not an implementation specification. No application behavior was changed.

The display-call tables retain exact C++ expressions, including adjacent string literals that concatenate at compile time. The English strings below are source templates. `tr()` permits translation. `%1`, `%2`, and other placeholders are filled at runtime. Raw backend errors are open-ended: database vendors, operating systems, storage, and network services can return text that cannot be exhaustively enumerated as a fixed list. Their forwarding boundaries and transformations are listed here.

## Visual variants and lifecycle

| Visual state | API and property | Default title | Behavior |
| --- | --- | --- | --- |
| Success | `ToastVariant::Success`, `variant=success` | `Success` through MainWindow | Success theme background and foreground; ordinary notice defaults to 5000 ms. |
| Warning | `ToastVariant::Warning`, `variant=warning` | `Warning` through MainWindow | Warning theme surface; ordinary notice defaults to 5000 ms. |
| Danger | `ToastVariant::Danger`, `variant=danger` | `Error` through MainWindow | Danger theme surface; ordinary notice defaults to 5000 ms. |
| Progress | `showProgress`, `variant=progress` | Feature-specific | Warning theme surface plus an indeterminate progress bar, range 0 to 0. No automatic expiry or percentage. |

Progress is a fourth rendering state, not a fourth `ToastVariant` enum value. Pinned and persistent are lifecycle modes, not additional visual variants.

| Mode | Rule |
| --- | --- |
| Ordinary timed | `showToast(..., durationMs=5000)`; next ordinary notice replaces the current one. |
| Persistent ordinary | `showToast(..., 0)` or a negative duration; no automatic expiry, but another ordinary notice can replace it. Used for query-settings errors and appearance warnings. |
| Pinned | `showPinnedToast`; remains in front until cleared or dismissed. Used for workspace recovery. There is one pending notice slot, not a FIFO queue: later notices overwrite that slot. Clearing or dismissing the pinned notice displays the pending notice if present. |
| Progress | Updated while work runs; cleared by the feature or replaced by a terminal notice. Can be manually dismissed. |

## Placement and shared code

- `ToastRegion::placeOverlay()` anchors to the bottom-right of its attached host with 16 px inset; width is at most 320 px and adapts to the host width. Resize events reposition it.
- `windowToast(context)` resolves the owning window, following parent dialogs upward. It shares a direct child named `toastRegion`; an unparented dialog uses its own window. Embedded modal ownership is tracked by `embeddedPopupOwner`.
- `progressToast(host)` creates or reuses a direct child named `progressToast` attached to that exact feature host. It can therefore appear at a panel/dialog/object-data corner rather than the whole-window corner.
- MainWindow constructs and attaches its shared region in `desktop/app/main_window_ui.cpp:916`. `MainWindow::showToast` maps severity to Success/Warning/Error titles in `desktop/app/main_window.cpp:47`.
- Title and body are HTML-escaped before rich-text rendering. Title is bold, followed by the body. Accessibility description is `title + ". " + body` (progress omits the separator when detail is empty); an accessibility Alert is announced on display.
- Every notice has a close button named `toastDismiss`, accessible name and tooltip `Dismiss notification`. Region accessible name is `Notifications`. Clearing a visible notice uses a 180 ms fade.

Shared implementation: [desktop/design_system/toast_region/toast_region.cpp:1](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/design_system/toast_region/toast_region.cpp:1). API: [desktop/design_system/toast_region/toast_region.h:1](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/design_system/toast_region/toast_region.h:1). Styling: [desktop/design_system/toast_region/toast_region_application_style_sheet.qss:1](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/design_system/toast_region/toast_region_application_style_sheet.qss:1). This is the only application toast implementation found in the scanned desktop sources.

## Screens and feature surfaces

The four top-level MainWindow screen values are Start, SQL, Object, and History. Window notifications may remain visible across screen changes. The rows below identify emitting workflows, rather than claiming each is a separate top-level screen.

| Screen or feature | Toast content | Placement |
| --- | --- | --- |
| Main window and Start navigation | Tab/navigation guards, startup diagnostics | Window |
| SQL workspace and editor | Query progress, completion catalog warning, saved SQL opening errors, connection errors | Window; query progress on central widget |
| Object navigator and SQL generation | SQL generation errors/success, refresh errors | Window |
| Object drop and rename | Preconditions, SQL preparation/execution failures, success, refresh follow-up | Window |
| Pins sidebar | Load/save errors, missing connection/object, lookup/reveal guards | Window |
| Object explorer | Loading metadata/ERD; active Data operation guards | Explorer progress; window warnings when hosted by MainWindow and Data footer applies |
| Object Data workspace | Working with object data | Object Data host |
| History screen/dock | Loading/clearing/policy progress, truncated preview, opening history guards | Dock progress; window notices |
| Preferences dialog | Load/save progress, results, aggregated/validation failures | Dialog progress; owning window notices |
| Query settings dialog | Load/save progress, results, persistent failures | Dialog progress; owning window notices |
| Search and Replace panel | Working progress, replacement success/error | Panel progress; owning window notices |
| Connection profile dialog | Profile save/delete/test/connect results and failures; SSH inspection | Owning window notices and SSH progress |
| SSH host key review dialog | Approval success/unknown, failure, invalidation | Owning window |
| Export dialog | Destination/start/cancel/live rows and bytes, outcome | Dialog progress; owning window outcome |
| Value detail dialog | Chunk offset loading and failure | Dialog progress; owning window failure |
| Appearance and workspace recovery workflows | Persistent appearance warnings, pinned restore/save/close recovery failures | Window |
| Update/restart/close workflows | Postponed update/close warnings | Window |
| Design-system preview tool | Success, warning, danger, progress and pinned examples | Preview viewport; not production workflow |

## Forwarded messages and dynamic data

Direct call expressions are listed in the next section. This section expands values such as `message`, `error`, `reason`, `detail`, and `failureMessage` that otherwise conceal message text.

### Workspace recovery and appearance

`desktop/app/main_window_lifecycle.cpp:288` chooses these pinned Danger titles:

- `Could not save workspace before closing`
- `Could not save workspace for recovery`
- `Could not restore workspace`

All use body `Details: %1. Recovery actions: File → Workspace recovery.` with the recovery error substituted for `%1`. A successful recovery state clears the pin. Update-restart failure also has a separate ordinary `Update postponed: %1` warning path.

AppearanceController emits `warningChanged`; MainWindow renders each nonempty warning as Warning with duration 0. Empty warning clears the ordinary notice (or pending notice if recovery is pinned). Messages from `desktop/app/appearance_controller.cpp`:

- `Appearance could not be loaded: %1. Retry or reset in Preferences.` — load error; retained as `persistentWarning_` and may be emitted again.
- `Appearance reset failed: %1` — reset error.
- `Layout could not be saved: %1` — automatic save error.
- `Choose System, Light, or Dark.` — invalid theme choice.
- `Appearance is still loading.` — preview before readiness.
- `Appearance settings are still loading or saving. Please retry.` — staging reset while busy.
- `Apply or cancel the appearance preview before resetting layout.` — reset layout during preview.

Appearance save failures can also enter the Preferences aggregate as `Appearance: %1`. The `saveFinished` source can provide raw save error, the retained load warning, `Retry loading appearance or reset it before saving.`, or `Layout settings are still being saved. Please retry.`. Appearance success `Appearance saved.` is handled by Preferences state and is not a separate direct toast.

### Preferences validation and aggregated errors

`desktop/widgets/preferences_dialog/preferences_dialog.cpp:550` joins errors with newline into one Danger body. Inputs include:

- `SQL editor / Keyboard shortcuts: %1` — shortcut validation error.
- `Connections or results: stored settings are invalid. Restore defaults to replace them.`
- `History & recovery: stored retention is outside the supported range. Restore defaults to replace it.`
- `Appearance: %1` — appearance failure.
- `%1: %2` — section and recovery/storage error; section is `SQL editor / Keyboard shortcuts`, `Results & execution`, or `History & recovery`.
- `Settings service is unavailable.`

`shortcutValidationError()` in `desktop/models/shortcut_catalog.cpp` returns the following texts. EditorPreferencesController forwards a nonempty result to a window Danger toast. PreferencesDialog first attempts inline validation and only uses a toast when it cannot place the error:

- `Unsupported preferences version.`
- `Font size is outside the supported range.`
- `Font family must be valid text of at most 256 UTF-8 bytes.`
- `The command catalog contains duplicate or empty identifiers.`
- `Invalid default shortcut for %1.` — command label.
- `Unknown command in shortcut overrides.`
- `A command has more than one shortcut override.`
- `The shortcut for %1 cannot be changed.` — command label.
- `Invalid shortcut for %1.` — command label.
- `The shortcuts for %1 and %2 conflict.` — command labels.

### Profiles and SSH trust

`ProfileDialog::setBusy(false, message, success)` shows a toast only when visible and message is nonempty. `success=true` selects Success; otherwise Danger. Busy profile operations use a progress dialog, not a bottom-right toast. Terminal body sources:

- `Profile saved.` or `Profile saved. %1` — backend warning appended, still Success.
- `Profile deleted.` or `Profile deleted. %1` — backend warning appended, still Success.
- `Connection test succeeded.` — Success.
- `Connected.` — Success.
- `Connection closed.` — Success in current code.
- Raw `profileFailed` error, or `%1 List refresh failed: %2` — preceding save/delete notice and refresh error, Danger.
- Raw connection failure error, optionally suffixed by ` [Code: %1]` — vendor code, Danger.
- `The saved profile is no longer available. Refresh the profile list.` — Danger.
- `Connection could not be submitted.` or raw connection submission error — Danger.
- `Invalid connection options. %1` — options validation error, Danger.

Sources include `profile_dialog.cpp`, `profile_dialog_actions.cpp`, and `profile_dialog_ssh_trust.cpp`. `showTrustStatus(message)` forwards these as Danger when the profile dialog is visible:

- `Connection settings changed. Inspect the host keys again.`
- `No SSH host keys were returned.`
- `Inspected host keys do not match the selected SSH server.`
- `The approval request finished for the previous settings. Inspect the current SSH server before retrying.`
- `SSH host key approval outcome is unknown.`
- Raw SSH host-key operation error.

SSH approval with no active review dialog produces Success `SSH host key approved.`. With a review dialog, the direct calls below produce `Key approved. Retry the connection to use it.`, Warning unknown outcome, or raw Danger failure. Settings invalidation in the review dialog is Warning, whereas the similarly worded profile trust status is Danger.

### Pins and navigator lookup

`savePinsAsync(updated, failureMessage)` substitutes persistence error into `%1`, rendering Danger at `main_window_pins.cpp:674`. Its callers supply:

- `Could not save pins: %1` — pin/unpin, including context-menu unpin.
- `Could not remove deleted profile's pins: %1`
- `Could not save unavailable pin state: %1` — both lookup paths.
- `The object changed, but its pin could not be saved: %1` — object rename/drop pin reconciliation.

`reason` at `main_window_pins.cpp:438` is `Connection unavailable. Collapse and expand to retry.`. Reasons forwarded from NavigatorController at lines 512 and 637 are Warning:

- `The object could not be shown. Activate the pin to retry.`
- `Finish active database work before opening this pin. Activate it to retry.`
- `The connection is no longer visible. Activate the pin to retry.`
- `The metadata lookup limit was reached. Activate the pin to retry.`
- `Metadata could not load: %1. Activate the pin to retry.` — metadata error.
- `The pinned object no longer matches its saved identity.`
- `The pinned object is no longer in its saved location.`

The expansion status row rewrites retry wording and has a `Could not load children. Collapse and expand to retry.` fallback; the toast forwards the original `reason`, not that transformed status-row string.

### Generated SQL and object actions

NavigatorController `generationFailed` reaches Danger at `main_window_navigator.cpp:218` with these UI-produced strings:

- `The selected object is no longer available.`
- `Select a table or view to generate SQL.`
- `Expand this object to load columns before generating SQL.`
- `Too many metadata objects to generate SQL.`
- `Column metadata exceeds the SQL template limits.`

`SqlTemplateService::generate` can also forward `SQL template input exceeds limits or is not valid Unicode.`. Rust template/bridge errors are `Unknown SQL template.`, `An identifier or required column list is empty`, `An identifier contains a NUL character`, `The quoted qualified name is invalid`, `SQL template exceeds resource limits`, and `Generated SQL exceeds the template size limit.`. Sources: `desktop/bridge/template_service.cpp`, `crates/bridge/src/templates.rs`, `crates/sql-language/src/templates.rs`. These are producer error vocabularies; guards may prevent individual inputs from reaching the toast through normal interaction.

Object-action `statement.error` is forwarded as Danger at two direct sites. Vocabulary from `desktop/app/object_action_sql.cpp` and `crates/core/src/object_action.rs`:

- `The object action input is not valid Unicode.`
- `Only tables and views support this action.`
- `The selected relation type is not supported.`
- `SQLite does not support renaming views directly.`
- `The selected object has an invalid identity.`
- `Enter a valid unqualified object name.`
- `Enter a different object name.`

Rename validation may instead be presented inline in its dialog before reaching the final preparation failure toast. Execution failure `%1` is action (`drop` or `rename`); `%2` is display kind; `%3` is qualified name; `%4` is backend event error. Display kind can describe table/view/materialized view/foreign table. Refresh follow-up errors come from metadata events or the NavigatorController refresh signal; both display paths are listed below.

### Query and object progress

SQL query progress title `Query in progress` uses state-dependent detail `Waiting to run…` for queued, `Cancelling query…` for cancelling, or `Running query…` otherwise while active.

ObjectExplorer progress title `Loading object` receives `%1 · Loading ERD…` (object label) or `%1 · Loading %2…` (label and lowercased current pane label). Its Data guards forward Window Warning bodies `Finish or cancel the active Data operation before changing panes.` and `Finish or cancel the active Data operation before changing objects.` when the Data footer path applies. Other explorer status errors are inline statuses, not automatically toast messages.

Object Data host progress uses title `Object data` and detail `Working with object data…`.

### Export and value details

Export terminal body comes from `ExportDialog::finish(message, outcome)`:

| Outcome | Variant and title | Body and data |
| --- | --- | --- |
| Success | Success / Success | `Export complete: %1 rows · %2 bytes`; event `exported_rows`, `exported_bytes`. |
| Failed | Danger / Error | `Could not check destination: %1` with destination preflight error; `Export could not be started.` fallback; raw synchronous submission error; raw `export_failed` event error. |
| Cancelled | Warning / Export | `Export cancelled.`; declining overwrite or cancelling submission. |

Live export progress uses the same event row/byte counters, but its bar remains indeterminate. Value loading displays requested byte `offset`; value failure displays `error.left(1024)`, truncating to the first 1024 QString characters.

### Search and History

Replace-all forwards `result.error` as Danger unless an oversized search needle sends it to field validation instead. The bridge has `Search input is not valid Unicode.`; Rust replace-all vocabulary includes `Search text is empty` and `Search operation exceeds resource limits`. `Search offset is invalid` belongs to find operations, not this replace-all path. Source prechecks may make some producer failures unreachable through ordinary valid UI input. Zero matches, discarded replacement results, invalid field input, and the UI output-size guard use inline status/validation rather than toast.

HistoryDock `noticeRequested` forwards Window Warning `Preview truncated to part %1. Use Earlier text / Later text to read all SQL.`; `%1` is `previewOffsets_.size() + 1`. History loading, clearing, and policy work use the progress calls below. History failure/status messages otherwise remain inline.

### Other forwarded backend errors

The direct calls below also forward raw runtime text from preferences/query-settings storage, saved connection loading/connecting, document path identification and document opening, workspace recovery, SSH operations, and metadata requests. These bodies are not restricted to a finite source-string catalog. Saved SQL opening uses the file basename and error; missing identity results use `Path is invalid.`. The saved-directory failure toast displays the directory path, without the underlying watcher error detail. Startup diagnostics intentionally shows a fixed generic warning.

## All direct display call sites

102 calls across 19 source files were found in `desktop/app`, `desktop/widgets`, and `desktop/tools`, excluding the MainWindow wrapper definition itself. This count includes the wrapper's forwarding call and three preview-tool calls; the other 98 calls are application workflow sites. Each expression preserves title/body, argument transformations, severity, and any explicit timeout. Unspecified `showToast` timeout is 5000 ms. Source links refer to the inspected workspace.

### desktop/app/editor_preferences.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/editor_preferences.cpp:85](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/editor_preferences.cpp:85) | `showToast( tr("Preferences could not be loaded: %1. Open Preferences to retry.") .arg(error), ToastVariant::Danger)` |
| [desktop/app/editor_preferences.cpp:96](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/editor_preferences.cpp:96) | `showToast(error, ToastVariant::Danger)` |

### desktop/app/main.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main.cpp:78](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main.cpp:78) | `showToast(QObject::tr("Local diagnostics could not be started."), choscordb::ToastVariant::Warning)` |
| [desktop/app/main.cpp:97](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main.cpp:97) | `showToast(QObject::tr("Local diagnostics could not be started."), choscordb::ToastVariant::Warning)` |

### desktop/app/main_window.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main_window.cpp:52](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:52) | `showToast(title, message, variant)` |
| [desktop/app/main_window.cpp:172](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:172) | `showToast(tr("Finish or cancel the active database work before changing workspace " "tabs. Cancel remains in the active tab."), ToastVariant::Warning)` |
| [desktop/app/main_window.cpp:204](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:204) | `showToast(tr("Open an object from the navigator to show its tab."), ToastVariant::Warning)` |
| [desktop/app/main_window.cpp:208](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:208) | `showToast(tr("Close all workspace tabs to return to Start."), ToastVariant::Warning)` |
| [desktop/app/main_window.cpp:268](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:268) | `showProgress(QObject::tr("Object data"), QObject::tr("Working with object data…"))` |
| [desktop/app/main_window.cpp:320](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:320) | `showToast(tr("This object is already open; its tab is selected."), ToastVariant::Warning)` |
| [desktop/app/main_window.cpp:335](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:335) | `showToast(tr("Reconnecting the saved connection. Select this tab to load fresh " "metadata."), ToastVariant::Warning)` |
| [desktop/app/main_window.cpp:339](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:339) | `showToast(tr("The saved connection is unavailable. Restore it in the sidebar, " "then retry."), ToastVariant::Danger)` |
| [desktop/app/main_window.cpp:343](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window.cpp:343) | `showToast(tr("Select a live connection in the sidebar, then choose Reconnect again."), ToastVariant::Warning)` |

### desktop/app/main_window_lifecycle.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main_window_lifecycle.cpp:285](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:285) | `showToast(tr("Update postponed: %1").arg(error), ToastVariant::Warning)` |
| [desktop/app/main_window_lifecycle.cpp:292](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:292) | `showPinnedToast( title, tr("Details: %1. Recovery actions: File → Workspace recovery.").arg(error), ToastVariant::Danger)` |
| [desktop/app/main_window_lifecycle.cpp:403](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:403) | `showToast(message, ToastVariant::Warning)` |
| [desktop/app/main_window_lifecycle.cpp:416](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:416) | `showToast(tr("Warning"), warning, ToastVariant::Warning, 0)` |
| [desktop/app/main_window_lifecycle.cpp:421](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:421) | `showToast(tr("Close postponed: %1").arg(error), ToastVariant::Warning)` |
| [desktop/app/main_window_lifecycle.cpp:435](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:435) | `showToast(tr("History cannot be opened while the workspace is unavailable."), ToastVariant::Warning)` |
| [desktop/app/main_window_lifecycle.cpp:454](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:454) | `showToast(tr("History text could not be opened."), ToastVariant::Danger)` |
| [desktop/app/main_window_lifecycle.cpp:520](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_lifecycle.cpp:520) | `showToast(tr("Update postponed: %1").arg(error), ToastVariant::Warning)` |

### desktop/app/main_window_navigator.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main_window_navigator.cpp:135](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:135) | `showToast( tr("Object changed, but navigator refresh failed: %1. Choose Refresh to retry.") .arg(error), ToastVariant::Danger)` |
| [desktop/app/main_window_navigator.cpp:218](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:218) | `showToast(tr("Error"), error, ToastVariant::Danger)` |
| [desktop/app/main_window_navigator.cpp:226](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:226) | `showToast(tr("The selected connection is no longer available."), ToastVariant::Danger)` |
| [desktop/app/main_window_navigator.cpp:230](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:230) | `showToast(tr("Finish the active query before switching connections to generate SQL."), ToastVariant::Warning)` |
| [desktop/app/main_window_navigator.cpp:236](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:236) | `showToast(tr("Generated SQL exceeds editor limits."), ToastVariant::Danger)` |
| [desktop/app/main_window_navigator.cpp:245](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:245) | `showToast(tr("Generated SQL could not be opened."), ToastVariant::Danger)` |
| [desktop/app/main_window_navigator.cpp:256](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_navigator.cpp:256) | `showToast(tr("SQL generated"), tr("Review the draft before running."), ToastVariant::Success)` |

### desktop/app/main_window_object_actions.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main_window_object_actions.cpp:37](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:37) | `showToast(tr("Finish the current object action and navigator refresh before trying " "again."), ToastVariant::Warning)` |
| [desktop/app/main_window_object_actions.cpp:46](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:46) | `showToast(tr("The selected connection is no longer available."), ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:51](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:51) | `showToast(tr("The selected object changed. Refresh the navigator and try again."), ToastVariant::Warning)` |
| [desktop/app/main_window_object_actions.cpp:57](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:57) | `showToast(tr("Finish or cancel active database work and pending edits before changing " "this object."), ToastVariant::Warning)` |
| [desktop/app/main_window_object_actions.cpp:67](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:67) | `showToast(tr("Finish the active object inspection before changing this object."), ToastVariant::Warning)` |
| [desktop/app/main_window_object_actions.cpp:74](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:74) | `showToast(tr("Finish or cancel active object data work and pending edits " "before changing this object."), ToastVariant::Warning)` |
| [desktop/app/main_window_object_actions.cpp:100](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:100) | `showToast(statement.error, ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:172](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:172) | `showToast(statement.error, ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:180](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:180) | `showToast(tr("The object action could not be submitted. Check the connection and retry."), ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:205](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:205) | `showToast(tr("The object action could not start: %1").arg(error), ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:222](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:222) | `showToast(tr("Object changed, but navigator refresh failed: %1. Choose Refresh " "to retry.") .arg(bridgeText(event.error)), ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:237](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:237) | `showToast(tr("The connection closed before the object action completed."), ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:249](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:249) | `showToast(tr("Could not %1 %2 %3: %4") .arg(action.action, action.displayKind, action.qualifiedName, bridgeText(event.error)), ToastVariant::Danger)` |
| [desktop/app/main_window_object_actions.cpp:285](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:285) | `showToast(action.action == QStringLiteral("drop") ? tr("%1 dropped. Refreshing navigator…").arg(action.displayKind) : tr("%1 renamed. Refreshing navigator…").arg(action.displayKind), ToastVariant::Success)` |
| [desktop/app/main_window_object_actions.cpp:292](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_object_actions.cpp:292) | `showToast(tr("Object changed, but the navigator could not refresh. Choose Refresh to " "retry."), ToastVariant::Warning)` |

### desktop/app/main_window_pins.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main_window_pins.cpp:172](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:172) | `showToast(tr("Some saved pins could not be loaded: %1").arg(error), ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:204](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:204) | `showToast(tr("Save this connection before pinning its objects."), ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:438](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:438) | `showToast(reason, ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:512](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:512) | `showToast(reason, ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:547](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:547) | `showToast(tr("This object is unavailable. Unpin it or refresh the original navigator."), ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:553](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:553) | `showToast(tr("Finish active database work or pending edits before opening this pin."), ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:563](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:563) | `showToast(tr("The saved connection for this pin was not found."), ToastVariant::Danger)` |
| [desktop/app/main_window_pins.cpp:568](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:568) | `showToast(tr("The saved connection could not be opened. Fix the connection or finish " "active work, then activate the pin to retry."), ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:637](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:637) | `showToast(reason, ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:649](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:649) | `showToast(tr("The connection is not ready. Activate the pin to retry."), ToastVariant::Warning)` |
| [desktop/app/main_window_pins.cpp:674](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_pins.cpp:674) | `showToast(failureMessage.arg(error), ToastVariant::Danger)` |

### desktop/app/main_window_workspace.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/main_window_workspace.cpp:230](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:230) | `showToast(tr("Suggestions use loaded navigator objects. Expand nodes for more " "names; large catalogs may be limited."), ToastVariant::Warning)` |
| [desktop/app/main_window_workspace.cpp:358](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:358) | `showToast(tr("Saved file cannot be opened while the workspace is busy."), ToastVariant::Warning)` |
| [desktop/app/main_window_workspace.cpp:383](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:383) | `showToast(tr("Saved file cannot be opened while the workspace is busy."), ToastVariant::Warning)` |
| [desktop/app/main_window_workspace.cpp:389](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:389) | `showToast(tr("Could not open %1: %2") .arg(QFileInfo(path).fileName(), identities.isEmpty() ? tr("Path is invalid.") : identities.front().error), ToastVariant::Danger)` |
| [desktop/app/main_window_workspace.cpp:418](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:418) | `showToast(tr("Could not open %1: %2") .arg(QFileInfo(openedPath).fileName(), error), ToastVariant::Danger)` |
| [desktop/app/main_window_workspace.cpp:467](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:467) | `showToast( tr("Could not create saved SQL directory: %1").arg(savedDirectory), ToastVariant::Danger)` |
| [desktop/app/main_window_workspace.cpp:560](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:560) | `showProgress(tr("Query in progress"), detail)` |
| [desktop/app/main_window_workspace.cpp:934](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:934) | `showToast(tr("Saved connections: %1").arg(error), ToastVariant::Danger)` |
| [desktop/app/main_window_workspace.cpp:939](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/main_window_workspace.cpp:939) | `showToast(error, ToastVariant::Danger)` |

### desktop/app/object_explorer.cpp

| Source | Display expression |
| --- | --- |
| [desktop/app/object_explorer.cpp:641](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/object_explorer.cpp:641) | `showProgress(tr("Loading object"), text)` |
| [desktop/app/object_explorer.cpp:653](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/app/object_explorer.cpp:653) | `showToast(text, ToastVariant::Warning)` |

### desktop/widgets/export_dialog/export_dialog.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/export_dialog/export_dialog.cpp:237](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/export_dialog/export_dialog.cpp:237) | `showProgress(tr("Export"), tr("Checking destination…"))` |
| [desktop/widgets/export_dialog/export_dialog.cpp:272](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/export_dialog/export_dialog.cpp:272) | `showProgress(tr("Export"), tr("Starting export…"))` |
| [desktop/widgets/export_dialog/export_dialog.cpp:307](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/export_dialog/export_dialog.cpp:307) | `showProgress(tr("Export"), tr("Cancelling…"))` |
| [desktop/widgets/export_dialog/export_dialog.cpp:317](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/export_dialog/export_dialog.cpp:317) | `showProgress(tr("Export"), tr("Exporting: %1 rows · %2 bytes") .arg(value.exported_rows) .arg(value.exported_bytes))` |
| [desktop/widgets/export_dialog/export_dialog.cpp:346](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/export_dialog/export_dialog.cpp:346) | `showToast(outcome == Outcome::Success ? tr("Success") : outcome == Outcome::Failed ? tr("Error") : tr("Export"), message, variant)` |

### desktop/widgets/history_dock/history_dock.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/history_dock/history_dock.cpp:467](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/history_dock/history_dock.cpp:467) | `showProgress(tr("History"), tr("Clearing history…"))` |
| [desktop/widgets/history_dock/history_dock.cpp:469](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/history_dock/history_dock.cpp:469) | `showProgress(tr("History"), tr("Saving or loading history preference…"))` |
| [desktop/widgets/history_dock/history_dock.cpp:472](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/history_dock/history_dock.cpp:472) | `showProgress(tr("History"), tr("Loading history…"))` |

### desktop/widgets/preferences_dialog/preferences_dialog.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:456](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:456) | `showProgress(tr("Preferences"), tr("Saving or loading preferences…"))` |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:499](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:499) | `showToast(tr("Error"), error, ToastVariant::Danger)` |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:521](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:521) | `showProgress(tr("Preferences"), tr("Saving preferences…"))` |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:550](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:550) | `showToast(tr("Error"), errors_.join(QLatin1Char('\n')), ToastVariant::Danger)` |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:554](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:554) | `showToast(tr("Success"), saved ? tr("Preferences saved.") : tr("Preferences loaded."), ToastVariant::Success)` |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:565](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:565) | `showProgress( tr("Preferences"), tr("Saving preferences… Please wait for storage to finish."))` |
| [desktop/widgets/preferences_dialog/preferences_dialog.cpp:578](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/preferences_dialog/preferences_dialog.cpp:578) | `showProgress( tr("Preferences"), tr("Saving preferences… Please wait for storage to finish."))` |

### desktop/widgets/profile_dialog/profile_dialog.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/profile_dialog/profile_dialog.cpp:721](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/profile_dialog.cpp:721) | `showToast(success ? tr("Success") : tr("Error"), message, success ? ToastVariant::Success : ToastVariant::Danger)` |

### desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:140](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:140) | `showToast(tr("Success"), tr("SSH host key approved."), ToastVariant::Success)` |
| [desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:164](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:164) | `showToast(tr("Error"), message, ToastVariant::Danger)` |
| [desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:205](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:205) | `showProgress(tr("Profiles"), tr("Inspecting SSH host keys…"))` |

### desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:143](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:143) | `showToast(approved_ ? tr("Success") : tr("Warning"), approved_ ? tr("Key approved. Retry the connection to use it.") : tr("Approval outcome is unknown. Check the selected file or " "retry approval."), approved_ ? ToastVariant::Success : ToastVariant::Warning)` |
| [desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:154](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:154) | `showToast(tr("Error"), error, ToastVariant::Danger)` |
| [desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:161](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:161) | `showToast(tr("Warning"), tr("Connection settings changed. Inspect the host keys " "again."), ToastVariant::Warning)` |

### desktop/widgets/query_settings_dialog/query_settings_dialog.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:100](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:100) | `showToast( tr("Error"), tr("Stored settings version is unsupported. Restore defaults."), ToastVariant::Danger, 0)` |
| [desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:109](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:109) | `showToast(tr("Success"), saving_ ? tr("Query settings saved.") : tr("Query settings loaded."), ToastVariant::Success)` |
| [desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:124](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:124) | `showToast(tr("Error"), error, ToastVariant::Danger, 0)` |
| [desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:131](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:131) | `showProgress(tr("Query settings"), tr("Loading query settings…"))` |
| [desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:135](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:135) | `showToast(tr("Error"), tr("Settings service is unavailable."), ToastVariant::Danger, 0)` |
| [desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:176](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:176) | `showProgress(tr("Query settings"), tr("Saving query settings…"))` |

### desktop/widgets/search_panel/search_panel.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/search_panel/search_panel.cpp:164](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/search_panel/search_panel.cpp:164) | `showProgress(tr("Search"), tr("Working…"))` |
| [desktop/widgets/search_panel/search_panel.cpp:280](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/search_panel/search_panel.cpp:280) | `showToast(tr("Success"), tr("Replaced one match."), ToastVariant::Success)` |
| [desktop/widgets/search_panel/search_panel.cpp:331](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/search_panel/search_panel.cpp:331) | `showToast(tr("Error"), result.error, ToastVariant::Danger)` |
| [desktop/widgets/search_panel/search_panel.cpp:354](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/search_panel/search_panel.cpp:354) | `showToast(tr("Success"), tr("Replaced %1 matches.").arg(result.count), ToastVariant::Success)` |

### desktop/widgets/value_detail_dialog/value_detail_dialog.cpp

| Source | Display expression |
| --- | --- |
| [desktop/widgets/value_detail_dialog/value_detail_dialog.cpp:212](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/value_detail_dialog/value_detail_dialog.cpp:212) | `showProgress(tr("Value"), tr("Loading bytes at offset %1…").arg(offset))` |
| [desktop/widgets/value_detail_dialog/value_detail_dialog.cpp:226](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/widgets/value_detail_dialog/value_detail_dialog.cpp:226) | `showToast(tr("Error"), tr("Unable to load value: %1").arg(error.left(1024)), ToastVariant::Danger)` |

### desktop/tools/preview/preview_standard.cpp

| Source | Display expression |
| --- | --- |
| [desktop/tools/preview/preview_standard.cpp:410](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/tools/preview/preview_standard.cpp:410) | `showToast(example.title, example.body, example.variant, duration->value() * 1000)` |
| [desktop/tools/preview/preview_standard.cpp:418](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/tools/preview/preview_standard.cpp:418) | `showProgress("Exporting", "Writing rows…")` |
| [desktop/tools/preview/preview_standard.cpp:434](/Users/ngthluu/.agents-sessions/choscordb-choscordb-feat-toast-display/desktop/tools/preview/preview_standard.cpp:434) | `showPinnedToast("Workspace recovery failed", "invalid command input", choscordb::ToastVariant::Danger)` |

## Preview examples

The design-system preview is demonstration code. Its three terminal examples expand the `example` values in the call-site table:

| Variant | Title | Body |
| --- | --- | --- |
| Success | Saved | Your changes have been saved. |
| Warning | Warning | Suggestions use loaded navigator objects. Expand nodes for more names; large catalogs may be limited. |
| Danger | Could not save | Please try again. |
| Progress | Exporting | Writing rows… |
| Pinned Danger | Workspace recovery failed | invalid command input |

Preview terminal timeout is configurable from 1 to 30 seconds, default 5 seconds.

## Verification scope

This inventory was checked against source calls, indirect message producers, and the shared component. Runtime layout and translations were not exercised. Tests are supporting evidence, not additional production message sources: `tests/desktop/preview_toast_test.cpp`, `tests/desktop/navigator_recovery_workspace_test.cpp`, and `tests/desktop/ssh_host_key_test.cpp` cover representative contracts. Test-only artificial messages are excluded from the application catalog. PostgreSQL names such as `pg_toast` are database objects and are unrelated to these notifications.

No implementation requirement or design decision is inferred from this inventory. A later change needs its own agreed behavior and acceptance criteria.
