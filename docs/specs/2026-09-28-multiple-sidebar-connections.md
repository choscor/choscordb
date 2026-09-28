# Multiple selected sidebar connections

Status: Ready for implementation
Date: 2026-09-28
Source: User screenshot and brainstorm decisions in this session. This spec supersedes the single visible connection rule in `docs/specs/2026-09-19-selected-connection-sidebar.md`; its object hierarchy and lazy metadata behavior still apply.

## Outcome and current state

People browsing several databases can keep their schemas visible together. Each selected saved connection has one root in Schema & Objects. The selected saved connection rows use a green background in both themes.

The app already permits multiple live database sessions, and `NavigatorModel` stores multiple roots. Today `desktop/app/main_window_workspace.cpp` tracks one browsed profile and pending open, the saved-profile list paints one Qt selection, and `SelectedConnectionProxy` in `desktop/app/navigator_controller.cpp` exposes only one root. Search advances through that one root. `desktop/design_system/navigation_profile_row/` paints selected rows gray. Existing tests in `tests/desktop/modern_ui_workspace_test.cpp` assert the one-root behavior and that sidebar browsing leaves the SQL editor target unchanged.

## Decisions and requirements

1. A normal click or keyboard activation on a saved connection row toggles that profile in the visible set. Any number may be selected. A second activation removes only its tree root and green selected state; it leaves an already open database session connected. Selecting an already connected but hidden profile reuses its session and metadata where valid.
2. Each selected profile appears as one connection root, with its existing database, schema, group, and object hierarchy. Roots have stable profile identity and appear in saved-profile list order, regardless of click order. Selecting or removing one profile preserves other roots and their usable metadata and expansion state where practical.
3. Selecting a profile that needs a session shows its `Loading…` root immediately beside the others. On success, that root becomes browsable. On failure, remove only the failed root and its selected state, show one existing sidebar connection failure dialog with the profile name and returned reason, and preserve all other selected roots. A rejected submission has the same visible outcome. Late success, failure, or metadata responses for a removed attempt cannot restore its root or alter another root.
4. The shared Filter objects field searches all selected connection roots, including collapsed groups under each root. Matches retain their connection ancestor. Clearing the filter restores ordinary lazy browsing. Existing search/request limits and stale-result protections remain; a failure or bound reached in one root must not silently imply that other roots were fully searched. Show an existing style of incomplete/refine status when the overall search is incomplete.
5. The selected state in the saved connection list represents tree visibility, separately from keyboard focus, the current tree item, and whether a session is connected. Clicking or focusing a tree node does not remove other selected roots. Right-clicking a saved row to open its menu does not toggle it. Disconnecting a session removes that profile from the visible set and leaves other roots intact. Refresh, disconnect, object opening, context actions, generated SQL, and completion target the connection associated with the invoked node or the SQL editor target, as applicable.
6. `NavigationProfileDelegate` uses the existing semantic green sidebar accent surface for selected rows instead of gray. Preserve readable text, icons, checkmark, hover, and focus indication in Light and Dark. An unselected connected profile has normal row appearance. This is a shared component change, not a screen-owned color rule.
7. The selected set lasts for the current app session. Restarting does not restore the set or automatically reopen databases. Refreshing or editing the saved-profile list during the session preserves selection by profile ID when the profile still exists; deleting a selected profile removes its root. No saved-profile or database migration is needed.

## Scope and behavior boundaries

- Existing SQL editor tabs retain their connection targets when sidebar selection changes. Sidebar selection does not execute SQL or switch an editor target. Generated SQL and object tabs retain their established per-connection behavior.
- Toggling a row off is not a disconnect command. The existing menu and tree disconnect actions remain explicit. A disconnected profile can be selected again to reconnect.
- Connection attempts may be started one after another through the existing workspace API; this feature does not require simultaneous network handshakes or change the workspace's active-query guard. If a request is rejected because work is active, retain other selections and show the profile-specific failure reason.
- No cross-database object operations, combined result sets, or persisted workspace selection are part of this work.
- The navigator footer may remain a simple aggregate status. With zero selected roots it reflects no browsed database; with one or more usable roots it must not claim that the entire explorer is disconnected. Root-local loading and errors carry profile context.

## User-visible flows and edge cases

