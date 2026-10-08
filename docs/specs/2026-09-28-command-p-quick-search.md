# Cmd/Ctrl+P quick search and navigation

- Status: Confirmed for implementation
- Date: 2026-09-28
- Source: User request and brainstorm decisions on 2026-09-28.

## Outcome and conversation summary

Give keyboard users one fast way to find and navigate the app. Cmd+P on macOS and Ctrl+P elsewhere opens a compact search overlay near the top center of the main window. The user asked to build its reusable presentation in the shared design system first, then integrate it into the app. Results cover main screens, open tabs, database objects in the selected connection, text in every open SQL editor, and all saved query history. View → Quick switch… opens the same overlay.

## Current state and workspace findings

- `desktop/app/main_window_ui.cpp` creates four View screen actions and a Quick switch menu. It creates editor Find/Replace actions separately. `desktop/app/main_window.cpp` owns `showScreen`, tab switching, object opening, and the active-work guard. The app's shortcut catalog is in `desktop/app/editor_preferences.cpp`.
- `desktop/widgets/search_panel/` is editor-only Find/Replace, with existing mutation and stale-result protections. Preserve it and its standard Find shortcut.
- `desktop/app/navigator_controller.cpp` searches the selected connection through a lazy `NavigatorModel`, including collapsed branches, with request/visit limits and incomplete-search status. Search is currently coupled to the sidebar filter; a palette search must not silently alter that filter or its tree selection.
- `desktop/widgets/history_dock/` filters only the loaded history page. `EngineAdapter::listHistory` pages saved entries. The storage history API in `crates/storage/src/lib.rs` returns bounded pages, but has no full-history text search endpoint. History selection already has an app path that opens or reuses a SQL tab.
- `desktop/design_system/README.md` maps existing modal presentation, fields, lists, and rows. `ModalDialog` is a centered shell; `RightSheet` is right aligned. The new palette needs a reusable, compact centered presentation and a Light/Dark gallery specimen.

## Requirements and decisions

1. Add one application action for quick search, defaulting to Cmd+P on macOS and Ctrl+P elsewhere. Register it with the existing shortcut preferences and conflict validation. The View → Quick switch… action opens the same overlay. Keep editor Find/Replace and the sidebar/history filters independent.
2. Create a design-system quick-search component before app integration. It owns themed compact overlay presentation, search field, result row/list presentation, loading/empty/error/status states, focus capture/restoration, accessible labels, and common keyboard/pointer interaction. The app owns result sources, ranking, recent destinations, database requests, and activation. The shared component must have no app, database, QScintilla, or Rust dependency. Reuse existing design controls, modal presentation, tokens, and styles where their contracts fit; no empty wrapper subclasses or screen-owned QSS.
3. Searchable destinations are the four main screens, every open workspace tab, objects across the **selected connection** including unloaded/collapsed schemas, every matching location in every open SQL editor, and **all** saved query history. A SQL text result shows its tab, short snippet, and line number; activation switches to that editor and selects or places the cursor at the match. History search includes full SQL text, not only the current loaded history page. History results open through the existing history-to-editor behavior; object results open through the existing object-tab behavior.
4. Use forgiving, ordered name matching for screens, tab titles, and object names, including separated query words. Match SQL editor and saved history text by literal case-insensitive substring. Show one ranked result list with visible type labels and enough context to distinguish duplicate names, schemas, tabs, and history entries. Strong destination-name matches rank ahead of body-text matches; stable tie breaking prevents visible jitter. Do not hide entire sources behind category tabs.
5. With an empty query, show main screens and recent destinations: open tabs, recent saved history, and objects visited during the current app session. Do not add persistent recent-object storage. Reset the input on each fresh open. Bound displayed results and tell the user when results are incomplete or they should refine the query; exact limits and ranking weights are implementation choices.
6. Focus the input on opening. Typing updates results without blocking the UI. Up/Down changes the active row, Enter activates it, Escape closes, and pointer selection also activates. Keep focus within the overlay while open, expose selected row and statuses accessibly, and restore the previous valid focus on dismissal. Reopening an already open overlay focuses its input rather than stacking overlays.
7. Search requests and local scans are bounded and stale-safe. Rapid typing, closing the overlay, changing the selected connection, changing/closing an editor, or clearing history must not display or activate an obsolete result. Show partial failure per source when useful while retaining valid results from other sources. A failed or guarded navigation leaves the user in the current workspace and shows an understandable reason; do not execute SQL, change an editor buffer, or change the selected connection as a side effect of search.

## Explicit non-goals

- Replacing SQL editor Find/Replace, sidebar object filtering, or the history page filter.
- Searching database row values, files not open as tabs, stored workspace documents not currently open, or objects in unselected connections.
- Executing SQL, editing query text, or changing an editor's database target from a quick-search result.
- Persisting search text, search results, or recent-object activity across restarts.

## User-visible flows and edge cases

