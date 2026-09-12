# Continuous integration

The checked-in workflows run on pull requests, pushes to `main`/`master`, and
manual dispatch. Superseded runs are cancelled, matrix fail-fast is disabled,
jobs have explicit timeouts, checkout credentials are not persisted, and
permissions default to read-only.

The merge-blocking layer is deterministic quality (format, Ruff, actionlint and
both Python suites), the four-platform Rust/native matrix, PostgreSQL 17
integration, full cargo-deny policy, and pull-request dependency review.
Clang-tidy and CodeQL are introduced as baseline-first gates: existing findings
must be reviewed before their analysis policy is made required. The initial
clang-tidy workflow step is therefore explicitly nonblocking while still
reporting all findings as errors inside the stage. Rust and C++
coverage generate separate artifact-only reports with no numeric threshold.
ASan+UBSan and timing-stress checks run weekly and manually; after stable runs,
the latest successful default-branch result is a release prerequisite. IWYU is
advisory.

| Runner | Architecture | Qt package | Compiler |
|---|---|---|---|
| `ubuntu-24.04` | x64 | Qt 6.8.3 `linux_gcc_64` | GCC |
| `windows-2022` | x64 | Qt 6.8.3 `win64_msvc2022_64` | MSVC 2022 |
| `macos-15` | arm64 | Qt 6.8.3 `clang_64` universal package | Apple Clang |
| `macos-15-intel` | x64 | Qt 6.8.3 `clang_64` universal package | Apple Clang |

These labels avoid architecture changes behind `macos-latest`. Rust 1.97.1 is
the sole supported Rust toolchain for this phase. CI uses Python 3.12, Qt 6.8.3,
QScintilla 2.14.1, LLVM major 23, Ruff 0.16.7, aqtinstall 3.3.0, CMake 3.31.6,
Ninja 1.11.1.4, actionlint 1.7.7, and cargo-deny 0.20.2. Patch updates to LLVM
23 are allowed after their output is reviewed. Coverage uses cargo-llvm-cov
0.6.21 and lcov2xml 1.0.9. Python tool pins live in
`scripts/ci/requirements.txt`.

Qt installation uses the project's [documented aqt CLI](https://aqtinstall.readthedocs.io/en/v3.3.0/cli.html). Architecture names were checked against live `aqt list-qt ... --arch 6.8.3` metadata for all three hosts. macOS QScintilla is compiled only for the runner's native architecture. `MACOSX_DEPLOYMENT_TARGET=13.0` is shared by Rust's native dependencies, qmake, and CMake in CI. This is a build setting, not evidence of an installation smoke test on macOS 13.

