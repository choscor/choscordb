# Unified workspace tabs

Status: Confirmed for implementation  
Date: 2026-09-19  
Source: User's main-panel screenshot and the 2026-09-19 brainstorm conversation.

## Outcome and current state

Make a top-level tab the parent view for each SQL document or inspectable database object. Users can switch among SQL scripts and object details in one workspace while seeing the actions and status relevant to the active tab.

Currently `desktop/app/main_window.cpp` puts SQL editor tabs inside a separate SQL screen and uses one Object screen. `desktop/app/object_explorer.cpp` reuses one object-inspection widget with inner Columns, Indexes, Keys, DDL, and Data panes and a footer containing both actions and status. `desktop/app/query_workspace.cpp` owns one latest SQL result shared across SQL documents. `desktop/app/workspace_recovery.cpp` and the saved workspace transport currently persist SQL documents only. The established behavior in `docs/specs/2026-09-13-mvp-ui-flows.md` remains applicable except where this spec changes screen and tab composition.

## Requirements and decisions

1. Put one top-level, closable, reorderable tab bar in the central workspace. It contains SQL document tabs and tabs for all inspectable object types supported by the navigator. A table/view tab shows its existing applicable Columns, Indexes, Keys, DDL, and Data inner panes; other objects show only applicable inspection panes. A SQL tab shows its editor and SQL result area. The top-level tab owns the visible content, actions, and status. Do not nest the existing SQL document tab bar inside a SQL screen.
2. Clicking an inspectable object in the navigator opens its tab, or focuses the existing tab for the same object identity. Clicking a child property focuses the parent object tab and relevant inner pane. Preserve the selected inner pane while switching tabs. Identify objects by stable profile/connection context, object type, and qualified identity so names from different connections or schemas do not collide. A new SQL command, opened file, history query, or generated SQL opens a new SQL document tab; generation and opening never execute SQL.
3. Keep Start as the empty state when no workspace tab is open. Keep History as a separate navigable screen. Reopening a History entry creates a SQL tab. Closing the active tab focuses the nearest remaining tab; closing the last shows Start. Preserve the current dirty SQL close confirmation and recovery safeguards. A tab close, selection, sidebar action, keyboard route, and History route follow the same navigation rules.
4. Show a contextual header below the top-level tabs. For SQL, it contains the connection target and Run, Cancel, and applicable query/session actions. For objects, it contains Refresh, Open query, Generate SQL, and applicable Data actions such as Cancel and Export. Explain unavailable actions. Keep result-specific Results/Messages selection and paging next to the SQL result. The bottom footer displays status, result summary, or messages, without the primary action buttons. The active tab determines header and footer content. Preserve accessible names, keyboard focus, tab overflow, and narrow-window usability.
5. Preserve one latest SQL result/message lifecycle shared among SQL tabs, with clear originating document and connection. Object Data remains a separate bounded, read-only result. Keep existing paging, export, large-value, transaction, cancellation, and operation-busy semantics. During active SQL execution or object Data work, block tab or screen changes that would hide the active Cancel control; explain the block. Resume navigation after terminal completion. Do not silently retarget a SQL document when the sidebar selection changes.
6. Retain lazy object inspection. Opening the relevant pane or explicit Refresh fetches data under current request-generation and bounded-read rules; merely refocusing a tab does not trigger a new read. A disconnected or missing object/connection shows an explicit unavailable state and a manual reconnect path. Do not display restored metadata as current, reconnect automatically, execute SQL, or silently drop a restored tab. Stale replies must not overwrite another tab or a newer request.
7. Workspace recovery preserves mixed tab order, active tab, SQL drafts and file associations, and object identity plus selected inner pane. Restore object tabs as inert references, then load their content when selected and connected. Extend the existing bounded, validated recovery format and failure handling; maintain compatibility with previously saved SQL-only workspaces. Recovery must preserve unsaved SQL content and must not persist unbounded Data pages or live handles.

