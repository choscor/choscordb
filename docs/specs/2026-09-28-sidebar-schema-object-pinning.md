# Sidebar schema and object pinning

Status: Ready for implementation
Date: 2026-09-28
Source: User request and brainstorm decisions in this session. Read alongside `docs/specs/2026-09-28-multiple-sidebar-connections.md` and `docs/specs/2026-09-28-table-view-context-drop-rename.md` where those features affect the current workspace.

## Outcome and current state

People can keep shortcuts to frequently used schemas and database objects in a Pinned section of the Connections sidebar. Pinning creates a shortcut; the original item remains in Schema & objects and behaves normally.

The Connections sidebar currently places saved connections directly above the `Schema & objects` `SidebarSection` in `desktop/app/main_window_ui.cpp`. `NavigatorController` owns the tree context menu and maps a visible tree index to `NavigatorModel`, whose rows carry connection, object ID, qualified name, kind, and parent context. The navigator loads metadata lazily and filters selected connection roots. `MainWindow` tracks saved profile IDs separately from live connection IDs; saved profile selection is session only. No existing object-pin feature or pin store was found. Object tabs already use saved profile IDs to rebind after reconnect. The app has a successful table/view Rename and Drop completion path in `desktop/app/main_window_object_actions.cpp`.

## Requirements and decisions

1. Add a **Pinned** section between **Connections** and **Schema & objects** on the Connections sidebar page. Keep it visible when no connection is selected. Show a clear empty state when there are no pins. Render pins as a flat list, newest first; persist this order. A pinned schema is one shortcut row, not a copied subtree.
2. Offer **Pin** in the right-click menu for schema rows and actual database-object rows, including tables, views, indexes, sequences, functions, columns, and key/constraint objects that the navigator exposes. Exclude connection roots, database roots, synthetic type groups, loading/error/load-more rows, and other navigation-only rows. Only objects belonging to a **saved connection profile** can be pinned. For an eligible object under an unsaved session, leave Pin unavailable and explain that the connection must be saved first.
3. Pin once per logical object per saved profile. The same database object shown through two navigator paths, such as a schema-level and table-level index, maps to one pin. An already pinned original row offers **Unpin** instead of another Pin. The pinned row's right-click menu offers **Unpin**. Pinning and unpinning affect the shortcut only, never the original navigator row, database object, session, or SQL editor target.
4. Persist pins locally across app restarts using saved profile identity plus a validated object identity and enough display/ancestor context to restore and disambiguate the row. Do not persist live connection IDs or credentials, and do not automatically connect when restoring pins. Support existing installations with an empty pin set; no database migration or feature flag is required. A pin mutation that cannot be saved must report the failure and leave the prior pin state intact.
5. Activating a pin reveals its saved connection in Schema & objects. If the profile is hidden, add it to the visible set without removing other selected connections. If disconnected, start the existing saved-profile connection flow on demand. Then expand/load the necessary ancestors, clear an active object filter if needed to reveal the row, select and scroll to the original item. Ordinary navigator selection behavior applies: selecting an object may open its details; selecting a schema does not create an object tab. Preserve SQL editor connection targets. Activation must honor the app's existing document-change and active-work guards; if navigation or connection cannot proceed, show a reason and retain the pin.
6. Keep pins visible regardless of the `Filter objects…` value; that field filters only Schema & objects. Use a compact, scrollable pinned list that does not crowd out the object tree. Show the object name, kind, and enough saved-connection/schema/parent context to distinguish duplicates, including overloaded functions and like-named objects on different profiles. Make disconnected, loading, unavailable, and focused states understandable without color alone.
7. A disconnected or hidden profile does not make its pins unavailable. A connection/metadata failure leaves the pins in place and gives a retryable error through the existing UI pattern. If an authoritative lookup shows the original object no longer exists or no longer matches its stored kind and identity, retain the pin as **Unavailable**, prevent it from opening a different object, and allow Unpin. Do not classify an object as missing merely because its group is collapsed, metadata is still loading, a page is not loaded, or a request failed. Deleting a saved profile removes its pins. Editing/renaming a saved profile keeps its pins by profile ID and updates their displayed profile name.
8. When ChoscorDB's table/view Rename action succeeds, update a pin for that object to the new identity and name. A failed or canceled rename leaves the pin unchanged. An external rename leaves the old pin unavailable when checked; the user may unpin and pin the new object. A successful in-app Drop leaves any pin to the dropped object unavailable and removable. Do not silently retarget a pin to a newly created object that reuses a name or internal ID.

## Non-goals and rejected alternatives

- No copied schema subtree or second metadata browser in Pinned; schema pins navigate to the original tree.
- No pinning of unsaved sessions, connection/database roots, or synthetic groups.
- No automatic connection on startup, no persistent selection of visible connection roots, and no change to the SQL editor target when a pin is activated.
- No pin search tied to `Filter objects…`, drag-and-drop reordering, cross-device sync, or automatic removal of missing-object pins.

## User-visible flows and failure behavior

