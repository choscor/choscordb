# Results

The result grid shows the rows of a query or of a table.
This page tells you how to read, copy, edit and export these rows.

## Result pages

ChoscorDB shows the rows in result pages.
A result page has 1,000 rows by default.
To change the page size, see [Query settings](sql-editor.md#query-settings).

- Click `Next page` to show the next result page.
- Click `Previous page` to show the previous result page.

A result page can have fewer rows when the rows are large or when the result ends.
The status line shows the page number, the row count and the memory that the visible rows use.

A query can give more than one result.
On the last page of a result, `Next page` changes to `Next result`.
Click `Next result` to show the next result.

## Column headers

Each column header shows the column name and the database type.
If the driver gives the precision and the scale, the header shows them too.
Put the pointer on a header to see all column data:

- `Name`
- `Database type`
- `Precision`
- `Scale`
- `Timezone`
- `Nullability`

If the driver does not give a value, the detail shows `Unknown`.

## Large values

The result grid does not keep the full value of very large text or binary cells.
To read such a value, double-click the cell.
The `Value detail` window shows one part of the value.
Click `Next` and `Previous` to read the other parts.
Text shows as escaped UTF-8 text, and binary data shows as hexadecimal.

## View values as JSON

Right-click the result grid to see the JSON views:

- `View cell as JSON` shows the value of one cell.
- `View row as JSON` shows one row as a JSON object.
- `View table as JSON` shows the rows of the current result page as JSON.

Click `Copy JSON` to copy the JSON text.
A JSON view can load a maximum of 8 MiB of complete values.

## Copy rows

1. Select cells in the result grid.
2. Press `Ctrl+C` (`Command+C` on macOS) to copy the selected cells.

Right-click the result grid for more copy actions:

- `Copy selected cells` copies the selected cells.
  Cells that you did not select in the rectangle stay empty.
- `Copy selected rows` copies all columns of each row that has a selected cell.
- `Copy current page` copies all rows and columns of the current result page.

ChoscorDB copies the rows as text with tab characters between the columns.
The copy does not include the column headers.

- A `NULL` value copies as `NULL`.
- An empty string copies as an empty field.
- A value with a tab, a quote or a line break copies in double quotes.
- A binary value copies as hexadecimal text that starts with `0x`.

The copy loads the full value of large cells.
The copy can load a maximum of 8 MiB of such values.
If the copy is too large, ChoscorDB shows an error and does not change the clipboard.
A copy does not run SQL again.

## Edit rows

You can edit the rows of a table when ChoscorDB can identify each row.
If ChoscorDB cannot identify each row, the result grid is read-only.
A table without a primary key accepts only new rows.

Right-click the result grid to see the edit commands and their shortcuts:

- `Edit cell…` opens the cell in the side sheet.
- `Duplicate row` adds a copy of the row as a new row.
- `Add row` adds a new row.
- `Delete rows` marks the selected rows for deletion.
- `Restore rows` removes the deletion mark from the selected rows.
- `Set NULL` sets the selected cells to `NULL`.
- `Apply…` reviews and applies the changes.
- `Discard` removes all changes that you did not apply. ChoscorDB asks you to confirm.

The Object Data view also has an `Apply…` button above the grid.

### Keyboard shortcuts for edits

These shortcuts work when the result grid has the keyboard focus.
On macOS, `Ctrl` is `Cmd`.

| Command | Shortcut |
| --- | --- |
| `Add row` | `Ctrl+N` |
| `Duplicate row` | `Ctrl+D` |
| `Delete rows` | `Ctrl+Alt+-` |
| `Restore rows` | `Ctrl+Alt+=` |
| `Set NULL` | `Delete` or `Backspace` |
| `Edit cell…` | `Shift+Enter` |
| `Apply…` | `Ctrl+S` |
| `Discard` | `Ctrl+Shift+Backspace` |

`Duplicate row` and `Edit cell…` use the current cell.
From the menu, they use the cell that you right-click.
The other row commands use the selected rows or cells.

When the grid has the focus, the grid uses these keys.
For example, `Ctrl+S` applies the grid changes and does not save the SQL file.
To edit a value in the grid, press `F2`. On macOS, press `Enter`.
While you edit a value, the keys edit the text.
Only `Ctrl+S` is different: it keeps the value and then opens the review.
If a command is not available, the grid shows the reason in the messages.

On some keyboard layouts, `Ctrl+Alt` is the same as `AltGr`.
If `Ctrl+Alt+-` or `Ctrl+Alt+=` types a character, set different shortcuts in Preferences.

ChoscorDB does not change the database until you apply the changes.

1. Choose `Apply…` or press `Ctrl+S`.
2. In `Review grid changes`, read the statements and the bound parameter values.
3. Click `Apply` to run the statements.

ChoscorDB applies the changes in their own transaction.
If ChoscorDB cannot apply all changes, it applies none of them.
If a transaction is open, run `COMMIT` or `ROLLBACK` before you apply grid changes.
If you try to leave the result with changes that you did not apply, ChoscorDB asks you to apply or discard them.

## Export rows

The export writes all rows of the current result to a file.
It does not use only the rows of the current page.

1. Click `Export` below the result grid.
2. In `Format`, select `CSV`, `JSON`, `JSON Lines` or `SQL INSERT`.
3. In `Destination`, type a file path, or click `Browse…`.
4. For `SQL INSERT`, select the `SQL dialect`: `SQLite`, `PostgreSQL` or `MySQL`.
5. For `SQL INSERT`, type the `Table` name, and optionally the `Schema` name.
6. Click `Export`.

The `Table` and `Schema` fields each take one name.
A dot in a name is part of the name.
A CSV file starts with a row of column names.

If the file exists, ChoscorDB asks before it replaces the file.
ChoscorDB writes the new file only when the export is complete.
An export that fails or that you cancel does not change the existing file.

The dialog shows the number of rows and bytes during the export.
Click `Cancel export` to stop the export.
When the export is complete, a notification in the corner of the window shows the file name.
If the export fails, the dialog shows the error and a `Retry` button.

In the `Data` pane of a table, the export uses the active filters and sort order.
For more information, see [Table data](navigator.md#table-data).