## User-visible flows and failure behavior

- Selecting an object from the sidebar opens or focuses its top-level tab; its inner pane displays loading, real content, empty, unsupported, or retryable error state as appropriate. Switching away and back preserves the selected pane without an automatic refresh.
- New query, Open query, Generate SQL, opening a SQL file, and reopening History each show a SQL tab. The active header changes to query controls and the result area identifies the origin of its shared result.
- Closing a tab updates focus and the visible header/footer. A dirty SQL tab asks before discarding; cancellation keeps the tab. The final close shows Start.
- After restart, mixed tabs appear in prior order with the prior active tab. A restored object with no usable connection remains visible and offers manual reconnection. Once connected, selecting its pane loads fresh content.
- During running/cancelling work, blocked tab changes show the reason and keep Cancel reachable. Failed loads and disconnects show status on the affected tab; Retry or Refresh is available where supported.

## Acceptance criteria and public test seams

| Observable outcome | Test seam |
| --- | --- |
| Navigator selection creates or focuses one correctly identified object tab, including same-named objects in different schemas/connections and child-property navigation. | Drive the public main-window navigator in Qt integration tests; inspect top-level tab labels/count, selected tab, and visible inner pane. |
| New query, file open, generated SQL, and History reopen create SQL tabs without executing; SQL edits and targets remain independent. | Trigger public menu/sidebar/history actions in Qt integration tests and observe tabs, editor text/target, and adapter execution events. |
| Active tab determines content, contextual header, and footer; object details and SQL result controls remain usable by pointer and keyboard at normal and narrow widths. | Main-window widget/accessibility integration tests and a visual capture of both tab types at representative widths. |
| Close and switch behavior follows dirty-confirmation and active-operation guards, with Start after the last tab. | Exercise tab bar/menu/shortcut actions through Qt integration tests with modified editors and controlled adapter events. |
| Shared SQL result remains attributed to its originating document; object Data does not replace it and stays bounded. | Execute through the workspace/adapter public signals and inspect visible result attribution, object Data, and paging. |
| Mixed order, active tab, SQL draft, object identity, and inner-pane choice survive restart; old SQL-only recovery data loads. Missing connections remain explicit and inert. | Save and restore via the public workspace recovery/bridge boundary, then inspect tabs and adapter events for unexpected reads/connects/execution. |
| Lazy refresh, stale-event rejection, error/retry, disconnect, and cancellation remain correct per tab. | Use delayed/failing adapter responses through public object-inspection and Data interfaces; observe the selected tab and status. |

## Scope and constraints

Likely affected areas include `desktop/app/main_window.cpp`, `desktop/app/object_explorer.cpp`, `desktop/app/object_data_workspace.cpp`, `desktop/app/query_workspace.cpp`, `desktop/app/workspace_recovery.cpp`, the bridge/recovery data model and persistence format, design-system tab styling, and desktop integration tests. Inspect current interfaces before choosing the widget composition or persistence schema. Reuse existing typed metadata, dialect quoting, request tokens, query guards, design components, and keyboard bindings. Do not add per-SQL-tab result storage, concurrent execution, automatic object reads on every tab focus, editable object Data, or automatic reconnect.

The migration must accept existing SQL-only recovery records. New mixed-tab records need validation, bounds, and safe fallback/error handling consistent with existing recovery. No separate rollout is required beyond the normal desktop release. Existing visible status and error surfaces should continue to report loading, cancellation, disconnect, restore failure, and retry outcomes.

## Risks and deferred implementation choices

The current object explorer is a single mutable widget; tab isolation and delayed callbacks need careful ownership or request scoping. The shared SQL result can be visually misleading when another SQL tab is active, so origin labeling is mandatory. Restored object references may be unavailable until the user reconnects. Exact widget factoring and serialized record shape are implementation choices, provided the behavior and test seams above hold.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and relevant tests, then invoke `$implement` with `docs/specs/2026-09-19-unified-workspace-tabs.md`.
