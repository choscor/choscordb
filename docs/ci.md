# Continuous integration and quality gates

This page describes the quality gates of ChoscorDB and the GitHub Actions workflows that run them.
It also tells you how to run each gate on your computer.

## The quality runner

`scripts/ci/quality.py` is the one entry point for the quality gates.
Each quality gate is a stage of this script.
The script prints each command before it runs it.
If a tool or a dependency is missing, the script stops with exit code 2.
It then shows the command that installs the missing item.

Run the gates from the repository root:

```sh
python3 -m pip install -r scripts/ci/requirements.txt
python3 scripts/ci/quality.py fast
python3 scripts/ci/quality.py native-dependencies
python3 scripts/ci/quality.py full
```

To run one stage, give its name, for example `python3 scripts/ci/quality.py ui-policy`.

### Fast stages

`fast` runs these stages in this order:

| Stage | What it checks |
| --- | --- |
| `cpp-format` | `clang-format --dry-run --Werror` on the C, C++ and Objective-C++ files in `desktop/` and `tests/`. Needs LLVM 23. |
| `cpp-size` | Each of these files has 1,000 lines or fewer. |
| `cpp-ownership` | The Rust and C++ ownership boundary. |
| `qss-lint` | Qt stylesheet rules (`scripts/ci/qss_policy.py`). |
| `ui-policy` | Visual constants, stylesheets and icons in screen code (`scripts/ci/ui_policy.py`). |
| `ui-consistency` | The census of UI control construction sites (`scripts/ci/ui_consistency.py`). |
| `perf-policy` | Code patterns that become slow with large data (`scripts/ci/perf_policy.py`). |
| `source-inventory` | Dead or copied native code (`scripts/ci/source_inventory.py`). |
| `docs-policy` | Documentation links and sentence length (`scripts/ci/docs_policy.py`). |
| `python-lint` | `ruff check` on `scripts` and `examples`. |
| `python-format` | `ruff format --check` on `scripts` and `examples`. |
| `actionlint` | The workflow files. Needs actionlint 1.7.7. |
| `python-tests` | The `test_*.py` suites in `scripts/ci` and `scripts/release`. |
| `rust-format` | `cargo fmt --all -- --check`. |
| `rust-check` | `cargo check` on all workspace targets and features. |
| `rust-clippy` | `cargo clippy` on all targets and features, with warnings as errors. |
| `rust-tests` | `cargo test` on the workspace with all features. |
| `cargo-deny` | Advisories, bans, licenses and sources of the Cargo graph. Needs cargo-deny 0.20.2. |

### Native stages

| Stage | What it does |
| --- | --- |
| `native-dependencies` | Installs Qt 6.8.3 and QScintilla 2.14.1 in `build/ci`. |
| `native-build` | Configures and builds a Release build with tests in `build/ci/native`. |
| `native-tests` | Runs all CTest tests with the Qt `offscreen` platform. |

`full` runs all fast stages, then `native-build` and `native-tests`.
It does not run `native-dependencies`.
Run that stage one time before `full`.

`scripts/ci/desktop.py` defines the native stages.
It keeps Qt, QScintilla and the native build in `build/ci` and never uses `build/dev`.
It gives the Qt runtime paths to the compiler and to the tests.

## Policy gates

Most policy gates accept a reviewed marker for one line.
Put the marker on the line or on the line above it.
A marker without a reason does not count.
Fix the finding before you add a marker.

| Gate | Marker | Exception list |
| --- | --- | --- |
| `ui-policy` | `// ui-ok: <reason>` | None |
| `perf-policy` | `// perf-ok: <reason>` | None |
| `cpp-ownership` | `// ui-budget: <reason>` for a UI number that is equal to a Rust limit | `scripts/ci/cpp_ownership_exceptions.json` |
| `source-inventory` | None | `EXCEPTIONS` in `scripts/ci/source_inventory.py` |
| `docs-policy` | `<!-- docs-ok: <reason> -->` | None |

Use `--json` with `ui_consistency.py`, `perf_policy.py`, `source_inventory.py`, `cpp_ownership.py` or `docs_policy.py` to get the findings as JSON.

### ui-policy

