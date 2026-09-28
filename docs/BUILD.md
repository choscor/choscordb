# Build and run

ChoscorDB uses the `choscordb` executable, `CHOSCORDB_*` environment variables and CMake options, and the `com.choscor.ChoscorDB` credential service and macOS bundle identity. On macOS, application data lives in `~/Library/Application Support/com.choscor.ChoscorDB/choscordb.sqlite`. Profiles, history, preferences, and recovery share this database. The identity change starts fresh: old development data and credential entries are neither read, migrated, overwritten, nor deleted. Updates using the new identity continue to use the same saved data and credential references.

The executable supports SQLite and PostgreSQL, saved connection profiles with macOS credentials, lazy navigation, editor completion/preferences, paged and cached results, large-value inspection, copying, export, history, and recovery. See [macOS releases](MACOS_RELEASE.md) for the separate Apple Silicon production workflow, requiring macOS 26.0 or newer. Normal development builds require no signing or update configuration and do not check the production update feed.

## Prerequisites

- Rust 1.97.1 (pinned by `rust-toolchain.toml`; rustup installs it).
- CMake 3.28 or newer, Ninja, and a C++23 compiler.
- Qt 6.8+ dynamically linked Widgets, Concurrent, Test, and Svg.
- QScintilla built against the same Qt 6 installation.
- Git and network access for the pinned Corrosion checkout and Cargo dependencies on the first build.

On macOS with Homebrew:

```sh
brew install qtbase qtsvg qscintilla2 ninja cmake
cmake --preset dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset dev
ctest --preset dev
open build/dev/choscordb.app
```

Intel Homebrew normally uses `/usr/local` instead. Homebrew's Qt bottle determines the supported macOS version; this is a local development build, not a redistributable release package. Set `CMAKE_OSX_DEPLOYMENT_TARGET` explicitly when preparing a deployment toolchain, and use Qt/QScintilla binaries built for that target.

On other systems, supply the Qt and QScintilla installation roots through `CMAKE_PREFIX_PATH`, then use the same configure/build/test presets. The separate [Windows and Linux release guide](WINDOWS_LINUX_RELEASE.md) describes unsigned candidate packages and GitHub Release assets.

`Cargo.toml`'s `workspace.package.version` is the authoritative stable `X.Y.Z`
application version. CMake reads it for bundle and executable metadata. Change it
and the corresponding `CHANGELOG.md` entry explicitly when preparing a release;
packaging never edits version files.

Choose Connection → New SQLite session and enter a database path or `:memory:`. Run the current SQL statement with the toolbar or Ctrl+Return. Expand the navigator to fetch objects. Results fetch one page at a time; Next page requests more rows and Previous page retrieves saved rows without rerunning SQL. Commit/Rollback are available in manual mode. A selection containing multiple statements is explicitly rejected by SQLite; it never executes only a prefix silently.

## Verification

```sh
cargo test --workspace --locked
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --locked -- -D warnings
cmake --build --preset dev
ctest --preset dev
```

Headless native smoke on macOS:

```sh
QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb --smoke-test
QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb --screenshot build/dev/workspace.png
```

The test preset selects Qt's offscreen platform. Tests exercise real SQLite through Rust, CXX, and Qt, including a complete million-row adapter traversal and an active write interrupted during engine destruction. The separate owned PostgreSQL fixture covers TLS, commands, cancellation, network loss, shutdown and restart.

## Build ownership

CMake owns native compilation, moc, and test registration. Corrosion imports the Cargo static library and links Rust runtime dependencies; CMake also links the credential backend frameworks or D-Bus library. `crates/bridge/build.rs` runs CXX once and stages generated headers through `CHOSCORDB_CXX_INCLUDE_DIR`. Qt types stay in C++; Rust services and database adapters do not link Qt. Integration follows the [Corrosion usage documentation](https://corrosion-rs.github.io/corrosion/usage.html) and [CXX build guidance](https://cxx.rs/build/cmake.html).

Avoid running multiple Ninja builds against the same build directory concurrently. Cargo serializes access to each target directory independently.

## Building in Git worktrees

Each worktree has its own ignored `build/` and `target/` directories. Keep those
directories local to the worktree: CMake records absolute source paths, and the
Rust bridge stages generated CXX headers in that worktree's CMake build tree.
Share a compiler cache instead of copying or symlinking build directories.

Install `sccache` once (for example, `brew install sccache` on macOS). The
checked-in Cargo configuration and CMake default launcher then use it
automatically in every new worktree; without it they invoke the compilers
directly. No per-worktree cache setup is needed. Build with the normal commands:

```sh
cmake --preset dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset dev
cargo test --workspace --locked
ctest --preset dev
sccache --show-stats
```

Use the Qt/QScintilla prefix for your machine in place of `/opt/homebrew`.
The CMake launcher setting is saved in that worktree's `build/dev/CMakeCache.txt`;
reconfigure an existing build directory once to pick up the new default.
Cargo disables incremental compilation so its Rust invocations can use the
shared cache. The first build still configures CMake and links native targets;
later worktrees can reuse cached compiler results when inputs and toolchains
match. Rust dependencies are the main cross-worktree benefit. C++ Debug
objects can miss when their absolute source or debug paths differ; forcing
reuse of those objects can point a debugger at the wrong worktree. Each
worktree still stores its own build outputs, so the compiler cache saves time
rather than worktree disk space. A shared compiler cache does not share
application data. Set
`CMAKE_CXX_COMPILER_LAUNCHER` to an empty value and `RUSTC_WRAPPER` to an empty
value to bypass the cache for a particular build.

Development and test builds keep Rust source line tables while omitting full
debug information from dependencies. Use `cargo build --profile debugging` when
full Rust type and variable information is needed. Cargo keeps old artifacts
when build flags or features change, so `target/` and `build/dev/cargo/` can
still grow over time. To reclaim them, run `cargo clean`, remove `build/dev/`,
then configure and build again with the commands above. This removes generated
outputs only; the shared `sccache` remains available for the rebuild.

Linux builds also require `pkg-config` and `libdbus-1-dev` for the Secret Service credential adapter, and `libssl-dev` for PostgreSQL TLS. A running, unlocked Secret Service is needed only for native credential operations; ordinary tests use injected or explicitly unavailable stores.

For a disposable PostgreSQL server and verified-TLS integration checks, see [PostgreSQL testing](testing/postgres.md).

The normal app restores editor tabs from its local metadata database without connecting or executing SQL. Recovery snapshots preserve unsaved buffers separately from explicit SQL file saves. Closing waits for the latest snapshot acknowledgement; persistence failures expose retry and explicit discard choices.

View → Query history lists completed and interrupted executions. Record history can be disabled, and Clear history requires confirmation. Open in new query copies SQL into an editor without executing it. Normal close flushes actor history after disconnecting sessions.

Edit → Find/Replace searches the active editor. Find and Replace all use background snapshots; results are discarded if their target changes. Replace all is one undoable change. The panel supports literal text, Unicode whole-word matching, case selection, and wrapping.
