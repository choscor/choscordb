# Navigator

The navigator is the tree of connection profiles and database objects at the side of the window.
This page tells you how to browse database objects, generate SQL, find objects and disconnect.

## Side panel

The side panel has three tabs:

- `Connections` shows the connection profiles, the pinned database objects and the navigator.
- `Saved queries` shows the SQL files in the saved SQL folder.
- `Recent history` shows the queries that you ran recently.

The navigator is in the `Schema & objects` section of the `Connections` tab.
Click a connection profile in the `Connections` section to show its database objects.
For more information, see [Connections](connections.md).

## Browse database objects

1. Expand a connection in the navigator.
2. Expand a schema or a database.
3. Expand a table or a view to see its columns and other child objects.

ChoscorDB loads the child objects of a node only when you expand the node.
ChoscorDB loads a maximum of 1,000 objects at a time.
If a node has more objects, right-click the node and choose `Load more objects`.

To filter the tree, type text in `Filter objects…`.
To load the objects of a node again, right-click the node and choose `Refresh`.

### Row status

A node shows its status as an icon after its name:

- A loader icon shows that ChoscorDB loads the node or its child objects.
- An error icon and a red row show that the load failed.
- A warning icon shows that the object is not available.

Hold the pointer on the node to read the message.
If a load that you start fails, ChoscorDB also shows the error in the error dialog.
You start a load when you expand a node, choose `Refresh` or choose `Load more objects`.
A failed node stays red until a retry succeeds.
To retry, right-click the node and choose `Refresh`.
You can also collapse the node and expand it again.
Pinned objects use the same icons and the same `Refresh` action.

For a saved connection profile, ChoscorDB keeps the names and kinds of the child objects on your computer.
When you expand a node again, ChoscorDB shows the saved child objects immediately.
The loader icon stays on the node until ChoscorDB loads the current child objects.
The current child objects then replace the saved child objects.
If the load fails, the saved child objects stay, and the node shows the error icon.
`Refresh` always loads the current child objects from the database.
ChoscorDB does not use saved child objects that are older than 7 days.
ChoscorDB deletes the saved child objects when you delete the connection profile.
It also deletes them when you change the server or the database of the connection profile.

When ChoscorDB searches objects or loads the saved queries or the recent history, a loader icon shows next to the section title.

### Errors and notices

If an action that you start fails, ChoscorDB shows one error dialog.
If more errors occur while the dialog is open, the dialog shows the number of errors and the list.
Errors from background work and warnings show in one notice at the bottom of the window.
A new notice replaces the old text and adds to the count in its title.
Close the notice when you have read it.
If another dialog is open, the error dialog shows after you close that dialog.

When you rename or drop an object, the `Schema & objects` header shows a busy icon.
The success message shows after the navigator shows the change.

## Object tabs

Click a table or a view in the navigator to open it in an object tab.
An object tab has these panes:

- `Columns` shows the columns.
- `Indexes` shows the indexes.
- `Keys` shows the keys.
- `DDL` shows the SQL that defines the object.
- `ERD` shows the tables that the object connects to through foreign keys.
- `Data` shows the rows of the object.

Click `Refresh object` to load the metadata again.
Click `Open query` to open a new SQL editor for the object.
Click `Generate SQL` to make SQL for the object.
ChoscorDB does not run this SQL.

### Table data

The `Data` pane shows the rows of a table or a view in the result grid.
Use `Previous page` and `Next page` to move between the result pages.

To filter the rows:

1. Type a condition in the filter field above the rows, for example `id > 10 AND name LIKE 'A%'`.
2. Click `Apply filters`.

Do not type the word `WHERE`.
ChoscorDB applies the condition with SQLite rules to the rows that it loaded.
Click `Clear filters` to show all rows again.

To make a filter from a cell, right-click the cell and choose `Quick Filter`.
Then select an operator, for example `=`, `<`, `LIKE` or `IS NULL`.

To sort the rows, click a column header.
Click the header again to sort in the opposite direction.
Click a third time to remove the sort.

The `Data` pane has its own actions: `Export…`, `Add row`, `Delete selected`, `Restore selected`, `Set NULL` and `Apply…`.
For more information, see [Edit rows](results.md#edit-rows) and [Export rows](results.md#export-rows).

## Context menu of a database object

Right-click a database object in the navigator to see its actions:

- `Pin` or `Unpin` adds the object to the `Pinned` section or removes it.
  Save the connection profile before you pin its objects.
- `Refresh` loads the object again.
- `Copy qualified name` copies the full quoted name of the object.
- `Show DDL` opens the `DDL` pane of the object.
- `Drop` deletes the object from the database.
- `Rename` gives the object a new name in the same schema or database.
- `Generate SQL` makes a SQL statement for a table or a view.

`Drop` and `Rename` show the SQL that ChoscorDB will run.
`Drop` asks for confirmation, and the default button is `Cancel`.
If an action is not available for an object, the menu shows the reason.

## Generate SQL

1. Right-click a table or a view in the navigator.
2. Choose `Generate SQL`.
3. Choose `SELECT`, `INSERT`, `UPDATE` or `DELETE`.

ChoscorDB opens the statement in a new SQL editor and selects the connection of the object.
ChoscorDB does not run the statement.
Review the statement before you run it.

- `SELECT` selects all columns of the object.
- `INSERT` lists the columns with numbered placeholders.
- `UPDATE` lists the columns with placeholders and an incomplete `WHERE` clause.
- `DELETE` has an incomplete `WHERE` clause.

Replace the placeholders with values before you run the statement.
Complete the `WHERE` clause before you run an `UPDATE` or a `DELETE`.

`INSERT` and `UPDATE` need the column names.
Expand the object in the navigator to load its columns first.
Until then, the menu shows `Expand this object to load columns.`

## Quick search

Quick search finds screens, tabs, database objects and SQL from the history.

1. Choose `View` > `Quick switch…`, or press `Ctrl+P` (`Command+P` on macOS).
2. Type a part of a name or of a query.
3. Select a result and press `Return`.

Quick search shows a maximum of 80 results.

## Disconnect

1. Right-click the connection in the navigator.
2. Choose `Disconnect`.
3. Read the message in the `Disconnect database session` dialog.
4. Click `Disconnect`, or `Roll back and disconnect` if a transaction is open.

A disconnect rolls back an uncommitted transaction.
If a query, a fetch or an export is active, the disconnect cancels it.
The default button of the dialog is `Cancel`.

A disconnect does not delete the connection profile.
The SQL editors and their text stay open.
You can also disconnect from the context menu of a connection profile in the `Connections` section.
