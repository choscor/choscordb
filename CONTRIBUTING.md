# Contributing to ChoscorDB

Anyone is welcome to create an issue to report a bug or suggest a change.
ChoscorDB does not accept external pull requests. The maintainer implements
changes and opens any pull requests needed for the project.

## Maintainer pull request title and description

Keep changes small enough to explain and verify independently, include
behavior-focused tests, and avoid unrelated cleanup. One approving review is not
required while the project has a single maintainer; the automated quality gates
are the required review surface.

Use a specific, action-oriented title. The preferred format is
`<type>(<scope>): <imperative summary>`, matching the project's Conventional
Commit subjects, for example `fix(desktop): preserve SQL editor focus after reconnect`.
The title should make sense when skimmed in a PR list or release history.

The [PR template](.github/pull_request_template.md) prompts for the reason,
meaningful changes, verification, and any context a reviewer cannot see in the
diff. State exact checks run and their outcomes; explain relevant checks that
were blocked or not run. Add screenshots for visible UI changes when useful, and
call out compatibility or migration effects. Link an issue if one exists; use
`Closes #N` only when the PR fully resolves it. Delete unused template sections
for small changes. Revisit the description if the change evolves during review.

## Before submitting a maintainer change

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

## C++ file size

Keep handwritten sources and headers under `desktop/` and `tests/` at or below
1,000 physical lines, including comments and blank lines. The `cpp-size` stage
runs in both `fast` and `full`, and CTest exposes it as `cpp-source-size` when
Python is available. Run it directly with:

```sh
python scripts/ci/quality.py cpp-size
```

Split by responsibility before reaching the limit. Keep related operations
together, keep private implementation details out of public headers, and register
new translation units in CMake. Prefer smaller functions with one logical task;
moving a large function to another file alone does not simplify it. Do not meet
the limit by compressing formatting, removing useful comments, or including
implementation `.cpp` files. See [the C++ size audit](docs/cpp-file-size.md) for
the initial splits and tool choices.

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