- Cmd/Ctrl+P or View → Quick switch… opens the same overlay from any main screen. Empty input immediately shows useful navigation. Closing it with Escape restores the prior focus and leaves the workspace untouched.
- Typing an object name can reveal a match under a collapsed schema in the selected connection. The row includes connection/schema/kind context. Selecting it opens or focuses its object tab. A connection switch invalidates old object rows and pending responses.
- Typing SQL text can show several locations, including in inactive tabs. Each row carries a tab name, line, and short snippet. Selecting one jumps to the correct live editor revision; an edited or closed tab's stale result is rejected or refreshed.
- A history match can come from any retained saved record, including one beyond the visible history page. Activating it uses the existing history open/reuse path and does not run it. If history is empty, disabled, being cleared, unavailable, or fails to search, the overlay communicates that state without masking other result types.
- No match is a clear empty state. A limit or source error gives a refine/retry message. A result that is no longer valid cannot navigate to a different object or document. Existing active-operation/recovery guards remain authoritative; blocked activation reports the reason and leaves the overlay usable.

## Acceptance criteria and public test seams

| Observable criterion | Preferred public test seam |
| --- | --- |
| The shared component renders in Light and Dark, exposes field/results/status with accessible names and state, and handles arrows, Enter, Escape, focus restoration, and pointer activation. | Design-system gallery specimen in `desktop/tools/preview/preview_window.cpp`, matching `tests/desktop/preview_test.cpp` check, and widget interaction test through its public API. |
| Cmd/Ctrl+P and View → Quick switch… open one overlay on each platform; shortcut customization follows the existing preferences rules; editor Find remains separate. | `MainWindow` QAction/shortcut and preference integration tests, exercising the menu and keyboard path. |
| Empty input shows screens and recent destinations without persistent recent-object data. | Main-window UI test using open tabs, selected object, and history responses; reopen and restart seams. |
| Name queries return ranked, labeled screen/tab/object results from only the selected connection, including collapsed objects; activating each reaches the intended destination. | Main-window/controller test via visible result rows, `NavigatorModel` metadata request/reply seam, and normal tab/screen activation. |
| Text queries return snippets and line numbers for matches across open SQL tabs; selection moves to the correct match without changing text or running SQL. | Main-window test through real `SqlEditor` public selection/cursor state and tab state. |
| A saved history record beyond the currently loaded history page is found and opens through the existing history path without execution. | Storage/core/bridge history search API test with retained records beyond one page, plus main-window adapter event/activation test. |
| Typing, source replies, tab edits/closure, connection switches, history clear, and overlay close cannot surface or activate stale results; partial/limit/error states are visible and other sources remain usable. | Controlled adapter/model replies and editor changes through the public overlay and `MainWindow` UI, plus backend query bounds tests. |
| Active-work or recovery guards block navigation cleanly and preserve the current document/connection. | Main-window integration test invoking a result while navigation is guarded. |

## Technical constraints and likely affected areas

- Start with the design-system component and its gallery/preview test, then wire app behavior. Follow `desktop/design_system/CLAUDE.md`, the component ownership map, and `cmake/DesktopComponents.cmake`. New component construction must appear in `python3 scripts/ci/ui_consistency.py --json`; extend its census and regression test if the construction pattern is new. Preserve existing object names, signal contracts, focus behavior, and screen accessibility.
- Likely app areas are `desktop/app/main_window_ui.cpp`, `main_window.cpp`, a quick-search controller/widget under `desktop/app/` or `desktop/widgets/`, `NavigatorController`/`NavigatorModel`, and shortcut preferences. Reuse existing object and history activation paths rather than duplicating their guards.
- Full saved-history search needs a bounded query across retained records through storage → core → bridge → `EngineAdapter`; do not fetch every history page into the UI merely to search. Return enough identity and context to activate a record safely, with cancellation/generation checks at the UI boundary. Respect the existing history retention and byte limits. Keep sensitive SQL/history text out of diagnostic logs.
- Object search may reuse or factor the navigator's bounded lazy metadata traversal, but it must have independent query state so opening the overlay does not rewrite the sidebar filter. Long editor scans need a snapshot/revision check and must keep the event loop responsive.
- Verify with `python3 scripts/ci/ui_consistency.py` (and `--json` for census), `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, the design-system gallery in Light/Dark, the relevant native CTest targets, and the full native suite when dependencies are present.

## Rollout, compatibility, risks, and deferred choices

- No data migration. Existing history retention, shortcut overrides, editor settings, and workspace recovery formats remain valid. Quick switch gains the new overlay; its menu action stays available.
- Exact result cap, debounce interval, ranking weights, visual dimensions, and history query mechanism are deferred implementation choices. Choose documented finite bounds, stable ordering, and visible partial-result feedback. Do not make unbounded database or UI-thread scans.
- “Recent” means the current session for objects, existing open tabs, and retained history ordered by recency. This is an explicit assumption to avoid adding a new persistence contract.
- Main-screen destinations remain subject to `showScreen` behavior; for example Start may be unavailable with open tabs. Preserve that rule and explain blocked activation rather than silently discarding the selection.

## Fresh-session instruction

Read this entire spec and inspect the current workspace before changing code. Invoke `$implement` with `docs/specs/2026-09-28-command-p-quick-search.md` and implement the confirmed behavior with tests at the public seams above.
