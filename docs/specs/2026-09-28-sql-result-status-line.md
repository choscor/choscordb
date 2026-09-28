# SQL result status line

Status: Ready for implementation

Date: 2026-09-28

## Source and outcome

This spec comes from the user's two annotated app screenshots and the brainstorm decisions on 2026-09-28. Make the SQL result footer communicate the active SQL document's connection state through its background, remove the persistent connection label below the navigator, and make completed result summaries easier to scan.

## Current state

- `desktop/app/main_window_ui.cpp` creates `sqlResultFooter` with a fixed `subtleAccent` background. Its left `executionSummary` label contains one combined result string, and the page buttons sit at the far right.
- `desktop/app/query_workspace_view.cpp` prefixes execution details with `resultOrigin_`. `desktop/app/query_workspace_events.cpp` builds completed summaries containing duration, page, row count, and visible size.
- `desktop/app/main_window_ui.cpp` also creates `navigatorStatus` below the object tree. `main_window_workspace.cpp`, `main_window_lifecycle.cpp`, and `main_window_navigator.cpp` use it for Connected/Disconnected/Loading text and navigator search messages. The navigator connection may differ from the active SQL document's connection, explaining the contradictory labels in the first screenshot.
- `QueryWorkspace` already tracks the selected SQL document connection and whether it is available. Existing UI tests inspect `executionSummary`, `sqlResultFooter`, and `navigatorStatus` by object name.

## Requirements and decisions

1. Color the entire SQL result footer using the active SQL document's **available** selected connection: a semantic green success surface when available, and a semantic red danger surface when absent, disconnected, or still opening. Update it when the active document changes, its target changes, a connection opens or closes, and the theme changes. A query failure, cancellation, or running state does not by itself change the connection color. Keep text and controls legible in Light, Dark, and high contrast modes.
2. Remove the persistent Connected, Disconnected, and Loading line below the navigator. Keep navigator search progress and warnings visible in that location only while a search message exists; hide the row otherwise. Preserve meaningful accessibility announcements and existing search behavior. The result footer does not follow the navigator's browsing connection or any other connected profile.
3. Split completed result information into two visual groups on one row: result source and execution outcome on the left; duration, page, row count, and visible size on the right, immediately before the existing previous/next page buttons. Show only metrics that apply to the current result; avoid stale values across runs, pages, errors, or document changes. Keep the current factual content, units, and page navigation behavior.
4. At narrow widths, keep the execution outcome and page buttons visible. Elide a long source name, then progressively omit lower priority right-side metrics as space requires; preserve the full current summary in a tooltip and accessible text. Do not allow controls to be clipped or add horizontal scrolling or a second footer line.
5. Preserve the meaning and visibility of queued, running, cancelling, completed, failed, cancelled, and disconnected execution states. Preserve existing object names, focus behavior, page button signals, and accessible names where their contracts still fit. Do not add a new reusable design component unless the ownership map and call sites establish a repeated contract.

## Non-goals

- No changes to database connection lifecycle, query execution, result pagination, or the sidebar's profile selection behavior.
- No replacement of query messages, execution history, or other status surfaces.
- No exact color values or screen-owned QSS; use design-system semantic roles and theme tokens.

## User-visible flows and edge cases

- On startup or with an SQL document lacking an available target, the footer is red, including when another profile is connected for sidebar browsing.
- After the active document's target becomes available, the footer turns green. Switching to an unconnected document turns it red; switching back restores green. Disconnecting or reconnecting updates it promptly.
- A failed SQL statement on a still-connected target leaves the footer green while the execution outcome says Failed. A connection failure or disconnect turns it red.
- A completed result shows source and outcome at left, with applicable metrics at right before pagination. A command without a result page omits page metrics. Pagination updates page-related values without retaining obsolete values.
- Searching navigator objects temporarily reveals search progress or warnings below the tree. Clearing the message hides that row, with no persistent connection label.
- Narrowing the window preserves outcome and page controls; hidden or elided details remain discoverable through the full summary tooltip and accessible text.

## Acceptance criteria and public test seams

| Observable acceptance criterion | Test seam |
| --- | --- |
| The footer is red when the active SQL document has no available selected connection and green when it does, including after document switches and disconnect/reconnect. Sidebar-only connections do not make it green. | Drive real document/connection actions through `MainWindow` and `QueryWorkspace` in native UI tests; inspect `sqlResultFooter`'s resolved palette/background after events. |
| Query execution failure with a live connection retains the green footer while the visible execution state becomes Failed. | Execute a failing statement through the Run action; inspect the footer and `executionSummary` through the widget tree. |
| Connected results separate source/outcome from duration/page/rows/size, with metrics before the page buttons, and update or omit metrics as result types change. | Run a query and a non-result command; inspect visible label text and widget geometry by object name, then page a result and inspect the updated labels. |
| At a narrow window width, the outcome and page buttons remain visible and clickable, and the complete summary is available to tooltip and accessibility consumers. | Resize the real `MainWindow` to its supported narrow size; inspect widget visibility/geometry, button clicks, tooltips, and accessible names. |
| The navigator has no persistent connection label, but search progress and warnings appear while relevant and disappear afterward. | Use the navigator's filter/search path in native UI tests; inspect `navigatorStatus` visibility and text while search signals change. |
| Light, Dark, and high contrast themes keep the state colors and footer content legible. | Switch `ThemeManager` modes in native UI tests, inspect semantic resolved colors/contrast and footer palette; visually review both standard themes. |

## Technical constraints and likely affected areas

- Follow the root `AGENTS.md` UI rules. If design-system code changes, also follow `desktop/design_system/AGENTS.md` and its Light/Dark gallery specimen and matching test requirements.
- Likely affected areas: `desktop/app/main_window_ui.cpp`, `main_window_workspace.cpp`, `main_window_lifecycle.cpp`, `main_window_navigator.cpp`, `query_workspace_view.cpp`, `query_workspace_events.cpp`, and related widget wiring/tests. Inspect current code before choosing exact boundaries.
- Avoid parsing the display string to recover metrics. Keep data and presentation synchronized through an explicit status representation or equivalent existing public flow.
- Run `python3 scripts/ci/ui_consistency.py` (and inspect `--json`), `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, and relevant native CTest targets; run the full native suite when dependencies are present.

## Rollout, risks, and assumptions

- No persisted data, migration, or staged rollout is needed.
- The footer's red/green state means **SQL target availability**, not query success. Use the existing semantic success/danger surfaces; high contrast may map them to system colors while retaining accessible state text.
- The exact order for omitting individual metrics at narrow widths is an implementation choice. Prefer removing visible size first, then row count, then duration; keep page context when pagination is relevant. The full summary must remain available.
- Preserve `navigatorStatus` as the search feedback widget if that keeps existing tests and accessibility contracts intact; hide it when it has no search message.

## Fresh-session instruction

Read this entire spec, inspect the current workspace, and invoke `$implement` with this spec path: `docs/specs/2026-09-28-sql-result-status-line.md`.
