# Expandable pins in the Connections sidebar

Status: Ready for implementation
Date: 2026-09-29
Source: User screenshots of the Pinned section and Schema & Objects tree, plus decisions in this brainstorm. This enhancement supersedes the flat-list/no-subtree decision in `docs/specs/2026-09-28-sidebar-schema-object-pinning.md`; that spec remains the source for the existing pin identity, persistence, reveal, and failure contracts where they do not conflict here.

## Outcome and current state

People browsing frequently used database objects can open their children directly beneath a pinned item while retaining the pin's shortcut to its original location. The Pinned heading and first item should have the same compact vertical relationship as other Connections sidebar sections.

Today `desktop/app/main_window_ui_pins.cpp` builds Pinned with `SidebarSection` and a flat `QListWidget`. `desktop/app/main_window_pins.cpp` renders saved `PinRecord` rows and makes a row click/activation reconnect and reveal the original in Schema & Objects. The `NavigatorController` and `NavigatorModel` own the live, lazy-loading object hierarchy and exact identity checks. The original explorer is a `design::NavigationTreeView` in `desktop/app/main_window_ui.cpp`. Pinned is hidden when empty; it stays visible across filtering, disconnects, and restarts when pins exist. Existing focused coverage is in `tests/desktop/pinning_sidebar_test.cpp` and `tests/desktop/pinning_flow_test.cpp`.

## Requirements and decisions

1. Keep Pinned between Connections and Schema & Objects. Reduce the vertical gap from the Pinned caption to its first row so it matches the neighboring section caption-to-content rhythm in both Light and Dark themes. Keep the spacing token based and preserve the compact gap below the last pin before Schema & Objects. Do not change unrelated sidebar spacing to mask the issue.
2. Render each pinned root as an expandable navigation row with the same branch arrow language, indentation, row styling, and object icon semantics as Schema & Objects. Show an arrow only when the pinned object can have children. Leaf pins remain direct shortcuts without a false arrow.
3. The arrow expands or collapses the live descendants **under that pin**. Support the full hierarchy, not just one level or a read-only preview: nested branch arrows, lazy metadata loading, pagination, and the ordinary actions available on corresponding Schema & Objects child rows. The displayed descendants refer to the original live objects; they are not new pins and are not persisted as a copied subtree.
4. Clicking the pinned root row outside its branch arrow keeps the existing behavior: reconnect as needed, clear an obstructing Schema & Objects filter, select and scroll to the exact original object, and honor existing active-work/document guards. Arrow activation only expands or collapses the pinned subtree; it does not itself reveal/select the original or clear the object filter. Ordinary descendant row activation and menus should behave like the same rows in Schema & Objects.
5. If a saved connection is disconnected, activating a pin's arrow starts that connection on demand, then loads children beneath the pin. Restoring pins at startup remains passive. Pins start collapsed after restart; expansion state need not be persisted. Expansion must not change the SQL editor's connection target or remove other visible/selected connections.
6. Keep the existing saved-profile/object identity and exact-match protection. Do not show descendants from a similarly named or reused object if the pin's original is missing or mismatched. Connection, metadata, and navigation failures retain the pin and provide a retryable visible reason through the existing UI pattern. Mark Unavailable only after the existing authoritative identity lookup supports that state. An unavailable pin can still be unpinned, but cannot expand into an unrelated object.
7. Preserve newest-first pin order, pin/unpin behavior, profile rename/delete behavior, tooltips or equivalent connection/context/status information, accessibility names/descriptions, focus, keyboard operation, and scrolling in the shared Connections scroll area. `Filter objects…` continues to filter only Schema & Objects, not Pinned or its expanded descendants.

## Non-goals and rejected alternatives

- No separate permanent metadata copy, offline subtree cache, or automatic startup connection.
- No one-level-only or preview-only dropdown.
- No change that makes a pinned root row click merely toggle expansion; its shortcut behavior remains.
- No pin reordering, search field for Pinned, or new pin storage format unless needed for compatibility with the existing records.

## User-visible flows and edge cases

