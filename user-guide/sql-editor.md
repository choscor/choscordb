# SQL editor

You write and run queries in the SQL editor.
Each SQL editor is a tab in the workspace.
This page also tells you about completion, find and replace, query settings and preferences.

## Open and save SQL files

Use the `File` menu:

- `New query` opens an empty SQL editor.
- `Open SQL file…` opens a `.sql` file in a new SQL editor.
- `Save SQL file…` saves the text of the current SQL editor.
- `Save SQL file as…` saves the text to a new file.
- `Close tab` closes the current tab.
  If the tab has unsaved changes, ChoscorDB asks before it discards them.

ChoscorDB suggests the `sql` folder in its application data directory for new files.
The `Saved queries` tab of the side panel shows the SQL files in that folder.
Click a file in the list to open it.
For the location of the application data directory, see [Your data](install.md#your-data).

## Run a query

Each SQL editor tab uses one connection.
A new tab gets the connection of the current tab or of the selected database.
When no result shows, the status line shows the connection of the current tab.

To change the connection of a tab, right-click the tab and choose `Change connection`.
Then choose a connection.
You cannot change the connection while a query runs.

1. Put the cursor in a statement, or select the SQL text that you want to run.
2. Do one of these steps:
   - To run the statement at the cursor, click `Run` in the toolbar.
     Or choose `Query` > `Run current`, or press `Ctrl+Return` (`Command+Return` on macOS).
   - To run all statements in the SQL editor, click the arrow next to `Run` and choose `Run all`.
     Or choose `Query` > `Run all`, or press `Ctrl+Shift+Return` (`Command+Shift+Return` on macOS).

If you select text, ChoscorDB runs only the selection.

MySQL and MariaDB connections run more than one statement as a script.
ChoscorDB runs the statements one after the other.
Execution stops at the first error.
To see the result of the next statement, go to the last page of the current result and click `Next result`.

PostgreSQL and SQLite connections run only one statement at a time.
If the text has more than one statement, the database shows an error and runs no statement.
The rows show in the result grid below the SQL editor.
The status line below the result grid shows the state, the duration and the row count.
If the query fails, the status line shows the error.

To stop a query, choose `Query` > `Cancel`.

ChoscorDB asks you to confirm some statements before they run.
Examples are `DROP`, `TRUNCATE`, and `DELETE` or `UPDATE` without a `WHERE` clause.
ChoscorDB also asks for SQL that it cannot read.
The default button of this dialog is `Cancel`.

## Transactions

Each statement commits when it completes.

To keep changes in a transaction, run `BEGIN` before the statements.
Then run `COMMIT` to keep the changes, or `ROLLBACK` to discard them.

If you disconnect or close ChoscorDB, an open transaction rolls back.
ChoscorDB asks you to confirm this first.
You cannot apply grid changes while a transaction is open.

## Completion

The SQL editor suggests SQL keywords and the names of database objects.
The list opens after you type two characters of a name.
To open the list at any time, choose `Edit` > `Complete SQL`.
The shortcut is `Ctrl+Space`, and `Control+Space` on macOS.

1. Type the start of a keyword or a name.
2. Use the arrow keys to select a suggestion.
3. Press `Return` to insert the suggestion.

Press `Escape` to close the list.
An inserted suggestion is one step for `Undo`.

SQL keywords are available also without a connection.
Names of schemas, tables, views and columns come from the navigator of the selected connection.
Completion shows only the names that the navigator loaded.
To get more names, expand more nodes in the navigator.
Completion does not send requests to the database and does not run SQL.

The list shows a maximum of 100 suggestions.
Completion keeps a maximum of 10,000 names or 8 MiB of name text for each connection.
If a connection has more names, ChoscorDB tells you that the suggestions are not complete.

## Find and replace

1. Choose `Edit` > `Find…` to open the search panel.
2. Type the text in `Find text`.
3. Click `Next` or `Previous` to go to a match.

To replace text:

1. Choose `Edit` > `Replace…`.
2. Type the new text in `Replace with`.
3. Click `Replace` to replace the current match.
4. Click `Replace all` to replace all matches.

Search uses the text literally.
By default, search ignores case, finds text inside words and continues from the other end of the document.
Select `Match case` or `Whole word` to change the search.
`Replace all` is one step for `Undo`.
Press `Escape` or click `Close` to close the panel.

The search panel operates on the current SQL editor only.
It can search a document of a maximum of 16 MiB.
The search text can have a maximum of 16 KiB.

## Query settings

Query settings apply to new queries.
To change them, choose `Query` > `Query settings…`.

| Field | Range | Default |
| --- | --- | --- |
| `Rows per page` | 100 to 10,000 | 1,000 |
| `Statement timeout (seconds)` | 0 to 86,400. 0 shows as `No timeout`. | `No timeout` |

Click `Apply` to save the settings.
Existing results keep their page size and timeout.
A change does not run SQL again.
The same settings are also on the `Results & execution` page of the preferences.

## Preferences

To open the preferences, choose `Edit` > `Preferences…`.
On macOS, this item is in the `ChoscorDB` application menu.
Click `Save preferences` to save your changes.
Click `Restore defaults` to put the default values in the form.
Then click `Save preferences` to keep them.

| Page | Field | Range | Default |
| --- | --- | --- | --- |
| `Appearance` | `Theme` | `System`, `Light`, `Dark` | `System` |
| `SQL editor` | `Use system monospace font` | On or off | On |
| `SQL editor` | `Font family` | Fonts on your computer | System monospace font |
| `SQL editor` | `Font size` | 8 to 48 | 13 |
| `Results & execution` | `Rows per page` | 100 to 10,000 | 1,000 |
| `Results & execution` | `Statement timeout (seconds)` | 0 to 86,400 | `No timeout` |
| `Connections` | `Connection timeout (seconds)` | 1 to 300 | 20 |
| `Connections` | `Show system schemas` | On or off | Off |
| `History & recovery` | `Record query history` | On or off | On |
| `History & recovery` | `Keep history (days)` | 1 or more | 90 |
| `History & recovery` | `Maximum history entries` | 1 or more | 10,000 |

`Connection timeout (seconds)` applies to new SSH tunnels and to database logins through them.
`Show system schemas` shows PostgreSQL catalog and temporary schemas in the navigator and in completion.
The `Appearance` page also has `Reset appearance and layout`.

A font change applies to all SQL editors.
It does not change the text or the undo steps.

### Keyboard shortcuts

The `Keyboard shortcuts` page shows the commands and their shortcuts.

1. Click the field of a command.
2. Press the new key combination.
3. Click `Save preferences`.

To remove the shortcut of a command, clear its field.
The command stays available in its menu.
The `Result grid` group shows the shortcuts for the edit commands of the result grid.
These shortcuts work only when the result grid has the keyboard focus.
Thus, a grid command and a window command can use the same shortcut.
If two commands in the same group use the same shortcut, ChoscorDB shows an error and does not save.
A grid shortcut must be one key combination. A sequence of two or more key combinations is an error.
You cannot change the grid shortcuts for `Copy`, `Open referenced row` and the `Backspace` key of `Set NULL`.
`Restore defaults` sets the platform default shortcuts again.
