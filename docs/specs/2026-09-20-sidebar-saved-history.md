# Compact sidebar with Saved and History panels

Status: Confirmed for implementation  
Date: 2026-09-20  
Source: User screenshot and brainstorm decisions confirmed on 2026-09-20.

## Outcome and current state

Make the database sidebar denser and let users reach saved SQL files and recent query history without leaving the navigator. The three icon tabs at the top are Connections, Saved, and History. Each panel opens SQL in the central workspace without running it.

The current sidebar is composed in `desktop/app/main_window.cpp` with a saved connection list, object filter, and `QTreeView`. The connection row delegate has a 45 px size hint in `desktop/design_system/navigation_profile_row/navigation_profile_row.cpp`; tree row padding is in `desktop/design_system/tree/tree_item_style_sheet.qss`. SQL file Open/Save use native pickers and file paths. History is stored by the existing engine, has a separate History screen, and already reopens a record in an editor tab. The app uses `QStandardPaths::AppLocalDataLocation` for its data directory. The working tree has existing uncommitted UI changes; inspect and preserve them.

Related confirmed specs are `docs/specs/2026-09-19-selected-connection-sidebar.md` and `docs/specs/2026-09-19-unified-workspace-tabs.md`. This spec adds sidebar entry points. For clicks from the new Saved and History panels, its tab reuse rules take precedence over the unified-workspace spec's general new-tab rule for opening a file or history entry.

## Requirements and decisions

1. Update the shared sidebar components and styling first. Reduce the horizontal gap between tree disclosure controls and their icons/text, including connection and schema rows. Reduce tree item vertical padding and line height to roughly 28–32 px per row while keeping text readable, full-row pointer targets, keyboard focus, and theme states. Keep connection profile rows legible with both name and driver; avoid clipping at the normal sidebar width. Apply component styling through the design system, including its Light/Dark preview and checks required by `desktop/design_system/AGENTS.md`.
2. Place a horizontal strip of three icon buttons at the top of the sidebar. Each has a tooltip, accessible name, keyboard focus, and visible selected state. Connections opens the current connection and schema/object content, including its title and add action. Saved and History replace that panel within the same sidebar. Preserve the existing Connections behavior, selection, and tree state when switching panels. A panel switch does not change an SQL editor's connection target.
3. Saved lists `.sql` files directly in a per-user writable `sql/` subdirectory of `QStandardPaths::AppLocalDataLocation`; do not recurse into subfolders or include arbitrary external files. Create this directory when first saving there. Save for a document without a path defaults its picker to this directory; an explicit Save As may choose any path. Saving an already associated external file continues to save to that file. Files saved outside this directory do not appear in Saved. Refresh the list on entering Saved and after a successful save. Show a clear empty state and a readable directory/listing or file-open error.
4. Clicking a Saved item focuses an already open SQL tab for the same canonical file path, preserving any unsaved edits. Otherwise it opens the file in a SQL tab. File content is never executed automatically. A missing or unreadable file reports an error and does not leave a misleading new tab. The current file identity and dirty-close rules apply.
5. History shows the latest 50 recorded executions, newest first, with enough SQL preview, connection name, timestamp, and execution status to identify each. Provide a route to the existing full History screen for filtering, paging, retention, and clearing. Refresh when entering History and after a query completes while it is visible. History recording disabled means no new records, but existing records remain visible; clearing history removes them from both views. Show empty, loading, and error states.
6. Clicking a History item focuses its already open SQL tab by stable history record ID while that tab remains open, preserving any edits there. Otherwise it opens a new SQL tab with that record's SQL and associated profile, following existing missing/disconnected-profile handling. After that tab is closed, selecting the record opens a fresh tab. Opening a history item does not connect or execute. Do not identify records merely by matching SQL text.
7. Preserve existing guards against navigating away during an active operation, recovery/shutdown restrictions, history policy, and unsaved document handling. Switching sidebar panels and opening either kind of item must give an understandable blocked or failure outcome where these guards apply.

## User-visible flows and failures

- Connections remains the default sidebar panel. Switching to Saved or History changes only sidebar content; returning to Connections restores the highlighted profile and explorer state.
- A first Save of an untitled SQL document starts in the app data `sql/` folder. A saved file appears in Saved; a Save As elsewhere associates the tab with that external path and does not add it to Saved.
- If a listed file is removed or becomes unreadable, a click reports the failure and the list can refresh. If a file is already open and dirty, its tab is focused without reading over its edits.
- History entries retain their connection identity even if that profile is now missing or disconnected. The opened SQL remains inert and shows the existing unavailable target state. When recording is off, earlier entries remain until retention or clear removes them.
- A history entry removed by retention or Clear disappears on refresh. An already opened SQL tab remains an editable document until the user closes it.

## Acceptance criteria and public test seams

| Observable outcome | Preferred test seam |
| --- | --- |
| Sidebar tree rows are compact, disclosure/text gaps are smaller, and profile rows remain legible in Light/Dark at normal and narrow widths. | Real component specimens in the design-system preview, QSS lint, and representative Qt visual/accessibility checks. |
| Three icon tabs switch panels by pointer and keyboard, expose accessible names/selected state, and preserve Connections selection and SQL target. | `MainWindow` Qt integration test through public sidebar widgets and active editor target. |
| First Save defaults to the per-user app data `sql/` folder; direct `.sql` files appear, external and nested files do not; entering Saved refreshes it. | Temporary `QStandardPaths` test location, public Save action/file picker seam, and visible Saved list. |
| Clicking a saved file opens it once, then focuses the same tab by canonical path without overwriting edits; missing/unreadable files show an error. | Main-window file list and editor tabs using temporary real files. |
| History lists at most the latest 50 records with connection/time/status; policy disable, clear, new completion, and full-History navigation stay consistent. | Existing engine/history API through MainWindow with temporary history storage, inspecting visible rows and navigation. |
| Repeated click of one history record focuses its edited open tab; clicking after close creates a new inert tab; identical SQL from distinct record IDs stays distinct. | Main-window History list, editor tabs, and adapter execution/connection event observations. |
| Opening either kind of item never executes or connects, and operation/recovery guards remain effective. | Public UI actions with controlled adapter events and SQL editor/recovery state. |

## Constraints and likely affected areas

Use the existing native Qt design components, icon set, file association, editor tabs, history API, and workspace guards. Likely areas are `desktop/app/main_window.cpp`, sidebar/design-system components, `desktop/widgets/history_dock`, and desktop integration tests. The Saved directory is user data, not the installed binary directory; do not write into the application bundle. Keep file operations bounded and responsive for an unusually large directory, and avoid loading all history records just to show 50. Preserve cross-platform path handling and canonical identity, including a sensible fallback for a temporarily missing path.

No migration is required for existing profiles, arbitrary SQL file associations, history records, or recovery buffers. Existing externally saved files remain openable through the current Open action, but are outside the Saved panel. There is no automatic SQL execution or connection on sidebar selection.

## Risks and deferred implementation choices

Exact icon glyphs, file row details, and the internal panel widget structure are implementation choices. The visual target is the supplied sidebar screenshot with tighter disclosure spacing and denser object rows, without hard-coding one display scale. The existing History screen may have different refresh timing; both views should show the same retained records after refresh. If another confirmed spec changes central tab composition first, adapt the entry points to that public tab interface while keeping these identity and inert-open rules.

## Fresh-session instruction

Read this entire spec and inspect the current workspace, including existing uncommitted changes and related confirmed specs. Then invoke `$implement` with `docs/specs/2026-09-20-sidebar-saved-history.md` and implement it using the stated public test seams.
