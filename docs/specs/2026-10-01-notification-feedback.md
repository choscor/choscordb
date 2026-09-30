# Notification feedback decisions and implementation

Date: 2026-10-01. Baseline: `bf6eb4134e0bb5b6740fc1bfb5e14907b4da7a03`.

The historical [toast inventory](../inventories/2026-10-01-bottom-right-toasts.md) remains unchanged. This document records final decisions for all 98 production display sites, including conditional outcomes and open-ended forwarded backend errors. The MainWindow forwarding wrapper and three gallery display calls are excluded.

## Score and decision contract

Scores are heuristic UX judgments: `5 × (2 × impact + recovery difficulty + confusion/noise)`, each dimension 0–5. They are not measured usage frequency. 80+ fixes take priority, 60–79 needs local feedback, 40–59 should be simplified, below 40 is generally useful feedback.

- Keep useful explicit completion toasts: generated SQL draft, saved Preferences after closing, profile save/delete/test/connect, completed export, and completed object mutation. Keep pinned workspace recovery failures. Keep complete details locally when a completion has follow-up warnings.
- Remove automatic settings-loaded success and duplicate-object notices. Missing Object navigation uses an empty state. Start navigation preserves documents and retains active-work guards.
- Replace progress overlays with local shared StatusLine loading icons and precise state text. Preserve result/object identity, Cancel controls, and existing request readiness.
- Move guards, validation fallback, retryable errors, appearance failures, pin failures, and SSH trust state into persistent selectable local status. Expected field validation remains inline.
- Intentional cancellation is neutral. Typed Rust cancellation outcomes determine export feedback. A pending connection closing is never green Success. Partial success explicitly distinguishes successful mutation from failed refresh/persistence.
- Shared statuses belong to their source: navigator, pins, saved files, connection, editor preferences, completion hints, application diagnostics, appearance, and workspace guards. Clear only the matching owned notice; appearance reset never clears a toast or another source's status.

## Readable toast contract

Success notices reserve at least 10 seconds, longer for longer text and longer caller-specified durations. Warning/error notices stay until explicitly dismissed or resolved. Timers start only when actually visible, pause on hover/keyboard focus/full-details reading, and resume remaining time. Incoming notices wait in FIFO display order; identical notices deduplicate without resetting reading time. Pinned recovery temporarily takes priority while retaining every pending ordinary notice. Resolving a pin or explicitly dismissing a notice advances the queue. No feature clears unrelated notices.

Long messages stay within the host and expose complete selectable text through a Details surface. Dismissal does not resolve an error or cancel an operation. Native accessibility descriptions retain complete current content before announcement. Routine feedback does not use an urgent Alert.

## Complete disposition ledger

Line numbers below refer to the historical inventory, not the edited sources. Dynamic producer families inherit the disposition of their forwarding site. Multiple outcomes at one source site are stated explicitly.

