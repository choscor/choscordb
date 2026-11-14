# ChoscorDB

ChoscorDB is a native, cross-platform SQL client with a Qt 6 Widgets frontend and a Rust engine.

## Features

- Connect to SQLite, PostgreSQL, MySQL and MariaDB.
- Keep passwords in the credential store of the operating system. Connect through an SSH tunnel or TLS.
- Write SQL with completion, search and replace, and run options.
- Read large results page by page. Filter, sort and edit table rows.
- Copy rows, or export results to CSV, JSON, JSON Lines or SQL `INSERT` statements.
- Browse schemas, tables, views, indexes and keys. See the DDL and an ERD of each table.
- Pin objects, and find objects, tabs and SQL text with quick search.
- Save queries, run queries again from the history, and recover unsaved SQL after a crash.
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