`ui-policy` makes sure that screens in `desktop/app/` and `desktop/widgets/` use the design system.
It rejects these items:

- Literal sizes in any argument position, for example margins, column widths, section sizes and paint geometry.
- RGB colors and `Qt::` colors.
- Raw menu popups and stock dialog button boxes.
- Qt standard icons.
- `designRole`, `variant`, `state` or `designSurface` values that the design system does not style.
- Literal icon sizes. Icons use `Dimension::IconSmall` or `Dimension::Icon` only.
- Icon files that are not recorded upstream Lucide files or recorded brand logos.

The icon rules accept no marker.
Read [Design system](design-system.md) for the icon rules.
CTest also runs this gate as the `ui-source-policy` test.

### qss-lint

`qss-lint` makes sure that Qt style rules stay in QSS resource files next to their component.
QSS files refer to tokens by name.
They contain no `%N` placeholders and no literal `font-size` or radius values.
The gate also reports a selector and property pair that two files of the shared cascade both define.

### ui-consistency

`ui-consistency` counts the sites that construct leaf visual controls in `desktop/app/` and `desktop/widgets/`.
A site is covered when it constructs a design-system control or a stock Qt control with shared style.
`ThemeManager`, `ControlStyle` and the shared QSS give the stock controls their style.
The gate rejects screen-owned presentation and leaf controls that it cannot classify.

The percentage is an estimate from the source text.
It does not measure the rendered UI or the runtime behavior.
Run `python3 scripts/ci/ui_consistency.py --json` for the full census.
If you add a new UI component, make sure that its call site is in this census.
If the component uses a new construction pattern, extend the checker and add a regression test.

### perf-policy

`perf-policy` rejects patterns whose cost grows with result pages, selections, navigator trees or pin lists:

- Result columns or rows that size to their contents.
- `selectedIndexes()` lists in place of a walk through the selection ranges.
- Proxy filters without a debounce timer.
- Trees without uniform row heights, or trees that expand all rows.
- Hover repaints of the full viewport.
- One zero-delay timer for each model signal.
- Bridge calls, SVG parsers, image loads, regex compilation and linear searches in paint, size-hint, `data()` or `parent()` paths.
- Helper functions that a hot path calls and that call `EngineAdapter`. The gate follows the helper calls.

### source-inventory

`source-inventory` rejects dead and copied native code:

- Translation units that CMake does not name.
- Headers that no file includes.
- Member functions that no code calls.
- Qt signals that no code connects.
- Anonymous-namespace helpers that are copied between files.
- Names in the `ui_consistency.py` component lists that no longer exist.

An exception in `EXCEPTIONS` needs a reason.
An exception that no longer matches a finding fails the gate.

### cpp-size

`cpp-size` limits each handwritten C, C++ and Objective-C++ source and header file to 1,000 lines.
The scope is `desktop/` and `tests/`, with the same file suffixes as `cpp-format`.
The gate counts blank lines and comments.
A file with exactly 1,000 lines passes.
The gate scans the files on disk, so it also checks new files that Git does not track.
CTest also runs this gate as the `cpp-source-size` test.

There is no exception list.
If a file becomes too large, split it by responsibility.
Do not split it into numbered parts.
File size does not measure cohesion, so also keep each function short and single-purpose.

### cpp-ownership

`cpp-ownership` makes sure that C++ does not take behavior that belongs to Rust.
Read [Architecture](architecture.md) for the rules, the exceptions and the limits of this gate.

### docs-policy

`docs-policy` checks the Markdown pages in `docs/`, `user-guide/` and the root `README.md`, `CONTRIBUTING.md` and `CLAUDE.md`:

- Relative links, `#anchor` targets and `docs/` or `user-guide/` paths must resolve.
- Each glob in the map of `docs/README.md` must match a file.
- A sentence in a list item has 20 words or fewer.
- Other sentences have 25 words or fewer.

Code, tables, headings and link targets do not count.
Read the [style guide](style-guide.md) for the rules of the text.

The CI workflow also runs a second check on each pull request:

```sh
python3 scripts/ci/docs_policy.py --changed --base origin/<base>
```