| Baseline source site | UX score | Final decision |
| --- | ---: | --- |
| `desktop/app/editor_preferences.cpp:85` | 75 | Remove toast; persistent editor-preferences status; relevant dialog validation remains inline. |
| `desktop/app/editor_preferences.cpp:96` | 75 | Remove toast; persistent editor-preferences status; relevant dialog validation remains inline. |
| `desktop/app/main.cpp:78` | 55 | Remove toast; application diagnostics status. |
| `desktop/app/main.cpp:97` | 55 | Remove toast; application diagnostics status. |
| `desktop/app/main_window.cpp:172` | 75 | Remove toast; retain persistent local status and full details. |
| `desktop/app/main_window.cpp:204` | 55 | Remove toast; show Object empty state. |
| `desktop/app/main_window.cpp:208` | 85 | Remove restriction and toast; Start preserves open documents and active-work guard. |
| `desktop/app/main_window.cpp:268` | 65 | Remove progress toast; Object footer tracks Data and inspection activity with loading icon, retaining result identity and Cancel. |
| `desktop/app/main_window.cpp:320` | 55 | Remove toast; selecting the existing object tab is sufficient feedback. |
| `desktop/app/main_window.cpp:335` | 65 | Remove toast; connection status with loading icon during reconnect, exact terminal cleanup and recovery instructions. |
| `desktop/app/main_window.cpp:339` | 75 | Remove toast; connection status with loading icon during reconnect, exact terminal cleanup and recovery instructions. |
| `desktop/app/main_window.cpp:343` | 65 | Remove toast; connection status with loading icon during reconnect, exact terminal cleanup and recovery instructions. |
| `desktop/app/main_window_lifecycle.cpp:285` | 75 | Remove toast; persistent workspace blocker/history error explanation with existing actions preserved. |
| `desktop/app/main_window_lifecycle.cpp:292` | 85 | Keep pinned recovery toast until resolved/dismissed; retain full error details and recovery-menu actions. |
| `desktop/app/main_window_lifecycle.cpp:403` | 55 | Remove forwarding toast; History preview owns its dedicated part/paging hint. |
| `desktop/app/main_window_lifecycle.cpp:416` | 70 | Remove toast; source-owned appearance status, cleared only by appearance recovery. |
| `desktop/app/main_window_lifecycle.cpp:421` | 75 | Remove toast; persistent workspace blocker/history error explanation with existing actions preserved. |
| `desktop/app/main_window_lifecycle.cpp:435` | 75 | Remove toast; persistent workspace blocker/history error explanation with existing actions preserved. |
| `desktop/app/main_window_lifecycle.cpp:454` | 75 | Remove toast; persistent workspace blocker/history error explanation with existing actions preserved. |
| `desktop/app/main_window_lifecycle.cpp:520` | 75 | Remove toast; persistent workspace blocker/history error explanation with existing actions preserved. |
| `desktop/app/main_window_navigator.cpp:135` | 70 | Remove toast; navigator partial-success warning and Refresh instruction. |
| `desktop/app/main_window_navigator.cpp:218` | 75 | Remove toast; persistent navigator guard/error; existing field/input validation unchanged. |
| `desktop/app/main_window_navigator.cpp:226` | 75 | Remove toast; persistent navigator guard/error; existing field/input validation unchanged. |
| `desktop/app/main_window_navigator.cpp:230` | 75 | Remove toast; persistent navigator guard/error; existing field/input validation unchanged. |
| `desktop/app/main_window_navigator.cpp:236` | 75 | Remove toast; persistent navigator guard/error; existing field/input validation unchanged. |
| `desktop/app/main_window_navigator.cpp:245` | 75 | Remove toast; persistent navigator guard/error; existing field/input validation unchanged. |
| `desktop/app/main_window_navigator.cpp:256` | 30 | Keep readable SQL-generated completion toast with review-before-running instruction. |
| `desktop/app/main_window_object_actions.cpp:37` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:46` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:51` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:57` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:67` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:74` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:100` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:172` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:180` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:205` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:222` | 70 | Remove toast; persistent navigator warning explicitly preserves successful object mutation. |
| `desktop/app/main_window_object_actions.cpp:237` | 80 | Remove toast; persistent outcome-uncertain status; reconnect/refresh before retry. |
| `desktop/app/main_window_object_actions.cpp:249` | 75 | Remove toast; persistent navigator guard/action error; preserve inline rename validation. |
| `desktop/app/main_window_object_actions.cpp:285` | 35 | Keep mutation completion toast with object identity; navigator refreshing is separate local loading state. |
| `desktop/app/main_window_object_actions.cpp:292` | 70 | Remove toast; persistent navigator warning explicitly preserves successful object mutation. |
| `desktop/app/main_window_pins.cpp:172` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:204` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:438` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:512` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:547` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:553` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:563` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:568` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:637` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:649` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_pins.cpp:674` | 75 | Remove toast; pins status and existing per-pin resolution states. Retry/save/reveal clears only matching notice. |
| `desktop/app/main_window_workspace.cpp:230` | 55 | Remove toast; separate completion-catalog status/hint. |
| `desktop/app/main_window_workspace.cpp:358` | 75 | Remove toast; saved-files local status. Directory failures include both path and cause. |
| `desktop/app/main_window_workspace.cpp:383` | 75 | Remove toast; saved-files local status. Directory failures include both path and cause. |
| `desktop/app/main_window_workspace.cpp:389` | 75 | Remove toast; saved-files local status. Directory failures include both path and cause. |
| `desktop/app/main_window_workspace.cpp:418` | 75 | Remove toast; saved-files local status. Directory failures include both path and cause. |
| `desktop/app/main_window_workspace.cpp:467` | 75 | Remove toast; saved-files local status. Directory failures include both path and cause. |
| `desktop/app/main_window_workspace.cpp:560` | 65 | Remove progress toast; SQL result footer loading icon and queued/running/cancelling text. |
| `desktop/app/main_window_workspace.cpp:934` | 75 | Remove toast; connection status retains backend error. |
| `desktop/app/main_window_workspace.cpp:939` | 75 | Remove toast; connection status retains backend error. |
| `desktop/app/object_explorer.cpp:641` | 65 | Remove toast; Object footer loading/guard state, preserving Data result origin and Cancel. |
| `desktop/app/object_explorer.cpp:653` | 75 | Remove toast; Object footer loading/guard state, preserving Data result origin and Cancel. |
| `desktop/widgets/export_dialog/export_dialog.cpp:237` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/export_dialog/export_dialog.cpp:272` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/export_dialog/export_dialog.cpp:307` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/export_dialog/export_dialog.cpp:317` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/export_dialog/export_dialog.cpp:346` | 25 / 75 / 50 | Keep explicit success toast with destination; failure persists beside Retry; intentional cancellation neutral and local. |
| `desktop/widgets/history_dock/history_dock.cpp:467` | 65 | Remove progress toast; History footer loading icon and exact operation text; preview hints remain independent from failures. |
| `desktop/widgets/history_dock/history_dock.cpp:469` | 65 | Remove progress toast; History footer loading icon and exact operation text; preview hints remain independent from failures. |
| `desktop/widgets/history_dock/history_dock.cpp:472` | 65 | Remove progress toast; History footer loading icon and exact operation text; preview hints remain independent from failures. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:456` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:499` | 65 | Remove toast; persistent dialog validation fallback; field validation retained. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:521` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:550` | 85 | Remove toast; persistent selectable section/error summary inside Preferences. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:554` | 55 / 25 | Remove loaded-success toast; keep saved completion on parent after dialog closes. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:565` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/preferences_dialog/preferences_dialog.cpp:578` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/profile_dialog/profile_dialog.cpp:721` | 25 / 70 / 80 | Keep explicit successful action completion plus local status; backend warnings use Warning; failures stay local; pending disconnect never Success. |
| `desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:140` | 85 | Remove toast; persistent trust result in profile and review dialog, with inspect/retry actions. |
| `desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:164` | 85 | Remove toast; persistent trust result in profile and review dialog, with inspect/retry actions. |
| `desktop/widgets/profile_dialog/profile_dialog_ssh_trust.cpp:205` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:143` | 85 | Remove toast; persistent approved/unknown/failed/invalidated trust status and appropriate retry actions. |
| `desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:154` | 85 | Remove toast; persistent approved/unknown/failed/invalidated trust status and appropriate retry actions. |
| `desktop/widgets/profile_dialog/ssh_host_key_dialog.cpp:161` | 85 | Remove toast; persistent approved/unknown/failed/invalidated trust status and appropriate retry actions. |
| `desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:100` | 75 | Remove persistent window toast; persistent dialog error with Restore Defaults/Retry context; no shared clearing. |
| `desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:109` | 55 / 25 | Remove loaded-success toast; saved confirmation stays in Query Settings status. |
| `desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:124` | 75 | Remove persistent window toast; persistent dialog error with Restore Defaults/Retry context; no shared clearing. |
| `desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:131` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:135` | 75 | Remove persistent window toast; persistent dialog error with Restore Defaults/Retry context; no shared clearing. |
| `desktop/widgets/query_settings_dialog/query_settings_dialog.cpp:176` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/search_panel/search_panel.cpp:164` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/search_panel/search_panel.cpp:280` | 55 | Remove completion toast; retain replacement count in search-panel status. |
| `desktop/widgets/search_panel/search_panel.cpp:331` | 65 | Remove toast; selectable persistent search error; existing field/resource-limit validation stays inline. |
| `desktop/widgets/search_panel/search_panel.cpp:354` | 55 | Remove completion toast; retain replacement count in search-panel status. |
| `desktop/widgets/value_detail_dialog/value_detail_dialog.cpp:212` | 65 | Remove progress toast; local status line with loading icon until operation finishes. |
| `desktop/widgets/value_detail_dialog/value_detail_dialog.cpp:226` | 75 | Remove toast; complete selectable value error and explicit Retry for same byte offset; no truncation. |

