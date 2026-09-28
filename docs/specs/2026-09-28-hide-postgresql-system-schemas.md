# Hide PostgreSQL system schemas in Schema & Objects

Status: Ready for implementation
Date: 2026-09-28
Source: User screenshot of Schema & Objects and brainstorm decisions in this session.

## Outcome and current state

People browsing PostgreSQL databases should see application schemas first, without catalog and temporary schemas filling the tree. The screenshot shows `pg_catalog`, several `pg_temp_*` and `pg_toast*` schemas beside `app`, `audit`, `notifications`, and `public`.

`crates/driver-postgres/src/metadata.rs` currently returns every row from `pg_catalog.pg_namespace` for a database. `NavigatorController` passes that metadata into `NavigatorModel`, whose selected-connection proxy only filters connection roots and text. Tree search traverses loaded and collapsed schema nodes. SQL completion takes a snapshot from `NavigatorModel`, so hiding rows only in a view proxy would leave system-schema suggestions in completion. Preferences are shown in `desktop/widgets/preferences_dialog/` and use persisted settings through the bridge and storage layer. Existing stored editor preferences use a versioned, strict JSON shape.

The existing hierarchy, lazy loading, bounded search, multiple visible connection roots, and per-editor connection target remain in place. See `docs/specs/2026-09-19-selected-connection-sidebar.md` and `docs/specs/2026-09-28-multiple-sidebar-connections.md` for those contracts.

## Decisions and requirements

1. Hide PostgreSQL-owned schemas by default in Schema & Objects. This includes `pg_catalog`, `information_schema`, `pg_toast`, `pg_toast_temp_*`, and `pg_temp_*`; cover any other PostgreSQL-owned namespace names returned by supported server versions. Match schema identity, not arbitrary object names containing `tmp` or all user schemas with similar words. Keep ordinary schemas such as `app`, `audit`, `public`, and extension or user-created schemas visible.
2. Add a clearly labeled **Show system schemas** checkbox in Preferences, in a database browsing or Connections section. It is off by default, applies to every PostgreSQL connection, and persists across app restarts. Restore defaults turns it off. The existing Save preferences action commits it; Close or cancellation discards an unsaved change. A successful save updates already open trees and completion without reconnecting. A failed save leaves the active value unchanged and follows the existing Preferences error flow.
3. With the setting off, hidden schemas and their descendants are absent from ordinary tree browsing, the Filter objects search results and traversal, and SQL metadata autocomplete. Turning the setting on makes them available through the existing lazy tree, search, and completion behavior. Changing it back hides them again, including previously loaded children and any active matching search results. Text search does not override the setting.
4. This is a PostgreSQL browsing preference. SQLite and MySQL trees and completions keep their existing schema behavior. Direct SQL, including explicit references to system schemas, continues to execute normally. PostgreSQL metadata/inspection APIs must remain capable of addressing system objects by ID; the display preference must not make those operations invalid.
5. Preserve object IDs, qualified names, tree ordering, selection/focus behavior where the selected object remains visible, context actions, request tokens, pagination bounds, stale-response handling, and the per-editor completion target. When the selected item becomes hidden, move selection to a visible ancestor or clear it safely. A search limited by existing caps or a metadata failure still reports its existing incomplete/error state for the visible search space.

## User-visible flows and edge cases

- On first launch or with an older stored settings record, expanding a PostgreSQL database shows application schemas and `public` without the catalog, temporary, toast, or information schemas. User schemas with `temp` or `tmp` in their names remain visible.
- Saving **Show system schemas** in Preferences reveals PostgreSQL system schemas under every open PostgreSQL database without reopening the connection. The setting survives a restart; switching it off hides them again.
- Searching `pg_temp` while the setting is off yields no system-schema match and does not load its descendants. With the setting on, the same search can find those schemas subject to normal bounds. Changing the setting during or after a search cannot restore hidden rows through a late response.
- Metadata completion omits a hidden schema and objects beneath it while the setting is off, even if they were loaded before the setting changed. When on, loaded system metadata can contribute suggestions. Completion still uses the SQL editor's own connection target, which may differ from the connections shown in the sidebar.
- A PostgreSQL query that explicitly references `pg_catalog` still works with the setting off. Inspecting an already open system-object tab or using its ID is not broken by changing the browsing preference.
- A failed preference save or a closed dialog without saving does not change tree or completion visibility.