This check uses the map in `docs/README.md`.
The map connects code paths to the pages that own them.
If a pull request changes mapped code, it must also change an owning page.
If no page needs a change, add this line to the pull request body or to a commit message:

```text
Docs: none needed, <reason>
```

## Workflows

All workflows have read-only permissions and do not keep checkout credentials.
Each job has a timeout.
A new run cancels the older run for the same reference.
Actions are pinned to full commits.

| Workflow | Triggers | Jobs |
| --- | --- | --- |
| `ci.yml` | Pull requests, pushes to `main` or `master`, manual | Deterministic quality, Rust and native, native static analysis, PostgreSQL 17 integration, MySQL 8.4 integration |
| `coverage.yml` | Pull requests, pushes to `main` or `master`, manual | Rust LCOV, C++ LCOV |
| `security.yml` | Pull requests, pushes to `main` or `master`, weekly, manual | Dependency review on pull requests |
| `dynamic-analysis.yml` | Weekly, manual | ASan and UBSan, timing stress |
| `cross-platform-release.yml` | Pull requests that change build inputs, manual with a tag | Windows x64 package, Linux x86_64 AppImage |

All macOS jobs run on `macos-15` (arm64).
The release workflow uses `windows-2022` with MSVC and `ubuntu-24.04` with GCC.
Read [Windows and Linux releases](release-windows-linux.md) for that workflow.

### Deterministic quality

This job runs `python scripts/ci/quality.py fast`.
On a pull request, it then runs `docs_policy.py --changed` against the base branch.

### Rust and native

This job runs `rust-check`, `rust-clippy` and `rust-tests`.
Then it runs `native-dependencies`, `native-build` and `native-tests`.
It sets `MACOSX_DEPLOYMENT_TARGET=13.0` for Rust native dependencies, qmake and CMake.
This setting is a build setting only.
It does not prove that ChoscorDB starts on macOS 13.

### Native static analysis

This job uses LLVM 23 and runs these targets:

- `choscordb-clang-tidy` in the `clang-tidy` preset. This step does not block the job.
- `choscordb-header-check`, which compiles each public header alone. This step blocks the job.
- `choscordb-iwyu` in the `iwyu` preset. This step is advisory.

### Database integration

The PostgreSQL job starts the repository-owned PostgreSQL 17 fixture.
It then runs the ignored driver and core suites and the native tests, one at a time.
The MySQL job starts a local MySQL 8.4 server on port 33306.
It then runs the ignored driver and core suites.
GitHub service containers need Linux runners, so these jobs start the servers directly.
Each job keeps the server log when it fails.
Read [Tests and fixtures](testing.md) to run the same fixtures on your computer.

### Coverage

`coverage.yml` makes separate LCOV artifacts for Rust and for first-party C++.
Each job rejects an empty report or a report without first-party `SF:` records.
There is no minimum percentage.
Do not add the Rust and C++ percentages together, because their tools and source sets are different.

### Dynamic analysis

The sanitizer job builds the `sanitizers` preset and runs CTest with ASan and UBSan.
Sanitizers instrument first-party C++ only.
Rust, Qt, QScintilla and other libraries are not covered.
The stress job runs the `workspace`, `disconnect` and `history` tests up to 20 times and stops at the first failure.

## Tool versions

| Tool | Version | Where it is pinned |
| --- | --- | --- |
| Rust | 1.97.1 | `rust-toolchain.toml` |
| Python | 3.12 | Workflow files |
| Qt | 6.8.3 | `scripts/ci/quality.py`, `scripts/ci/desktop.py` |
| QScintilla | 2.14.1 | `scripts/ci/bootstrap_qscintilla.py` |
| LLVM | 23 | `scripts/ci/quality.py`, CMake presets |
| aqtinstall, CMake, Ninja, Ruff | See the file | `scripts/ci/requirements.txt` |
| actionlint | 1.7.7 | `scripts/ci/quality.py` |
| cargo-deny | 0.20.2 | `scripts/ci/quality.py` |
| cargo-llvm-cov, lcov2xml | 0.6.21, 1.0.9 | `coverage.yml` |

The LLVM stages reject a major version other than 23.
Dependabot proposes weekly updates for Cargo, pip and GitHub Actions.
It never merges them automatically.

