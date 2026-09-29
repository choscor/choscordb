# Workspace recovery errors in window toasts

- Status: Confirmed for implementation
- Date: 2026-09-29
- Source: User screenshot of `invalid command input` in the workspace toolbar, repository investigation, and brainstorm decisions on 2026-09-29.

## Outcome and conversation summary

Keep unexpected error messages out of toolbar and header slots intended for controls. The user asked for a scan of screenshot-like placements throughout the desktop UI and chose a toast at the bottom-right of the whole window for workspace recovery failures. The toast remains until recovery succeeds or the user dismisses it. It uses a plain-language title and retains the raw backend error as detail.

## Current state and workspace findings

- The highlighted text is `recoveryMessage`, a fixed-width `QLabel` placed before the SQL connection selector in `desktop/app/main_window_lifecycle.cpp`. `WorkspaceRecoveryController::errorOccurred` puts its raw string there and forces the workspace header visible. The screenshot matches this placement.
- The exact `invalid command input` text is `SubmitError::InvalidInput` in `crates/core/src/protocol.rs`. `EngineAdapter::pumpRecovery` can forward a rejected recovery command through `recoveryFailed` to the controller. The screenshot alone does not identify why the command was rejected or prove which saved field failed validation.
- The recovery menu in File already owns Retry workspace recovery, Start new workspace, Close without recovery, and Keep workspace open. The existing failure flow disables normal toolbar actions while recovery is unresolved.
- `desktop/design_system/toast_region/` provides a window-level, bottom-right toast. Its API supports a nonpositive duration for a notice that does not expire automatically, and an explicit dismiss button. The current window toast has one visible notice at a time.
- A source scan of `desktop/app/` and `desktop/widgets/` found no second raw backend error placed in a toolbar or header like this one. Other dynamic errors are in dedicated status, validation, dialog, or toast surfaces. This finding should be rechecked against the current tree during implementation.

## Requirements and decisions

1. Remove the recovery error label and its reserved width from the workspace header. Recovery failures must not insert raw error text into the query toolbar, tab bar, or another control row.
2. On a restore or save/close recovery failure, show a danger toast attached to the whole main window at its bottom-right corner. Give it a plain-language title that names the failed workspace action, followed by the backend error as detail. The toast should tell the user where to find the relevant recovery actions in File → Workspace recovery; preserve the underlying error without presenting it as an unexplained heading.
3. Keep the recovery failure toast visible without an automatic timeout. Clear it when that failure is resolved; allow the user to dismiss it manually. Dismissing the toast does not clear the recovery failure, re-enable blocked work, or disable the recovery menu. A later distinct failure may show a new toast. An unrelated notification must not silently erase an unresolved recovery notice; if the shared toast region needs coordination, make that behavior explicit.
4. Preserve existing recovery semantics and action availability: retry, start empty after an unresolved restore when allowed, close without recovery or keep open after a close failure, disabled toolbar actions, and the guards against losing unsaved work. A failed update-install close continues to use its existing postponement path unless investigation finds the screenshot-like placement there.
5. Audit other desktop app/widget paths that put dynamic error strings in toolbar, tab, or header control areas. Fix any actual match with the same user-visible rule. Keep legitimate inline validation, contextual status lines, dialogs, and toasts in their intended places. Do not suppress failures to make the UI appear clean.

## Non-goals

- Changing recovery storage validation, protocol wording, or the saved-workspace format solely because this screenshot shows `invalid command input`.
- Removing useful error detail from the UI, hiding a failed recovery state, or changing ordinary query/result errors and contextual validation surfaces.
- Creating a new general notification component when the existing toast contract can serve the behavior.

## User-visible flows and failure behavior

