# History and recovery

ChoscorDB keeps a history of the queries that you run.
It also keeps the text of your SQL editors for recovery after ChoscorDB stops unexpectedly.
This page also tells you what ChoscorDB does when you close it.

## Query history

To open the history, choose `View` > `Query history`.
The history shows one row for each query that you ran:

| Column | Contents |
| --- | --- |
| `Timestamp` | The time when the query ran. |
| `Connection profile` | The connection profile of the query, or `Unsaved connection`. |
| `SQL excerpt` | The start of the SQL text. |
| `Duration` | The run time of the query. |
| `Status` | `Completed`, `Failed`, `Cancelled` or `Disconnected`. |
| `Rows` | The row count. A dash shows that the row count is not known. |

The history shows its rows in pages.
Use `Previous` and `Next` to move between the pages.
To filter the current page, type text in `Filter this page`.
To show only one status, select it in the status list.

### Open a query from the history

1. Select a row in the history.
2. Read the full SQL text in the preview.
3. Click `Open in new query`.

ChoscorDB opens the full SQL text in a new SQL editor.
If a connection of the same connection profile is open, ChoscorDB selects it.
ChoscorDB does not open a new connection and does not run the SQL.

If the SQL text is long, the preview shows one part of it.
Click `Earlier text` and `Later text` to read the other parts.

The `Recent history` tab of the side panel also shows recent queries.
Quick search finds SQL text in the history too.
For more information, see [Quick search](navigator.md#quick-search).

### Manage the history

The `Manage history` menu has these actions:

- `Record history` turns the history on or off.
  When you turn it off, ChoscorDB keeps the existing entries.
- `Refresh` loads the history again.
- `View full query` shows the full SQL text of the selected row.

To delete all entries, click `Clear history…` and confirm.

ChoscorDB keeps history entries for 90 days by default.
It keeps a maximum of 10,000 entries by default.
To change these limits, use the `History & recovery` page of the preferences.
For more information, see [Preferences](sql-editor.md#preferences).

## Recovery

ChoscorDB saves the state of your workspace while you work.
If ChoscorDB stops unexpectedly, it restores the workspace the next time that it starts.

Recovery restores these data for each SQL editor:

- The SQL text, also text that you did not save to a file.
- The tab title and the tab order.
- The file path and the unsaved state.
- The cursor position and the selection.
- The connection profile of the SQL editor.

Recovery also restores object tabs.

All restored tabs stay disconnected.
Recovery does not open connections and does not run SQL.
It does not read the SQL files again.
To run a restored query, select a connection and run the query.

Recovery is not the same as `Save SQL file…`.
To keep a query in a file, save it.

### Recovery errors

If ChoscorDB cannot save or restore the workspace, it shows an error message.
The stored workspace stays unchanged until you choose an action.
Use the `File` > `Workspace recovery` menu:

- `Retry workspace recovery` tries again.
- `Start new workspace` starts with an empty workspace.
- `Close without recovery` closes ChoscorDB without a saved workspace.
- `Keep workspace open` stops the close.

## Close ChoscorDB

When you close the window, ChoscorDB does these steps:

1. If table data has grid changes that you did not apply, ChoscorDB asks you to apply or discard them.
2. If a transaction is open or a query is active, ChoscorDB asks you to confirm.
3. ChoscorDB saves the workspace for recovery.
4. ChoscorDB closes all connections.
5. ChoscorDB writes the last history entries.

In the confirmation dialog, `Roll back and close` rolls back the open transactions.
`Cancel work and close` cancels the active work.
The default button is `Cancel`.

If ChoscorDB cannot write the history, it shows the `History could not be flushed` dialog.
Click `Retry` to try again.
Click `Close without history` to close without the history entries that are not written.
Click `Cancel` to keep ChoscorDB open.

If ChoscorDB cannot save the workspace for recovery, use the `File` > `Workspace recovery` menu.