- Right-click a schema or object in Schema & objects and choose Pin. A new row appears at the top of Pinned; the original row stays in its tree location. Reopen the original menu to see Unpin. Right-click the shortcut to Unpin; either path removes only the shortcut.
- Restart the app. The ordered shortcuts appear with saved display context while their profiles remain disconnected. Activating one reconnects only as needed, reveals its profile alongside any other selected profiles, loads the ancestry, selects the original row, and scrolls it into view.
- Activate a pin while `Filter objects…` is active. The filter clears as part of navigation, other pins remain visible, and the original row is revealed. A schema shortcut selects the schema; an object shortcut follows established detail-opening behavior.
- Two profiles contain `public.orders`, or a function name is overloaded. Their pin labels/context distinguish them. Repeated Pin attempts for the same underlying object do not create duplicates, even when it appears under multiple navigator paths.
- A connection attempt fails, a catalog page fails, or the requested object is missing. Keep the pin. Report connection/catalog failures with a retry path. Mark the pin Unavailable only after reliable object absence or identity mismatch; activating an unavailable pin must not open a different object.
- Rename a pinned table/view through ChoscorDB. Only a confirmed successful rename changes its pin. Delete a saved profile and only that profile's pins disappear. Rename a saved profile and its pins retain their order and update their displayed context.

## Acceptance criteria and public test seams

| Observable outcome | Preferred public test seam |
| --- | --- |
| Eligible saved-profile schema/object context menus expose Pin, then Unpin; excluded and unsaved-session rows cannot create pins; the original navigator row remains. | `NavigatorController::populateContextMenu`, `MainWindow`'s `databaseNavigator` and Pinned widgets, with public model rows and profile/session setup. |
| One logical object yields one shortcut across duplicate tree paths; pins are newest first, disambiguated, and removable from either menu. | Drive tree and pinned-row menus through `MainWindow`; inspect public item text/data and row count across profiles and duplicate index/function fixtures. |
| Pins persist in order across a new window/process using the same temporary app storage; restoration does not connect or change selected roots; write failure does not claim success. | `MainWindow` with its injectable storage path/profile store, observable saved state, connection events, and visible pinned rows. |
| Activating a pin reveals its profile without hiding other roots, connects on demand, clears an obstructing filter, selects/scrolls to the exact original row, and keeps the SQL editor target. | `MainWindow` widget activation and public adapter connection/metadata events; inspect `databaseNavigator`, `navigatorFilter`, selected saved profiles, and editor target. Include delayed replies. |
| Missing or mismatched objects stay visibly Unavailable and cannot open another object; temporary connection/metadata failure retains a retryable pin. | Public adapter metadata request/reply and failure seams, pinned-row activation, visible status/menu, and object-tab count. Cover paged and collapsed ancestors before declaring absence. |
| Successful in-app Rename retargets a pin, failed/canceled Rename does not; successful Drop makes its pin unavailable; profile rename preserves pins and profile deletion removes them. | Existing Rename/Drop UI and adapter query completion path, profile save/delete events, and visible pinned rows after refresh/restart. |
| Pinned list, menus, empty and status states remain keyboard accessible and readable in Light and Dark; the object filter affects only the explorer. | `MainWindow` focus/keyboard and widget-state tests, UI consistency census, and real design-system gallery specimen/test if a shared component is changed. |

## Technical constraints and likely affected areas

- Keep pin workflow and storage coordination in `desktop/app/` or `desktop/widgets/`; reuse `SidebarSection`, shared tree/list/menu styles, semantic icons, colors, typography, and spacing. Do not introduce screen-owned QSS or an empty design-system subclass. Check `desktop/design_system/README.md` and component call sites before extracting a shared component. Any new construction pattern must appear in the `python3 scripts/ci/ui_consistency.py --json` census.
- Likely areas are `desktop/app/main_window_ui.*`, `main_window_workspace.cpp`, `main_window_navigator.cpp`, `main_window_object_actions.cpp`, `navigator_controller.*`, and `desktop/models/navigator_model.*`, plus storage integration and focused desktop tests. Inspect the current code before choosing ownership or a storage format.
- Pin identity must survive reconnect and distinguish profiles, schemas, object kinds, parent paths, and overloaded signatures as needed. Do not rely on a live session ID or display label alone. Resolve through existing lazy metadata with bounded requests, pagination, stale-response protection, and exact identity checks. Avoid a full-catalog scan at startup. Preserve existing navigator object names, accessibility names, focus, signal, and context-menu behavior.
- Follow the root `AGENTS.md` UI verification checklist: `ui_consistency.py` and `--json`, `ui_policy.py`, `qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present. For changes under `desktop/design_system/`, follow its `AGENTS.md`, including real Light and Dark gallery specimens and matching tests.

## Rollout, assumptions, and risks

- Pin storage is an additive local preference; existing installations start with no pins. A malformed or unsupported pin record must not crash startup or create an actionable shortcut to a wrong object. Recovery/reporting can follow existing local-preference patterns.
- The exact persistence format, pinned-list widget, cache strategy, and capped-list layout are implementation choices. No user-facing pin count limit was requested; keep rendering and lookup bounded without silently discarding saved pins.
- The chosen activation contract implies clearing the explorer filter to reveal an original row. It does not imply changing unrelated profile selections or SQL editor targets.
- Some driver object IDs may be path-derived while others may be server IDs. Reconnect, refresh, rename, duplicate index paths, and identifier reuse require careful identity verification. An uncertain match stays unavailable/retryable instead of opening an unrelated object.
- No material product decisions remain open.

## Fresh-session instruction

Read this entire spec and inspect the current workspace, including applicable `AGENTS.md` files and any uncommitted changes. Then invoke `$implement` with `docs/specs/2026-09-28-sidebar-schema-object-pinning.md` and implement it using the public test seams above.
