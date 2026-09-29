# ERD navigation and dense layout

Status: Confirmed for implementation

Date: 2026-09-29
Source: User's ERD screenshot and brainstorm decisions on 2026-09-29. This refines `docs/specs/2026-09-27-object-er-diagram.md` for the existing feature.

## Outcome and current state

Help a database user inspect a table's direct foreign-key neighborhood with trackpad gestures and a readable drawing even when many tables or columns are present. Remove the visible **Zoom in**, **Zoom out**, and **Fit diagram** buttons and their control row.

`desktop/widgets/object_erd_widget.cpp` owns the `QGraphicsView` scene, the three buttons, wheel handling, keyboard shortcuts, and layout. Today any vertical wheel event changes zoom by a fixed step, including a two-finger scroll on a trackpad. Initial load calls `fitGraph()`, which can shrink a dense graph until text is unreadable. Neighboring tables are stacked in a single vertical column on each side of the selected table (or one column below it at narrow widths), making dense diagrams tall. The widget already supports pointer panning, `+`/`-`/`0` zoom and fit, arrow-key table selection, Enter activation, and double-click activation. Tests are primarily in `tests/desktop/object_explorer_test.cpp`; some ERD navigation coverage is in `tests/desktop/navigator_object_workspace_test.cpp`.

The graph remains the selected table plus direct incoming and outgoing FK neighbors. Its structured data and bounded loading come from the existing adapters. Current driver bounds include 64 tables in SQLite/MySQL, 128 edges in PostgreSQL, and aggregate column/text limits. Existing incomplete-graph status must continue to identify limits or missing metadata; this work must not silently drop more items to improve the drawing.

## Decisions and requirements

1. Remove exactly the three screenshot controls and their row. Do not leave an empty toolbar or padding band above the diagram. Keep the ERD pane and its existing status, refresh, and retry behavior in `ObjectExplorer`.
2. Pinching on a trackpad zooms the ERD smoothly, anchored near the gesture position. Two-finger vertical and horizontal scrolling pans the viewport in their respective directions. Avoid zooming on ordinary trackpad scrolling. Preserve pointer drag panning, keyboard `+`/`-` zoom, and `0` fit, with accessible instructions that describe the actual controls. Mouse-wheel behavior may follow normal Qt scrolling; Ctrl/Command-wheel zoom is an optional implementation detail, not a required gesture.
3. Keep every available column, type, PK/FK marker, table identity, and relationship endpoint in the diagram. Do not replace neighbor boxes with summaries or names-only boxes because a graph is dense. Existing metadata limits and incomplete status still apply.
4. Improve automatic placement for dense one-hop graphs. Boxes must not overlap; the selected table must be visually distinct; incoming and outgoing relationships must be traceable to their column endpoints; self, parallel, and composite FKs must remain distinct. Prefer stable placement across repeated renders of the same graph and avoid routing lines through unrelated boxes where practical. The implementation may choose a layout algorithm and use multiple rows or columns; no manual layout editor is required.
5. On first load or refresh, a small graph may fit fully. When fitting the entire graph would make table text hard to read, open at a readable scale centered on the selected table and nearby relationships. Off-screen items remain reachable by panning and scrolling. `0` fits the whole graph on demand, even if that overview makes text small. Zoom must remain bounded so a user cannot lose the graph through an extreme gesture.
6. Keep table activation and the existing object-tab navigation route. Preserve the graph's read-only behavior, light/dark theme, keyboard focus and access, loading/error/incomplete status, lazy loading, refresh, stale-reply protection, and workspace recovery behavior from the existing ERD specification.

The user chose full column detail over collapsing or hiding it. The user chose a readable initial focus over always fitting the whole dense graph. The user chose pinch zoom plus two-finger pan over treating two-finger scrolling as zoom.

## User-visible flow and edge cases

- Open a table's ERD. The viewport begins immediately below the tab bar without the three controls. A modest graph is visible in full; a dense graph opens with the selected table readable. Pan to reach other boxes; pinch around a location of interest to zoom; press `0` for an overview.
- Scroll horizontally or vertically with two fingers to pan without changing zoom. Pinch in or out to change zoom without unexpectedly moving the selected point away from the gesture. Keyboard zoom/fit remains available when the view has focus.
- At a zoom limit, further pinching in the same direction leaves the scale at the limit. Empty, loading, failed, disconnected, and incomplete graph states continue to use the existing status surfaces. Refreshing replaces the scene and reapplies the initial-view policy.
- Dense graphs preserve all data delivered by the adapter. If the adapter reports truncated or unavailable metadata, retain the existing incomplete notice; rendering cannot pretend the graph is complete.

