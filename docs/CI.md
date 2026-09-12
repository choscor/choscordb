# Continuous integration

`.github/workflows/ci.yml` runs for every pull request, pushes to `main`/`master`, and manual dispatch. Each native matrix entry runs Rust format, strict Clippy, workspace tests, a release CMake/Ninja build, and all registered CTest tests. Building the desktop compiles the generated CXX translation unit and links the Rust static library into C++; this is the bridge compile check. A separate job applies the Cargo dependency license policy.

| Runner | Architecture | Qt package | Compiler |
|---|---|---|---|
| `ubuntu-24.04` | x64 | Qt 6.8.3 `linux_gcc_64` | GCC |
| `windows-2022` | x64 | Qt 6.8.3 `win64_msvc2022_64` | MSVC 2022 |
| `macos-15` | arm64 | Qt 6.8.3 `clang_64` universal package | Apple Clang |
| `macos-15-intel` | x64 | Qt 6.8.3 `clang_64` universal package | Apple Clang |

These explicit labels follow [GitHub's hosted-runner architecture table](https://docs.github.com/en/actions/reference/runners/github-hosted-runners). They avoid architecture changes behind `macos-latest`. The runner image supplies Rustup and the system compiler; Rust itself is installed from `rust-toolchain.toml`. The workflow uses Python 3.12, aqtinstall 3.3.0, CMake 3.31.6, Ninja 1.11.1.4, and cargo-deny 0.20.2. Python build-tool versions live in `scripts/ci/requirements.txt`. Transitive Python requirements are resolved by pip and are not fully locked.

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
python -m unittest discover -s scripts/ci -p 'test_*.py'
python scripts/ci/desktop.py rust
python scripts/ci/desktop.py dependencies
python scripts/ci/desktop.py build
python scripts/ci/desktop.py test
cargo install cargo-deny --version 0.20.2 --locked
cargo deny --locked check licenses
```

The desktop helper keeps Qt, QScintilla, and the native build beneath `build/ci`; it never reuses `build/dev`. Its compiler and test subprocesses receive explicit Qt runtime paths. Source checkouts and dependencies must be available over HTTPS on a first build. Action dependencies are pinned to full commits, `persist-credentials` is disabled, and workflow permissions are read-only. No publishing/signing secrets are used.

## License check scope

`deny.toml` accepts the license alternatives required by the inspected Cargo graph: MIT, Apache-2.0, Unicode-3.0, Zlib, and the workspace's GPL-3.0-or-later. A new dependency with no accepted alternative fails the check; the policy is not expanded automatically. The command and configuration follow [cargo-deny's license-check documentation](https://embarkstudios.github.io/cargo-deny/checks/licenses/cfg.html).

This check covers Cargo packages, including build and test dependencies. It does not discover native libraries obtained outside Cargo or certify distribution compliance. QScintilla 2.14.1's source header specifies GPL version 3 without an “or later” clause; its source is identified here as GPL-3.0-only. The combined distribution must account for that restriction even though ChoscorDB's own source is GPL-3.0-or-later. Qt has module-specific licensing and third-party notices described in [Qt 6.8's licensing documentation](https://doc.qt.io/qt-6.8/licensing.html). Qt and QScintilla remain dynamically linked. Packaging still needs license texts, source obligations, notices, and an SBOM checked for the actual shipped artifacts; this foundation makes no legal-review, installer, signing, or notarization claim.

## Verification evidence and remaining gates

Locally verified on macOS arm64: official source digest; aqt's three host architecture lists; Python script compilation; three archive-validation regression tests; actionlint 1.7.12; cargo-deny 0.20.2 (`licenses ok`); and a complete QScintilla 2.14.1 source build against installed Qt 6.11.2, including successful repeated bootstrap into `build/ci/qscintilla-local`.

The local bootstrap first exposed the macOS `DESTDIR` post-link failure; the corrected bootstrap then built and installed successfully. These checks do not prove that all GitHub matrix jobs pass. No workflow has been pushed or dispatched as part of this change. Windows, Linux, Intel macOS, and the exact Qt 6.8.3 native builds remain remotely unverified until the workflow runs. Installation smoke tests, release artifacts, performance measurements, and signed packages remain separate release gates from PRD §17.

The Linux Secret Service backend introduces `subtle` 2.6.1 through its cryptographic dependencies. Its packaged license was reviewed as BSD-3-Clause; `deny.toml` allows that exact crate/version. GNU lists the [modified BSD license as GPL-compatible](https://www.gnu.org/licenses/license-list.en.html#ModifiedBSD). Release artifacts still need the dependency's copyright, conditions, and disclaimer in third-party notices. This exception does not approve other unreviewed versions or licenses.

On macOS SDKs without AGL, Qt 6.8.3's qmake and CMake package metadata can retain an obsolete `-framework AGL` link. The QScintilla bootstrap removes that exact generated Makefile token when the selected SDK lacks AGL. Application configuration applies the same condition to Qt's imported WrapOpenGL target using the effective `CMAKE_OSX_SYSROOT`. Both paths remain unchanged when the selected SDK provides AGL.

The optional `CHOSCORDB_ENABLE_STAGING` CMake target invokes the release staging adapter with explicit Qt and QScintilla locations. The macOS adapter installs into a temporary prefix, deploys runtime frameworks and the Cocoa plugin, rewrites QScintilla references to bundle-relative paths, validates Mach-O dependencies, records payload hashes, relocates the stage, and smoke-tests that final location with isolated application data. Ordinary builds never stage, sign, or publish artifacts.

The CMake install boundary now includes the application's root license (inside the macOS bundle's Resources/licenses directory, or share/licenses/choscordb elsewhere). The Windows application target requests the GUI subsystem. A local macOS install into `build/staging-foundation` verified that the installed license exactly matches the source file. This staging tree still depends on development libraries: no relocatable-package or signed-distribution claim is made.
