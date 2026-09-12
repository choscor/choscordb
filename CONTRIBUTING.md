# Contributing to ChoscorDB

ChoscorDB accepts focused issues and pull requests. Keep changes small enough to
explain and verify independently, include behavior-focused tests, and avoid
unrelated cleanup. One approving review is not required while the project has a
single maintainer; the automated quality gates are the required review surface.

## Before submitting a change

Install Python 3.12+, the Rust toolchain pinned by `rust-toolchain.toml`, LLVM 23,
actionlint 1.7.7, cargo-deny 0.20.2, and the dependencies in
`scripts/ci/requirements.txt`. The exact missing-tool message includes the
installation command. Run:

```sh
python scripts/ci/quality.py fast
```

For C++ or desktop changes, provision Qt 6.8.3 and QScintilla 2.14.1 once and run
the full suite:

```sh
python scripts/ci/quality.py native-dependencies
python scripts/ci/quality.py full
```

Focused commands and platform-specific prerequisites are documented in
`docs/CI.md`. Pre-commit hooks are optional; the checked-in commands and CI are
canonical.

## Exceptions and suppressions

Fix first-party warnings and analyzer findings when practical. A suppression
must name only the specific diagnostic at the narrowest useful location and
state why the code is correct. Broad `NOLINT`, generated-directory exclusions,
blanket lint allowances, test retries, and timeout-only fixes are not accepted.
Any handwritten unsafe Rust exception must document its safety invariant and
have a focused test. The generated `cxx::bridge` boundary is the current sole
scoped unsafe-code exception.

Never weaken a test to hide a race. Required tests run once. If a gate is
temporarily removed, document the precise reason and tracking issue.

## Scope of desktop evidence

CTest uses `QT_QPA_PLATFORM=offscreen`. It verifies widget/model behavior, not a
real window manager, accessibility integration, installers, packaging, signing,
or end-user performance. Those require separate release evidence.
