# ChoscorDB

A native, cross-platform SQL client built with Qt 6 Widgets and a Qt-independent Rust engine. PostgreSQL and SQLite are the MVP drivers. Licensed under GPL-3.0-or-later.

Implementation is in progress. See [the PRD](docs/prd-mvp.md), [UI reference](docs/design/README.md), and [verification status](docs/architecture/implementation-status.md). The visual reference is a design artifact, not a working desktop application.

To try the application with sample data, use the runnable
[PostgreSQL and SQLite examples](examples/databases/README.md).

## Development

Install the pinned tools, then use the repository-owned quality interface:

```sh
python -m pip install -r scripts/ci/requirements.txt
python scripts/ci/quality.py fast
python scripts/ci/quality.py native-dependencies
python scripts/ci/quality.py full
```

`fast` checks C/C++ and Rust formatting, Ruff lint/format, workflow syntax, full
Cargo dependency policy, both Python test suites, and all ordinary Rust
targets/features. `full` adds the strict native build and all CTest tests using
Qt's offscreen platform. Every included gate is also available as a focused
stage; run `python scripts/ci/quality.py --help` and see [CI and quality gate
documentation](docs/CI.md). Native build and run instructions are in
[docs/BUILD.md](docs/BUILD.md), and contribution policy is in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Dependency direction

`driver-api` defines shared contracts without Qt or concrete drivers. Database adapters implement these contracts. `storage` owns local metadata and migrations, never database credentials. `core` composes services and schedules I/O; `bridge` translates plain commands/events; `desktop` owns rendering and input. A driver is a compiled adapter registered by the composition root, not a third-party binary plugin.
