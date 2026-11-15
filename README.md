# ChoscorDB

ChoscorDB is a native, cross-platform SQL client with a Qt 6 Widgets frontend and a Rust engine.

## Features

- Connect to SQLite, PostgreSQL, MySQL and MariaDB.
- Keep passwords in the credential store of the operating system. Connect through an SSH tunnel or TLS.
- Write SQL with completion, search and replace, and run options.
- Format or minify SQL, and toggle line or block comments.
- Set a row limit for `SELECT` statements on each connection.
- Run all statements and see one result tab for each statement. Name a tab with a `-- name:` comment, pin a tab, and stop on the first error.
- Switch the database of a SQL tab (Ctrl+K). For SQLite, switch to another database file in the same folder.
- Tag each connection with an environment and a color. The sidebar and the result footer show the tag.
- Set a safety level for each connection: run without confirmation, confirm destructive statements, confirm each statement, or read-only.
- Read large results page by page. Filter, sort and edit table rows.
- Set a cell to NULL, DEFAULT, an empty value or Now() from the Set value menu. Pick a date and time for date and time cells. Load a binary cell from a file, or save it to a file.
- Paste a block of cells from a spreadsheet into the grid. Set one value in many selected rows.
- Explain a statement and see its query plan as a tree with costs and row estimates.
- Sort by up to four columns with Shift+click. Hide result columns with the column chooser (Ctrl+Alt+F). ChoscorDB keeps the hidden columns for each table.
- Copy rows as TSV, CSV, Markdown, HTML, JSON or SQL `INSERT` statements.
- Create, rename and drop PostgreSQL, MySQL and MariaDB databases with a SQL preview.
- Export results to CSV, JSON, JSON Lines, Excel or SQL `INSERT` statements.
- Export tables or views from the navigator to CSV, JSON, JSON Lines, Excel or SQL with structure, DROP and data.
- Import CSV, JSON or JSON Lines files into a table or a new table with a preview and column mapping.
- Run a SQL file or a dump. Stop at the first error, continue after errors, or run it in one transaction.
- Edit the columns, indexes and keys of a table or create a table. Review the SQL before you apply it.
- Set the CSV delimiter, quote, line break, NULL text and header row.
- Browse schemas, tables, views, indexes and keys. See the DDL and an ERD of each table.
- Start a new table, view, function, procedure or schema from a SQL template in the navigator.
- Rename, clone, truncate or drop a table from the navigator. Each action shows its SQL before it runs. Copy the CREATE, DROP, TRUNCATE or SELECT script of an object.
- See the triggers of a table. Create, edit or drop a trigger from SQL that opens in a query tab.
- Pin objects, and find objects, tabs and SQL text with quick search.
- Save queries, run queries again from the history, and recover unsaved SQL after a crash.
- Keep the original rows of each applied grid change in the query history.
- Preview JSON values as a tree with pretty and minified text, show images, and show geometry as WKT.
- Put saved queries in folders and sort them by name or date. Give a saved query a keyword to insert it from completion.
- Update the app from a signed update feed. Use the Light or Dark theme.

## Download

Get ChoscorDB from [GitHub Releases](https://github.com/choscor/choscordb/releases).

- macOS 26 or later on Apple silicon: open the signed and notarized `.dmg` file and drag ChoscorDB to `Applications`.
- Windows x64: start the `-setup.exe` installer. It is not signed, so SmartScreen can show a warning.
- Linux x86_64: make the `.AppImage` file executable with `chmod +x`, then start it. It is not signed.

## Build from source

```sh
python3 -m pip install -r scripts/ci/requirements.txt
python3 scripts/ci/quality.py native-dependencies
python3 scripts/ci/quality.py native-build
```

[docs/build.md](docs/build.md) gives the full build procedure.

## Documentation

- [Developer documentation](docs/README.md): build, test, architecture and release.

## Contributing

You can create an issue to report a bug or to ask for a change. ChoscorDB does not accept external pull requests.
[CONTRIBUTING.md](CONTRIBUTING.md) gives the full policy.

## License

ChoscorDB is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).