## Acceptance criteria and public test seams

| Observable criterion | Preferred public test seam |
| --- | --- |
| Default PostgreSQL tree excludes system schemas while showing ordinary and user schemas; similarly named user objects stay visible. | PostgreSQL metadata fixture plus `NavigatorController`/`QTreeView` through metadata request and reply events. Include `pg_catalog`, `information_schema`, `pg_temp_*`, `pg_toast*`, and a user schema containing `tmp`. |
| Preferences can reveal and rehide system schemas across multiple open PostgreSQL roots, and the choice survives restart; Restore defaults and cancellation behave as specified. | `MainWindow` Preferences dialog actions with a temporary settings store and real navigator widgets; reopen the app/window against that store. |
| Filter objects excludes hidden branches, includes them when enabled, and stays correct after rapid setting/search changes and late metadata responses. | `NavigatorController` filter field, model/tree, and public metadata request/reply seam. |
| SQL metadata suggestions follow the saved visibility choice and the editor's connection target, including after toggling with previously loaded catalog data. | `MainWindow` editor completion popup or completion controller driven through the public navigator/completion integration seam. |
| Non-PostgreSQL browsing and manual SQL access to PostgreSQL system objects still work. | SQLite/MySQL navigator fixture plus PostgreSQL driver metadata/inspection or query integration test where a test server is available. |
| A failed preference save preserves the active visibility and reports the error. | Preferences dialog with adapter storage failure seam; inspect tree and completion after failure. |

## Technical constraints and likely affected areas

- Place the visibility rule at a boundary shared by tree display, search, and completion, or ensure those consumers use the same predicate. Do not rely solely on hiding painted rows while search or completion still sees them. Do not remove PostgreSQL system metadata from APIs needed to inspect objects by ID. Preserve lazy fetching and bounded catalog behavior.
- Likely areas: `desktop/app/navigator_controller.*`, `desktop/models/navigator_model.*`, `desktop/app/main_window_navigator.cpp`, `desktop/widgets/preferences_dialog/*`, Preferences controller/storage/bridge contracts if persistence needs a new field, and corresponding desktop/storage tests. Inspect the current workspace before choosing the smallest compatible persistence representation. Existing settings records must load with the new default; avoid rejecting old strict JSON records solely because the new setting is absent.
- Reuse stock shared-styled Qt controls and design-system spacing/typography in Preferences. Preserve object names, accessibility names, keyboard navigation, focus, and existing signals. No new design-system component is required by this spec.
- Follow repository UI verification: `python3 scripts/ci/ui_consistency.py` and its `--json` census, `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present.

## Rollout, compatibility, and deferred choices

- Default off for new and existing installations; migrate or default older stored preference values safely. This setting is app-wide, not per saved profile. No database schema or user data migration is intended.
- The exact persistence field/key and whether visibility is enforced in `NavigatorModel`, a proxy, or a shared predicate are implementation choices. Keep the visible behavior and test seams above.
- PostgreSQL can expose version-specific internal namespace names. The implementation should explicitly cover the known families above and verify any additional family it treats as PostgreSQL-owned; do not broadly hide legitimate schemas based only on a `tmp` substring.

## Fresh-session instruction

Read this entire spec and inspect the current workspace, including existing changes and repository instructions. Then invoke `$implement` with `docs/specs/2026-09-28-hide-postgresql-system-schemas.md` and implement it using the public test seams above.