## QScintilla bootstrap

`scripts/ci/bootstrap_qscintilla.py` builds QScintilla from the official Riverbank source archive.
It does these steps:

1. Downloads the archive, or uses the archive that you give with `--archive`.
2. Checks the SHA-256 digest before extraction.
3. Rejects archive links, devices and path traversal.
4. Builds a shared library with the `qmake` of the installed Qt.
5. Copies headers, libraries, provenance and the license into an isolated prefix.

The script does not change the system Qt installation.
On macOS, it builds only for the physical architecture of the host.
It reads that architecture from the host, not from the Python process, because Rosetta can report `x86_64`.

To build QScintilla for a local Qt 6.8 or later installation, run:

```sh
python3 scripts/ci/bootstrap_qscintilla.py \
  --qmake /path/to/qt/bin/qmake \
  --prefix build/ci/qscintilla-local \
  --work build/ci/qscintilla-local-source \
  --jobs 2
```

Some macOS SDKs do not have the AGL framework.
Qt 6.8.3 can still request `-framework AGL` in that case.
The bootstrap and the CMake configuration then remove only that link.

## Native analysis on your computer

Use the CMake presets and targets:

```sh
cmake --preset clang-tidy
cmake --build --preset clang-tidy --target choscordb-clang-tidy choscordb-header-check
cmake --preset sanitizers && cmake --build --preset sanitizers
ctest --preset sanitizers
cmake --preset coverage && cmake --build --preset coverage
cmake --build --preset coverage --target choscordb-cpp-coverage
cmake --preset iwyu && cmake --build --preset iwyu --target choscordb-iwyu
```

`CHOSCORDB_ENABLE_STRICT_WARNINGS` is on by default.
Strict compilation uses `/W4 /WX` on MSVC.
On GCC and Clang, it uses `-Wall -Wextra -Wpedantic -Werror`.
The clang-tidy profile in `.clang-tidy` uses analyzer, bugprone, performance and portability checks.
It also uses reviewed modernize and core-guideline checks.

## Scope

- Rust checks cover all workspace crates, targets and features.
- C++ checks cover the handwritten files in `desktop/` and `tests/`, including headers.
- Ruff covers the Python code in `scripts/` and `examples/`.

The checks exclude generated CXX and moc output, Qt, QScintilla, vendored sources, `build/` and `target/`.
External include paths are system includes, so their diagnostics do not fail first-party checks.

## Incremental bridge builds

Cargo writes the generated CXX headers to a staging directory.
`cmake/CxxHeaders.cmake` copies them to the published include directory with `copy_if_different`.
Thus Ninja recompiles the C++ consumers only when the bridge layout changes.
`scripts/ci/test_cxx_headers.py` tests a changed layout, a build with no change and a deleted header.

## Suppressions

Fix warnings and analyzer findings.
An exception must name one diagnostic at the narrowest location and give the reason.
Do not add broad `NOLINT` comments, directory exclusions, automatic retries or longer timeouts as a fix.
The workspace denies `unsafe` Rust code.
The `cxx::bridge` macro is the only exception, for the generated interface code.

## Dependency licenses

`deny.toml` accepts MIT, Apache-2.0, Unicode-3.0, Zlib and GPL-3.0-or-later for Cargo packages.
It also accepts some reviewed licenses for exact crates only:

- BSD-3-Clause for `subtle`, `ed25519-dalek` and `curve25519-dalek`.
- Apache-2.0 with the LLVM exception for `winx`.

If a new dependency has no accepted license, `cargo-deny` fails.
Do not expand the policy automatically.
The release notices must include the text of each reviewed license.

This check covers Cargo packages only.
It does not check Qt or QScintilla.
QScintilla 2.14.1 is GPL-3.0-only, and Qt has its own module licenses.
Both stay dynamically linked.
Read the [cargo-deny license documentation](https://embarkstudios.github.io/cargo-deny/checks/licenses/cfg.html) for the configuration format.

## Limits of local runs

A local pass does not prove that the GitHub jobs pass.
The `offscreen` tests check widget and model logic.
They do not test a real window manager, accessibility or packages.
Keep this difference clear in release notes and handoffs.
