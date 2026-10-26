# Build and run

This page tells you how to build ChoscorDB from source, run it and run its tests.
ChoscorDB has drivers for SQLite, PostgreSQL and MySQL.
For the release procedures, read [macOS releases](release-macos.md) and [Windows and Linux releases](release-windows-linux.md).

## Prerequisites

- Rust 1.97.1. `rust-toolchain.toml` pins this version, and rustup installs it.
- CMake 3.28 or later, Ninja and a C++23 compiler.
- Qt 6.8 or later with the Widgets, Concurrent, Test and Svg modules, dynamically linked.
- QScintilla, built against the same Qt 6 installation.
- Git and network access for the first build.
  CMake gets Corrosion v0.6.1 from GitHub, and Cargo gets the Rust dependencies.

On Linux, you also need these packages:

- `pkg-config` and `libdbus-1-dev` for the Secret Service credential adapter.
- `libssl-dev` for TLS.

A development build needs no code signature and no update configuration.
It does not check the production update feed.

## Build on macOS

1. Install the dependencies with Homebrew:

   ```sh
   brew install qtbase qtsvg qscintilla2 ninja cmake
   ```

2. Configure the `dev` preset:

   ```sh
   cmake --preset dev -DCMAKE_PREFIX_PATH=/opt/homebrew
   ```

3. Build the application and the tests:

   ```sh
   cmake --build --preset dev
   ```

4. Run the native tests:

   ```sh
   ctest --preset dev
   ```

5. Start ChoscorDB:

   ```sh
   open build/dev/choscordb.app
   ```

On an Intel Mac, Homebrew uses `/usr/local` in place of `/opt/homebrew`.
The Homebrew Qt bottle sets the minimum macOS version of this build.
This build is for local development only.
It is not a package that you can give to other users.
To set a different minimum macOS version, set `CMAKE_OSX_DEPLOYMENT_TARGET`.
Then use Qt and QScintilla binaries that are built for that target.

## Build on other systems

1. Install Qt and QScintilla.
2. Give their installation roots to CMake in `CMAKE_PREFIX_PATH`.
3. Use the same `dev` configure, build and test presets as on macOS.

As an alternative, `scripts/ci/quality.py` can install the pinned Qt and QScintilla versions in `build/ci`.
Read [Continuous integration and quality gates](ci.md) for these commands.

## CMake presets

`CMakePresets.json` defines these configure presets:

| Preset | Build directory | Purpose |
| --- | --- | --- |
| `dev` | `build/dev` | Debug build with tests. |
| `release` | `build/release` | Release build without tests. Use it to install. |
| `strict` | `build/strict` | `dev` with `CHOSCORDB_ENABLE_STRICT_WARNINGS` set explicitly. |
| `clang-tidy` | `build/clang-tidy` | `strict` with the clang-tidy targets. |
| `sanitizers` | `build/sanitizers` | `strict` with ASan and UBSan. Needs LLVM 23. |
| `coverage` | `build/coverage` | `strict` with C++ coverage. Needs LLVM 23. |
| `iwyu` | `build/iwyu` | `strict` with the Include-What-You-Use target. |

`CHOSCORDB_ENABLE_STRICT_WARNINGS` is on by default.
Thus all builds treat first-party C++ warnings as errors.

## Version

`workspace.package.version` in `Cargo.toml` is the application version, in the form `X.Y.Z`.
CMake reads this value for the bundle and executable metadata.
To prepare a release, change this value and the `CHANGELOG.md` entry yourself.
The packaging scripts do not edit version files.

## Application identity and data

The executable is `choscordb`.
The macOS bundle identifier and the credential service name are `com.choscor.ChoscorDB`.
Environment variables and CMake options start with `CHOSCORDB_`.

ChoscorDB keeps its data in `choscordb.sqlite`.
This file is in the `com.choscor.ChoscorDB` folder of the Qt generic data location.
On macOS, the path is `~/Library/Application Support/com.choscor.ChoscorDB/choscordb.sqlite`.
Connection profiles, history, preferences and recovery data use this one database.
Passwords and other secrets go to the credential store of the operating system.