- Select A, then B: both rows are green and the tree shows A and B roots. Select A again: B remains selected and visible, while A's session stays open. Select A again: its root returns without opening a duplicate session.
- Select B while A is visible and B must connect: A remains interactive and B shows `Loading…`. If B fails, A remains visible and B returns to normal appearance; the failure dialog names B and gives the returned reason.
- Search with A and B selected: matching objects from either database appear beneath their own root; rapid filter changes, toggles, disconnects, or delayed metadata cannot mix results between roots.
- Disconnect A by its explicit action: A's row and root are cleared, B remains. Removing all selections restores the empty Schema & Objects message.
- Refresh the saved profile list or edit a profile: selection follows stable profile IDs, not list row positions. Deleting a selected profile removes its root without changing another profile's selection.

## Acceptance criteria and public test seams

| Observable criterion | Preferred public test seam |
| --- | --- |
| Any number of rows can be toggled independently; the tree shows exactly one root per selected profile in saved-list order; toggling off does not disconnect. | `MainWindow` with saved profiles and `savedConnections`/`databaseNavigator` widgets, plus adapter connection events. Replace the old one-root assertion in `modern_ui_workspace_test.cpp`. |
| Existing live sessions are reused, and SQL editor targets and query execution remain unchanged by sidebar toggles. | `MainWindow` UI actions, `connectionSelector`, editor target, and adapter connection/query events. |
| A pending root coexists with usable roots; success replaces only that root; failure or rejected submission removes only it and shows one reason-bearing dialog. Late events cannot resurrect it. | `MainWindow` driven through saved-row activation and public adapter event/submission seams, observing list, tree, and dialog. |
| A shared filter finds objects under every selected root, including collapsed groups; clear restores the tree; bounded, stale, and partial searches report accurately. | Filter widget, `NavigatorController`/tree, and public metadata request/reply signals with delayed or reversed responses. |
| Tree and menu actions operate on their own connection; disconnecting one leaves the others; keyboard activation and focus remain usable. | `MainWindow`/`NavigatorController` UI actions and observable adapter requests/events. |
| Selected rows have a semantic green background with legible content and visible focus in Light and Dark, while unselected rows retain normal appearance. | Real `NavigationProfileDelegate` gallery specimen in both themes, matching `preview_test.cpp` check, and a focused component rendering/state test where useful. |
| Profile refresh/edit preserves surviving selection by ID, deletion clears only the deleted profile, and restart does not restore selection. | `MainWindow` with public profile save/list/delete events and a new window against the same temporary profile store. |

## Technical constraints and likely affected areas

- Keep connection/session identity distinct from selected profile identity and from the tree's current index. Adapt the single-selection fields and logic in `desktop/app/main_window.h` and `desktop/app/main_window_workspace.cpp`, plus the selected-root proxy and multi-root search in `desktop/app/navigator_controller.*`. Preserve `NavigatorModel`'s lazy loading, token checks, and loaded metadata for other roots.
- Keep workflow state in `desktop/app/`; keep row appearance in `desktop/design_system/navigation_profile_row/` and semantic colors in `desktop/design_system/colors/`. No screen-owned QSS, literal UI colors, or empty component wrappers.
- Preserve object names, accessibility names, keyboard focus, and existing signals where possible. Ensure the selected rows and any loading/error state are understandable without relying solely on color.
- Follow `desktop/design_system/AGENTS.md` for the design component edit: show selected and unselected real rows in Light and Dark gallery specimens and update matching preview tests. Run `python3 scripts/ci/ui_consistency.py` (inspect `--json`), `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present.

## Risks and deferred choices

- The current controller has one pending browse ID and one search cursor. Implementation must prevent overlapping or stale attempts from changing unrelated roots. Whether attempts are internally queued or independently tracked is an implementation choice; the visible behavior above is required.
- Search across many large catalogs can reach existing request limits. Keep work bounded and report an incomplete search rather than presenting partial results as complete. The exact scheduling and limit remain implementation choices.
- No specific green hex value is prescribed. Use the existing semantic sidebar accent token and verify contrast in both themes.

## Fresh-session instruction

Read this entire spec and inspect the current workspace, including any uncommitted changes. Then invoke `$implement` with `docs/specs/2026-09-28-multiple-sidebar-connections.md` and implement it using the public test seams above.