Windows enters the [MSVC developer environment](https://github.com/ilammy/msvc-dev-cmd) before invoking Rust, qmake/nmake, or CMake/Ninja. Windows steps use the default PowerShell shell, avoiding the GNU `link.exe` collision associated with a Bash shell. Qt's DLL directory and QScintilla's DLL directory are added to the subprocess PATH during tests. Linux installs Qt's X11/font/OpenGL runtime dependencies and D-Bus development headers for the native Secret Service credential adapter; tests use `QT_QPA_PLATFORM=offscreen` on every OS. This exercises widget/model logic, not a real window-manager accessibility or packaging test.

## QScintilla bootstrap

`scripts/ci/bootstrap_qscintilla.py` downloads the official [Riverbank source distribution](https://www.riverbankcomputing.com/software/qscintilla/download), verifies it before extraction, rejects archive links/devices/path traversal, invokes the installed Qt's qmake, and builds a shared library. Headers, libraries, Windows DLLs, source provenance, and the upstream license are copied into an isolated prefix. No system Qt installation is modified.

- Source: `https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz`
- Version: `2.14.1`
- SHA-256: `dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d`

The digest was computed from the downloaded official 3,233,610-byte archive and cross-checked against [Homebrew's QScintilla source manifest](https://github.com/Homebrew/homebrew-core/blob/main/Formula/q/qscintilla2.rb). The Python-binding sdist on PyPI has a different filename and checksum and is not substituted automatically.

The bootstrap intentionally keeps qmake's default build output directory. Upstream's macOS post-link `install_name_tool` command addresses the library by basename and fails if `DESTDIR` redirects it. Copying completed artifacts into the isolated prefix avoids that failure. The bootstrap preserves shared-library symlinks and can be rerun with the same verified archive.

For local use with an existing Qt 6.8+ installation:

```sh
python3 scripts/ci/bootstrap_qscintilla.py \
  --qmake /path/to/qt/bin/qmake \
  --prefix build/ci/qscintilla-local \
  --work build/ci/qscintilla-local-source \
  --jobs 2
```

Use `--archive /path/to/QScintilla_src-2.14.1.tar.gz` for a previously downloaded source archive; checksum verification still runs. On Windows, invoke the script from an x64 MSVC developer shell and use `qmake.exe`.

## Reproducing the jobs

Install Python 3.12+, Rustup, the native C++ toolchain, and the Linux packages listed in the workflow when applicable. Use a Python virtual environment for local installs.

```sh
python -m pip install -r scripts/ci/requirements.txt
python scripts/ci/quality.py fast
python scripts/ci/quality.py native-dependencies
python scripts/ci/quality.py full
cargo install cargo-deny --version 0.20.2 --locked
cargo deny --locked check
```

The focused deterministic stages are `cpp-format`, `python-lint`,
`python-format`, `actionlint`, `python-tests`, `rust-format`, `rust-check`,
`rust-clippy`, `rust-tests`, and `cargo-deny`. Native stages are
`native-dependencies`, `native-build`, and `native-tests`. Every command prints
the subprocess it invokes and returns
nonzero with installation guidance when a required tool or dependency is
missing.

Native analysis can be reproduced with the named CMake presets and targets:

```sh
cmake --preset clang-tidy
cmake --build --preset clang-tidy --target choscordb-clang-tidy choscordb-header-check
cmake --preset sanitizers && cmake --build --preset sanitizers
ctest --preset sanitizers
cmake --preset coverage && cmake --build --preset coverage
cmake --build --preset coverage --target choscordb-cpp-coverage
cmake --preset iwyu && cmake --build --preset iwyu --target choscordb-iwyu
```

LLVM stages reject any major other than 23. Sanitizers instrument first-party
C++ only; linked Rust, Qt, QScintilla, and other third-party libraries are not
claimed as sanitizer-covered.

The desktop helper keeps Qt, QScintilla, and the native build beneath `build/ci`; it never reuses `build/dev`. Its compiler and test subprocesses receive explicit Qt runtime paths. Source checkouts and dependencies must be available over HTTPS on a first build. Action dependencies are pinned to full commits, `persist-credentials` is disabled, and workflow permissions are read-only. No publishing/signing secrets are used.

## First-party scope and exclusions

Rust checks cover every workspace crate, target, and feature. C/C++ formatting,
warnings, clang-tidy, headers, sanitizers, and coverage cover handwritten files
under `desktop/` and `tests/`, including headers. Ruff covers repository-owned
Python under `scripts/`. Generated CXX and Qt/MOC output, Qt, QScintilla,
vendored/dependency sources, `build/`, `target/`, release artifacts, historical
JSON evidence, Markdown prose, and other generated trees are excluded. External
include paths are treated as system includes so dependency diagnostics do not
become first-party failures.

Strict native compilation uses `/W4 /WX` on MSVC and `-Wall -Wextra -Wpedantic
-Werror` on GCC/Clang. The blocking clang-tidy profile is limited to analyzer,
bugprone, performance, portability, and reviewed modernize/core-guideline
checks; naming/readability policy is not imported wholesale.

## PostgreSQL integration

The Linux integration job installs PostgreSQL 17, starts the repository-owned
fixture, exports its TLS/SCRAM environment, and runs the ignored driver and core
suites plus native CTest sequentially. The restart suite has exclusive fixture
control. It preserves TLS, cancellation, restart, and cleanup evidence and
always attempts to stop the marked cluster. Reproduce it exactly using
[`docs/testing/postgres.md`](testing/postgres.md). The native credential-store
round trip remains manual because unattended OS keychains may prompt or be
unavailable.

## Coverage artifacts

`coverage.yml` uploads separate LCOV artifacts for Rust and first-party C++.
Each job parses the LCOV and rejects an empty report or one without first-party
`SF:` records. Do not combine their percentages: the toolchains, source sets,
and instrumentation boundaries differ. Coverage generation failing is a gate;
coverage percentage is not. A later decision may ratchet separate baselines.

## Suppressions and accepted findings

Warnings and analyzer findings should be fixed. Any exception must identify a
single diagnostic at the narrowest location and explain the invariant. Broad
`NOLINT`, blanket directory exclusions, automatic retries, and arbitrary
timeout increases are forbidden. Handwritten Rust unsafe code is denied; a
future exception needs a documented safety invariant and focused tests. The
current `cxx::bridge` macro has the sole narrow exception for generated ABI
glue, whose layouts/signatures are compiler-checked and transport-tested.

Cargo-deny checks advisories, bans, licenses, registries, and Git sources over
the locked graph. Exceptions are exact and documented. Dependabot proposes
weekly grouped Cargo, pip, and GitHub Actions updates; it never auto-merges.

## Optional repository rules

After the first remote runs establish stable names, the owner may configure a
GitHub ruleset for `main`/`master` requiring the green deterministic-quality
(including Cargo policy), four platform, PostgreSQL integration, and dependency-review
checks. Do not require an approving review while there is one contributor.
Repository settings are owner-applied; workflow files do not claim to change
them.

## License check scope

`deny.toml` accepts the license alternatives required by the inspected Cargo graph: MIT, Apache-2.0, Unicode-3.0, Zlib, and the workspace's GPL-3.0-or-later. A new dependency with no accepted alternative fails the check; the policy is not expanded automatically. The command and configuration follow [cargo-deny's license-check documentation](https://embarkstudios.github.io/cargo-deny/checks/licenses/cfg.html).

This check covers Cargo packages, including build and test dependencies. It does not discover native libraries obtained outside Cargo or certify distribution compliance. QScintilla 2.14.1's source header specifies GPL version 3 without an “or later” clause; its source is identified here as GPL-3.0-only. The combined distribution must account for that restriction even though ChoscorDB's own source is GPL-3.0-or-later. Qt has module-specific licensing and third-party notices described in [Qt 6.8's licensing documentation](https://doc.qt.io/qt-6.8/licensing.html). Qt and QScintilla remain dynamically linked. Packaging still needs license texts, source obligations, notices, and an SBOM checked for the actual shipped artifacts; this foundation makes no legal-review, installer, signing, or notarization claim.

## Verification boundaries

Local success does not prove that GitHub's Windows, Linux, Intel macOS, arm64
macOS, PostgreSQL, CodeQL, coverage, or scheduled jobs passed. Until these files
are pushed, remote execution awaits a maintainer run. Preserve that distinction
in release notes and handoffs.

The Linux Secret Service backend introduces `subtle` 2.6.1 through its cryptographic dependencies. Its packaged license was reviewed as BSD-3-Clause; `deny.toml` allows that exact crate/version. GNU lists the [modified BSD license as GPL-compatible](https://www.gnu.org/licenses/license-list.en.html#ModifiedBSD). Release artifacts still need the dependency's copyright, conditions, and disclaimer in third-party notices. This exception does not approve other unreviewed versions or licenses.

On macOS SDKs without AGL, Qt 6.8.3's qmake and CMake package metadata can retain an obsolete `-framework AGL` link. The QScintilla bootstrap removes that exact generated Makefile token when the selected SDK lacks AGL. Application configuration applies the same condition to Qt's imported WrapOpenGL target using the effective `CMAKE_OSX_SYSROOT`. Both paths remain unchanged when the selected SDK provides AGL.

The optional `CHOSCORDB_ENABLE_STAGING` CMake target invokes the release staging adapter with explicit Qt and QScintilla locations. The macOS adapter installs into a temporary prefix, deploys runtime frameworks and the Cocoa plugin, rewrites QScintilla references to bundle-relative paths, validates Mach-O dependencies, records payload hashes, relocates the stage, and smoke-tests that final location with isolated application data. Ordinary builds never stage, sign, or publish artifacts.

The CMake install boundary now includes the application's root license (inside the macOS bundle's Resources/licenses directory, or share/licenses/choscordb elsewhere). The Windows application target requests the GUI subsystem. A local macOS install into `build/staging-foundation` verified that the installed license exactly matches the source file. This staging tree still depends on development libraries: no relocatable-package or signed-distribution claim is made.
