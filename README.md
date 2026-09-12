# ChoscorDB

A native, cross-platform SQL client built with Qt 6 Widgets and a Qt-independent Rust engine. PostgreSQL and SQLite are the MVP drivers. Licensed under GPL-3.0-or-later.

Implementation is in progress. See [the PRD](docs/prd-mvp.md), [UI reference](docs/design/README.md), and [verification status](docs/architecture/implementation-status.md). The visual reference is a design artifact, not a working desktop application.

## Development

Install the pinned Rust toolchain through rustup. Run `cargo test --workspace`, `cargo fmt --all -- --check`, and `cargo clippy --workspace --all-targets -- -D warnings` for Rust verification. See [native build and run instructions](docs/BUILD.md).

## Dependency direction

`driver-api` defines shared contracts without Qt or concrete drivers. Database adapters implement these contracts. `storage` owns local metadata and migrations, never database credentials. `core` composes services and schedules I/O; `bridge` translates plain commands/events; `desktop` owns rendering and input. A driver is a compiled adapter registered by the composition root, not a third-party binary plugin.