## Verification seams

Public Qt tests cover minimum reading duration, FIFO ordering, pinned recovery, hidden-host startup, hover/focus pause, long-text reading/details, independent status ownership, Start preserving documents, local preferences/query/search/value feedback, value retry, SSH approval/unknown state, neutral export cancellation, export failure/retry, navigator mutation plus follow-up refresh, and independent History preview/error state. Light/Dark gallery uses real StatusLine loading and toast components. Run the full native CTest suite, standalone headers, UI consistency/policy/QSS, C++ ownership, formatting and Python policy tests.

No Rust/domain rules, durable storage, bridge API, network execution, or recovery permissions are changed. Platform-specific native accessibility remains a manual validation boundary.

## Final verification evidence

- Development configure/build passed. The task-local build used `-Wl,-dead_strip` after the fresh native suite exhausted available disk space; only regenerable binaries from this task were removed. Repository build flags were not changed.
- `ctest --test-dir build/dev --output-on-failure -j 4`: **57/57 targets passed**, 102.78 seconds. Existing tests requiring external PostgreSQL/MySQL fixtures skip without those environments.
- `cmake --build build/dev --target choscordb-header-check -j 4`: passed.
- Python CI suite: 98 tests, passed. Python release suite: 141 tests, passed with one skip. These are test fixtures; no release was published.
- UI consistency JSON: no violations; all new construction sites appear in the static census. UI policy, QSS policy, C++ source-size and diff whitespace checks passed.
- C++ ownership: no blocking findings or inventory errors. Reviewed and refreshed four existing presentation exception receipts (SQL highlighting, synthetic gallery SQL, and transient SSH form hydration); their backend behavior did not change. The scan retains 29 existing excepted findings, not new failures.
- Real Light and Dark `status-line` gallery exports inspected: neutral loading icon, compact error text, and accessible Details action fit their surfaces.
- Valid red evidence was observed before corresponding behavior changes for reading-time/queue behavior, hidden-host notices, missing persistent dialog/navigator/trust/export/search feedback, clipped long toast/status text, value loading failure, and preview hints overwriting History error state. Final native tests cover their green outcomes.
- Independent requirements and code reviews approved after resolving findings about hidden-window queueing, clipping, source ownership, stale resolved notices, and History error retention. Main agent reviewed delegated changes and ran decisive verification.

Limitations: this verification used macOS/Qt 6.11.2. Windows/Linux runtime behavior, older macOS compatibility and native screen-reader announcements were not manually exercised. The linker reports pre-existing Rust objects built for macOS 26.5 against a 26.0 deployment target; this UI change does not establish older-OS package compatibility.

## PR preparation verification

A fresh run during PR preparation passed the native build, standalone headers, C++ formatting, UI consistency/policy/QSS, ownership and whitespace checks. CTest passed 54/57 targets in 182.87 seconds; `object-data-workspace` failed row duplication (one row instead of two), `pinning-sidebar` failed its 20 px connection-to-pin gap, and `diagnostics-smoke` reported blocked startup. Two other CTest runs were active on the machine; contention is possible but not established as the cause. No blind retries or test changes were made. Python CI tests passed 98/98; release fixtures ran 141 tests with three errors and one skip, reporting exhausted disk space during temporary-file creation. These latest failures remain unresolved; the earlier passing evidence above describes the prior run.
