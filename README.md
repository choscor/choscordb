# ChoscorDB

ChoscorDB is a native, cross-platform SQL client with a Qt 6 Widgets frontend and a Rust engine.

## Features

- Save connection profiles with credentials in the system credential store, and SSH tunnel and TLS options.
- Write SQL in an editor with completion, search and query settings.
- Read large results page by page in the result grid, and edit table rows.
- Copy rows, and export results to CSV, JSON, JSON Lines or SQL `INSERT` statements.
- Browse schemas, tables, views and columns in the navigator, and find objects with quick search.
- Run queries again from the history, and get unsaved SQL editor text back after an unexpected stop.

## Supported databases

ChoscorDB supports SQLite, PostgreSQL and MySQL.

## Download

Get ChoscorDB from [GitHub Releases](https://github.com/choscor/choscordb/releases). The macOS DMG is signed and notarized. The Windows and Linux packages are unsigned, so your system can show a security warning.
The [installation guide](user-guide/install.md) gives the steps.

## Build from source

```sh
python3 -m pip install -r scripts/ci/requirements.txt
python3 scripts/ci/quality.py native-dependencies
python3 scripts/ci/quality.py native-build
```

[docs/build.md](docs/build.md) gives the full build procedure.

## Documentation

- [User guide](user-guide/README.md): install and use ChoscorDB.
- [Developer documentation](docs/README.md): build, test, architecture and release.

## Contributing

You can create an issue to report a bug or to ask for a change. ChoscorDB does not accept external pull requests.
[CONTRIBUTING.md](CONTRIBUTING.md) gives the full policy.

## License

ChoscorDB is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).