- On startup restore failure, the user sees a bottom-right toast headed with a clear restore-failure message and the backend detail. No error label appears beside the connection selector. File → Workspace recovery exposes Retry and Start new workspace when applicable.
- On autosave or close recovery failure, the toast identifies saving/closing as the affected action and includes the backend detail. The existing enabled menu choices and blocked toolbar behavior remain correct. Retrying successfully clears the recovery notice; a failed retry updates or re-shows it.
- Dismissing the toast removes only the notification. The failed recovery state and available menu actions remain. Opening or resizing the window keeps a visible recovery toast anchored to the whole-window bottom-right. Any overlapping notification behavior preserves the persistent failure notice until resolved or dismissed.

## Acceptance criteria and public test seams

| Observable outcome | Public test seam |
| --- | --- |
| Restore and save/close failures show a danger toast at the main-window bottom-right with a contextual title, raw error detail, and a pointer to File → Workspace recovery. No error text or reserved recovery label appears in the workspace header/control row. | `MainWindow` Qt integration test using a temporary storage path and `WorkspaceRecoveryController::errorOccurred`, extending `tests/desktop/navigator_sql_workspace_test.cpp`; inspect the visible `toastRegion`, its accessible description and geometry, and the header's child widgets. |
| The toast does not auto-expire, can be dismissed, and clears on successful recovery without clearing an unrelated current notification. A new failure can be surfaced again. | Drive failure, dismiss, retry/success, and another failure through the public controller and `ToastRegion` behavior in a Qt integration test; use timer/event-loop observation rather than private state. |
| Recovery menu action availability and normal-work guards are unchanged after failure and after toast dismissal. | Extend the existing action-state checks in `tests/desktop/navigator_sql_workspace_test.cpp` and disabled-toolbar checks in `tests/desktop/modern_ui_test.cpp`. |
| A real rejected recovery command still reaches the user-facing failure flow; its technical detail remains available in the toast. | Exercise an invalid/rejected recovery submission through the existing adapter/controller seams in `tests/desktop/recovery_test.cpp` or `tests/desktop/workspace_test.cpp`, then observe the main-window toast through the UI seam where practical. |
| No other desktop toolbar/header renders unexpected dynamic error text; legitimate status and validation messages still appear in their contextual surfaces. | Source census/review of dynamic error setters in `desktop/app/` and `desktop/widgets/`, plus focused Qt UI tests for any additional match found. Run the required UI policy and consistency checks. |

## Technical constraints and likely affected areas

- Primary app change: `desktop/app/main_window_lifecycle.cpp`; inspect `desktop/app/main_window_ui.cpp`, `desktop/app/main_window_widgets.h`, and `desktop/app/main_window.cpp` for header composition and toast ownership. Use `desktop/design_system/toast_region/` as the shared presentation. Keep workflow state in app code and preserve object names, focus, signals, accessibility, and recovery action contracts.
- Follow the repository's desktop UI ownership rules. Before extracting or changing a shared component, inspect `desktop/design_system/README.md` and call sites. If the toast component itself changes, follow `desktop/design_system/AGENTS.md`, including Light and Dark gallery specimen coverage and its matching test.
- Run `python3 scripts/ci/ui_consistency.py` and inspect `--json`, plus `python3 scripts/ci/ui_policy.py` and `python3 scripts/ci/qss_policy.py`. Build and run relevant native CTest targets (`navigator-sql-workspace`, `modern-ui`, `workspace`, `recovery`); run the full native suite when dependencies are present.

## Rollout, compatibility, risks, and deferred choices

- No migration or separate rollout is required. Recovery persistence and menu actions keep their existing contracts.
- The shared window toast currently displays one notice. Preserving a persistent recovery notice while other notifications arrive may require coordination or a small shared-component change. The exact queue/priority mechanism is deferred; the observable persistence rule above is fixed.
- The screenshot identifies the error route and location, not the underlying invalid input. If implementation investigation finds a separate backend defect, report it without silently broadening this presentation task.

## Fresh-session instruction

Read this entire spec, inspect the current workspace, and invoke `$implement` with `docs/specs/2026-09-29-recovery-error-toast.md`. Implement the confirmed behavior with tests at the public seams above.