- A pinned schema or table appears with a branch arrow when it has children. Clicking the arrow opens children directly below it; clicking deeper arrows continues through the normal hierarchy. Clicking an object row or using its menu has the same effect as the corresponding row in Schema & Objects. Clicking the pinned root's label reveals the original in the lower tree.
- A disconnected pin is visible and collapsed at startup. Opening its arrow connects on demand. While loading, show a comprehensible loading state; on connection or metadata failure, keep the pin and offer the normal retry path. Collapsing during loading or switching connections must not attach stale results to the wrong pin.
- A leaf pin has no arrow and its row remains a shortcut. A pin whose original was dropped or no longer matches its saved identity stays visibly Unavailable and removable, without showing another object's descendants.
- Many pins or expanded descendants grow inside the shared Connections scroll area. The Pinned view does not trap its own vertical scrolling or crowd/clip the Schema & Objects section. Pin and descendant context menus remain distinguishable: Unpin on a pinned root removes that shortcut only; actions on descendants target their actual original object.

## Acceptance criteria and public test seams

| Observable outcome | Preferred public test seam |
| --- | --- |
| With one or more pins, the Pinned caption-to-first-row gap matches the adjacent section rhythm at normal and compact sidebar sizes, in Light and Dark; Schema & Objects remains near the last pin. | `MainWindow` widget geometry and rendered sidebar captures using `pinnedSection`, `connectionsPanel`, the saved-connection section, and the navigator section. Compare visible caption/row bounds, not just layout constants. |
| Expandable pins show a correctly directed branch arrow; arrow click expands/collapses under the pin while root row click still reveals the original. Leaves have no arrow. | Drive pointer and keyboard input through the public Pinned view in `MainWindow`; inspect row hierarchy/expansion, `databaseNavigator` current index, and `navigatorFilter`. Use a schema, table, and leaf fixture. |
| Expanded pins expose the same nested live children and ordinary child activation/context actions as Schema & Objects, including lazy and paged rows. Descendants are not new saved pins. | Use `MainWindow` with public `NavigatorModel`/adapter metadata replies; expand multiple levels, activate a child, inspect its visible action/result, and reopen the window to inspect persisted roots. |
| Opening an arrow on a disconnected pin connects on demand; startup stays disconnected; failures and stale replies leave a retryable pin and never show the wrong object's children. | `MainWindow` with temporary pin/profile storage and `QueryWorkspace`/`EngineAdapter` connection and metadata events; include delayed/failing responses, identity mismatch, and retry. |
| Pins and expanded descendants remain independent of `Filter objects…`; the shared sidebar scroll reaches all rows and the lower explorer; keyboard focus, status, and Unpin remain usable. | Public widget input and geometry in `MainWindow`, accessibility properties, filter edits, wheel events, menus, and the existing pinning sidebar/flow tests. |

## Technical constraints and likely affected areas

- Keep database/workflow state in `desktop/app/` or `desktop/widgets/`; reuse the live navigator model/controller behavior and the design system's navigation tree presentation where their contracts fit. Inspect `desktop/design_system/README.md` and call sites before extracting or changing a component. Avoid screen-owned QSS, literal visual values, or an empty wrapper subclass.
- Likely affected areas: `desktop/app/main_window_ui_pins.*`, `main_window_pins.cpp`, `main_window_ui.*`, `main_window_navigator.cpp`, `navigator_controller.*`, and focused pinning tests. Existing tests refer to `QListWidget#pinnedList`; update them to verify behavior through the resulting public view if the widget type changes. Preserve stable object/accessibility names where practical and update the UI consistency census for any new construction pattern.
- Follow the repository UI checklist: `python3 scripts/ci/ui_consistency.py` and `--json`, `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present. If code under `desktop/design_system/` changes, also follow its `AGENTS.md`: real Light and Dark gallery specimen, matching preview test, and design-system verification.

## Rollout, compatibility, assumptions, and risks

Existing stored `PinRecord`s should render as expandable roots without migration; expansion state is transient and starts collapsed. No feature flag or telemetry is requested. Treat the screenshot's spacing request as visual consistency, not an exact pixel count. The full-subtree decision requires care with lazy loading, duplicate object paths, profile switching, stale asynchronous replies, and identity validation. Inferred behavior: an arrow action does not change the lower tree selection or clear its filter, because the user reserved original reveal for the root row click. No material product decision remains open.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and applicable `AGENTS.md` files, then invoke `$implement` with `docs/specs/2026-09-29-expandable-sidebar-pins.md` and implement against the public test seams above.