## Acceptance criteria and public test seams

| Observable outcome | Public test seam |
| --- | --- |
| ERD has no Zoom in, Zoom out, or Fit diagram controls or empty control row. Other pane controls still work. | Open an `ObjectExplorer` table tab; inspect visible child controls, `objectErdView` geometry, and neighboring pane actions. Update tests that currently click `objectErdZoomIn`/`objectErdFit`. |
| Pinch in/out changes scale around the gesture position, within bounds; two-finger vertical/horizontal scroll pans without changing scale. | Send platform-appropriate native gesture and pixel/angle wheel events through the `objectErdView` viewport in a Qt widget test; observe `zoomFactor()` and `mapToScene()` for a fixed viewport point. Manually verify a macOS trackpad because synthetic Qt gestures cannot fully prove hardware delivery. |
| Keyboard `+`, `-`, and `0`, pointer drag pan, arrow selection, Enter and double-click activation still work. | Use Qt input events on the public widget/view; observe zoom, scene position, `tableActivated`, and the resulting object tab through `ObjectExplorer`. |
| With a dense one-hop fixture, all delivered tables/columns/markers and FK endpoints remain present, table boxes do not overlap, and layout is deterministic across repeat render and resize. | Supply a synthetic `ObjectGraph` through `ObjectErdWidget::setGraph`; inspect public `QGraphicsScene` items and geometry, including self, composite, and parallel edges. Capture Light/Dark normal and narrow views for visual review of line legibility. |
| A dense graph starts with readable selected-table text and accessible nearby relationships; `0` fits the full scene; all boxes can be reached by panning. Small graphs still fit on load. | Resize/show `ObjectErdWidget` with small and dense fixtures; observe view transform, selected box and text geometry in viewport coordinates, `sceneRect`, scrollbar ranges, and the result of `0`. |
| Metadata limits and existing ERD status/navigation/recovery behavior remain correct. | Existing adapter and `ObjectExplorer` integration tests with incomplete and stale responses, refresh, activation, and restored tabs. |
| UI presentation follows repository policy. | Run `python3 scripts/ci/ui_consistency.py` (inspect `--json`), `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, relevant native CTest targets, and the full native suite when dependencies are present. |

## Technical constraints and likely affected areas

Primary work is in `desktop/widgets/object_erd_widget.h/.cpp` and `tests/desktop/object_explorer_test.cpp`. `desktop/app/object_explorer.cpp` should only change if required to preserve status or initial-view behavior. Reuse design-system theme colors, typography, and spacing; screen code must not introduce local QSS or literal visual values. Consult `desktop/design_system/README.md` before extracting a component. If editing `desktop/design_system/`, follow its `AGENTS.md`, including Light/Dark gallery specimen and matching test. Preserve `objectErd` and `objectErdView` object names, focus, accessibility, and signals. Removed button names may disappear; update tests accordingly. Check the UI consistency census for any new component or construction pattern.

No graph schema, database migration, workspace format change, telemetry, or special rollout is intended. The rendering must stay responsive within the current adapters' bounded payloads. Do not weaken incomplete reporting to meet layout goals.

## Risks and assumptions

- Qt may deliver trackpad pinch as a native gesture, a modifier-bearing wheel event, or a platform-specific sequence. Implementation should inspect the actual Qt event path on supported desktop platforms and accept the relevant forms without mistaking ordinary two-finger scroll for zoom. Hardware verification on macOS is required in addition to synthetic tests.
- “Readable” is judged by using the design-system's normal text sizes at the initial view rather than shrinking the entire dense scene. Exact threshold and layout algorithm are implementation choices; tests should assert observable scale and visibility rather than an internal algorithm.
- The previous ERD spec's one-hop scope, full available column detail, statuses, and navigation remain in force. This spec supersedes its initial-fit and pointer-zoom behavior where they conflict.

## Fresh-session instruction

Read this whole spec and `docs/specs/2026-09-27-object-er-diagram.md`, inspect the current workspace, then invoke `$implement` with `docs/specs/2026-09-29-erd-navigation-dense-layout.md`.