## Verify a change

Run the Rust checks:

```sh
cargo test --workspace --locked
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --locked -- -D warnings
```

Run the native build and tests:

```sh
cmake --build --preset dev
ctest --preset dev
```

The test preset uses the Qt `offscreen` platform.
The tests use real SQLite through Rust, CXX and Qt.
The live PostgreSQL and MySQL tests need a disposable server.
Read [Tests and fixtures](testing.md) for these fixtures.

Before you finish a change, run the quality gates:

```sh
python3 scripts/ci/quality.py fast
```

## Headless smoke test on macOS

The executable has options for a smoke test and for screenshots:

```sh
QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb --smoke-test
QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb --screenshot build/dev/workspace.png
```

`--smoke-test` starts the Qt event loop and then stops.
If `CHOSCORDB_TEST_DATA_DIR` is set, the smoke test keeps its data in that folder.
`--screenshot` writes a workspace image and then stops.
Use `--screenshot-theme` with `system`, `light` or `dark` to set the theme.
Use `--screenshot-size` to set the window size, for example `960x640`.
The default size is `1280x900`.

## Build ownership

- CMake owns native compilation, moc and test registration.
- Corrosion imports the `choscordb-bridge` Cargo static library and links the Rust runtime dependencies.
- CMake links the system libraries that the Rust code needs, for example the Security framework on macOS.
- On Linux, CMake links D-Bus and OpenSSL.
- `crates/bridge/build.rs` runs CXX one time.
- CMake copies the generated headers from `CHOSCORDB_CXX_INCLUDE_DIR` to the include directory.
- Qt types stay in C++. The Rust crates do not link Qt.

For more data, read the [Corrosion usage documentation](https://corrosion-rs.github.io/corrosion/usage.html) and the [CXX CMake guide](https://cxx.rs/build/cmake.html).
Do not run two Ninja builds in the same build directory at the same time.
Cargo locks each target directory, but Ninja does not.

## Build in Git worktrees

Each worktree has its own `build/` and `target/` directories.
Keep these directories in their worktree.
CMake records absolute source paths, and the bridge writes the CXX headers into the worktree build tree.
Do not copy or link build directories between worktrees.
Share a compiler cache in their place.

To share a compiler cache, do these steps:

1. Install `sccache` one time, for example with `brew install sccache`.
2. Configure and build with the usual commands.
3. Show the cache statistics with `sccache --show-stats`.

`.cargo/config.toml` and `CMakeLists.txt` call `scripts/dev/compiler-cache` as the compiler launcher.
If `sccache` is on `PATH`, this script uses it.
If `sccache` is not available, the script calls the compiler directly.
CMake saves the launcher in `CMakeCache.txt`.
Configure an old build directory again to get the launcher.

Cargo turns off incremental compilation so that `sccache` can cache Rust compiler calls.
The cache is most useful for Rust dependencies.
C++ debug objects can miss the cache when their absolute paths are different.
The cache saves build time, not disk space.
It does not share application data between worktrees.

To build one time without the cache, set `CMAKE_CXX_COMPILER_LAUNCHER` and `RUSTC_WRAPPER` to empty values.

## Debug information and disk space

The `dev` Cargo profile keeps line tables for workspace crates.
It omits debug information from dependencies.
For full Rust type and variable data, run `cargo build --profile debugging`.

Cargo keeps old outputs when build flags or features change.
Thus `target/` and `build/dev/cargo/` can become large.
To recover the disk space, do these steps:

1. Run `cargo clean`.
2. Remove `build/dev/`.
3. Configure and build again.

This procedure removes only generated outputs.
The shared `sccache` cache stays available for the next build.

## Credential store on Linux

The native credential operations need a Secret Service that runs and is unlocked.
The usual tests do not need it.
They use injected credential stores or stores that are marked unavailable.
