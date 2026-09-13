# ChoscorDB prototype

Open [workspace.html](workspace.html) directly in a browser. No server, build, or network connection is required.

## Screens

| File | Purpose |
| --- | --- |
| [index.html](index.html) | Empty workspace with a New connection action |
| [workspace.html](workspace.html) | SQL editor, results, execution, and transactions |
| [connection.html](connection.html) | Direct entry to the new-connection dialog |
| [schema.html](schema.html) | Columns, indexes, keys, DDL, and data |
| [history.html](history.html) | Query history and reopening SQL |
| [changes.html](changes.html) | Post-MVP row-change review |

Connections are managed in the sidebar: **+** opens a connection dialog, and **…** edits/tests, duplicates, or deletes the selected profile. There is no separate Connections page or workflow-map page.

**Export** opens a modal from the workspace bottom bar or File menu. It supports CSV, JSON, JSON Lines, SQL INSERT, preview, download, progress, cancellation, and simulated failure. Closing the modal cancels an unfinished export.

**Preferences** opens a modal from the ChoscorDB menu or Cmd/Ctrl+, with Appearance, SQL editor, Results & execution, History & recovery, and Keyboard shortcuts tabs. Saving applies editor preferences without leaving the current screen.

## Files and layout

- `prototype.js`: screen renderers, sample state, application menus, shared handlers.
- `desktop-pages.js`: compact content panes and pinned bottom actions.
- `dialogs.js`: Export, Preferences, and sidebar connection management.
- `prototype.css`: shared light/dark desktop styling.

The full-height sidebar starts below the title bar. Object tabs sit above the working table. Content scrolls inside panes; primary actions remain at the bottom. macOS-style menus support keyboard navigation. Dialogs make background controls inert and preserve the current editor.

## Prototype boundaries

Database connections, queries, schema metadata, and transactions use illustrative sample state. Editing SQL does not execute it against a database. Exports download the eight sample rows, and SQL open/save uses browser files. Settings and non-secret profile metadata use browser local storage; passwords are not persisted. Row editing remains a post-MVP concept.

## Validation

Verified all six remaining screens and their relative links in Chromium. Checked modal export/download/cancellation, unchanged URL and editor text, preference tabs/save/reopen, immediate editor preference application, sidebar profile edit/duplicate/delete, and history preference entry. No browser runtime errors were observed. Visually reviewed both dialogs.

## Complete interaction flow

1. Open `index.html`. Select a saved connection to browse its first object, or use **New connection → Test → Save & connect** to open a new SQL document.
2. In the object explorer, switch between Columns, Indexes, Keys, DDL, and Data. **Open query** and **Generate SQL** open a document for that object without executing it.
3. Run or cancel the query. Create, switch, and close editor tabs; SQL buffers and the active tab persist when navigating between pages. Closing the last tab returns to the empty start page.
4. Completed sample executions appear in **View → Query history**. Select a record and use **Open in editor** to create another document without executing it.
5. **Export** and **Preferences** open dialogs over the current page. Export downloads sample rows; preferences apply to the editor. Both preserve the current screen and SQL buffers.
6. **Review changes** opens the post-MVP change review. Apply, commit/roll back, then return to the object or editor using the bottom actions.
7. **View/Window → Start page**, the macOS menus, and Quick switch provide routes between all remaining screens. Connection management remains in the sidebar.

`flows.js` owns document tabs, cross-screen state, executed-query history, object-to-editor handoff, and navigation initialization. All six HTML entry points load the shared scripts with relative paths; none requires a removed page.

Validation followed the complete flow above in Chromium, including creating a connection from the empty screen, selecting Analytics and opening its events object, generating/opening SQL without execution, retained drafts after visiting History, result export, preferences, change review/rollback, and returning to the object. All remaining page links resolve and no browser errors were observed. Database operations continue to use sample fixtures.
