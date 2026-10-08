# Schema & Objects: compact columns and table/view icons

Status: Ready for implementation  
Date: 2026-09-29  
Source: User request, two attached reference images, workspace inspection, and brainstorm decisions in this session.

## Outcome and current state

People browsing Schema & Objects can scan a table or view's columns and their database types without repeated file icons or index/key rows. Table and view rows and object tabs use the Lucide `grid-2x2` icon.

Today, expanded table/view nodes include columns, indexes, and keys. The sidebar renders column nodes with file icons, name only. Its `NavigatorIconDelegate` maps tables/views to `Icon::Table`; object tab creation, theme refresh, and workspace recovery also use `Icon::Table`. Metadata events already contain a column `database_type` through the Rust bridge, but the navigator conversion currently copies only general object properties. The tree has one model column and sits inside the Connections scroll area.

Relevant code includes `desktop/app/main_window_widgets.h`, `desktop/app/main_window_ui.cpp`, `desktop/app/navigator_controller.cpp`, `desktop/app/main_window.cpp`, `desktop/app/workspace_recovery.cpp`, `desktop/app/main_window_pins.cpp`, `desktop/models/navigator_model.*`, `desktop/design_system/icons.*`, and `desktop/resources/`. Inspect current code before deciding exact ownership.

## Requirements and decisions

1. Beneath an expanded **table or view**, display column rows only. Hide its index and key/constraint rows from the Schema & Objects tree. Do not remove those objects from database metadata or other features. Separate schema-level object groups and their rows remain available.
2. Render each visible column as a compact single row: column name left; its database-declared type right, in subordinate text, as in the second reference image. Omit the file icon and reclaim its space. Keep the type aligned at the right edge and visually subordinate in Light and Dark themes. Preserve stable row order. Long names/types must elide without overlap; expose their full values through an accessible description and tooltip or equivalent existing UI affordance. If the driver supplies no type, keep the row and leave the type area empty rather than inventing one.
3. Use the Lucide **grid-2x2** icon for table and view rows in Schema & Objects and for their object tabs. This includes newly opened tabs, restored tabs, and icon refresh after theme changes. Other object kinds keep their existing icon mapping. Add the icon to the existing licensed icon resource/catalog path, following its manifest conventions.
4. Preserve column selection, keyboard focus, search/filter, context menu, and Pin behavior. Selecting a column must still open its owning object on the Columns pane. Type text is presentation metadata, not part of the column name or object identity. Hidden index/key rows must not appear merely because the object filter matches them.
5. Preserve existing pins to now-hidden index/key objects. Activating one must verify its exact identity, reveal the owning table, and open the relevant Indexes or Keys details pane. Keep the pin removable. A genuine missing/mismatched object follows the existing Unavailable behavior; a loading or failed metadata request must not falsely mark it unavailable. Schema-level index/key pins retain their existing behavior.
6. Keep metadata loading lazy and bounded. A table/view with no columns should show its existing empty/loading/error states as applicable, without exposing hidden rows. Refresh, paging, and stale responses must not reintroduce hidden rows or display a type for the wrong column.

## Non-goals and alternatives rejected

- No change to database schema, DDL, or stored objects.
- No removal of Indexes or Keys panes in object details, quick object search results, SQL generation, or metadata APIs.
- No grid icon for indexes, keys, functions, sequences, or other kinds.
- No type normalization or inferred type when metadata omits one.
- No new sidebar preference, rollout switch, or storage migration.

## User-visible flows and edge cases

- Expand a table or view: see only its columns, with names and right-aligned types. Select a column to reach the Columns pane. A long type or narrow sidebar elides gracefully and remains available to assistive technology or a tooltip.
- Search/filter the tree for a column name: it remains discoverable. Search for a hidden table-level index/key: the tree does not show that row. Dedicated object search and object details retain their current functions.
- Open a table/view from the tree, quick search, a pin, row navigation, or workspace restore: its tab shows grid-2x2. Theme changes recolor the icon through the existing themed icon path.
- Activate a previously saved pin to a table-level index/key: load and verify the original metadata, select/reveal the parent table, and open its Indexes/Keys pane. A missing object retains the existing unavailable-pin treatment; a transient load failure stays retryable.

## Acceptance criteria and public test seams

| Observable outcome | Preferred public test seam |
| --- | --- |
| Expanded table/view rows show only columns; schema-level object groups remain visible; refresh/paging/filter cannot reintroduce hidden table-level index/key rows. | Drive `NavigatorController` with public metadata replies or a real SQLite fixture, then inspect `databaseNavigator`'s visible proxy rows and filter results. |
| Column rows show name left, subdued database type right, no file icon; missing type is blank; long content does not overlap; full values remain accessible in Light and Dark. | Use the real navigator widget with metadata fixtures and inspect its visible/accessibility data plus a focused visual check. If a shared design component changes, add its real Light/Dark gallery specimen and matching test. |
| Column selection, search, menu, and Pin behavior still work; type text does not alter identity. | Exercise `MainWindow`'s navigator selection, context menu, filter, and Pin UI against loaded column metadata. |
| Table and view rows/tabs use grid-2x2 across new tabs, restored tabs, and theme refresh; other kinds retain their icons. | Inspect the icon catalog/resource decoding and real `databaseNavigator`/`editorTabs` widgets after opening, restoring, and switching themes. |
| A saved pin to a hidden table-level index/key opens the verified parent table's matching details pane; a missing object is unavailable, while metadata failure is retryable. | Activate pins through `MainWindow`'s Pinned list using public adapter metadata success, absence, and failure events; inspect selected tree row, tab/pane, and visible pin status. |

## Technical constraints and likely affected areas

- Follow the root `CLAUDE.md` UI ownership rules. Reuse theme colors, typography roles, spacing tokens, and design-system presentation; do not add screen-owned QSS or visual literals. Preserve object names, accessibility names, focus, and signals.
- The Rust bridge's metadata DTO exposes `has_column` and `column.database_type`. The navigator conversion in `NavigatorController` currently drops these fields; carry only the data needed for row presentation through an appropriate model role or existing metadata property path. Keep name and identity roles unchanged.
- Filter hidden direct children in the navigator's visible tree path while preserving source metadata where needed for existing details, Pins, quick search, and identity verification. Do not rely on visual row absence as proof an object is missing.
- Check `desktop/design_system/README.md` and call sites before extracting a shared component. If design-system code changes, follow `desktop/design_system/CLAUDE.md`, including real Light and Dark specimens and their tests. Ensure any new UI construction pattern is counted by `python3 scripts/ci/ui_consistency.py --json`.
- Run `python3 scripts/ci/ui_consistency.py` and `--json`, `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present.

## Rollout, compatibility, and risks

This is a presentation and navigation change; no migration or feature flag is needed. Existing saved pins and workspace recovery records must continue to load. The main risk is that hidden rows still serve as identity or navigation targets elsewhere. Verify pin activation against exact metadata before routing to the parent table, and keep metadata failures distinct from confirmed absence.

The precise delegate layout, elision policy, and model role names are implementation choices. The visible behavior above is the contract. No material product decisions remain open.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and applicable `CLAUDE.md` files, then invoke `$implement` with `docs/specs/2026-09-29-schema-object-sidebar-columns.md` using the public test seams above.
