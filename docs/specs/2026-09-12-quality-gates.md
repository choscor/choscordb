# ChoscorDB quality gates

- **Status:** Approved for implementation
- **Date:** 2026-09-12
- **Source context:** Repository-specific brainstorm and review of current CI, Rust/LLVM/CMake/GitHub guidance, Google's public C++ style and engineering practices, and Google's Tricorder/OSS-Fuzz material.

## Intended outcome

Establish strict, reproducible quality gates for ChoscorDB, a solo-maintained open-source Rust/C++ Qt application. The gates must catch platform-specific regressions, unsafe or suspicious code, style drift, test failures, dependency risks, and CI configuration errors while keeping the maintainer workflow lightweight. The repository should also be ready for future contributors without imposing review bureaucracy today.

The central design principle is Google's publicly documented low-noise analysis model: run actionable checks close to every change, prefer compiler-verifiable contracts, keep changes attributable, and do not copy organization-specific style rules that do not fit this project. Relevant sources include the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html), [Google small-change guidance](https://google.github.io/eng-practices/review/developer/small-cls.html), [Tricorder](https://research.google/pubs/tricorder-building-a-program-analysis-ecosystem/), [clang-tidy](https://clang.llvm.org/extra/clang-tidy/), the [Clang Static Analyzer](https://clang.llvm.org/docs/ClangStaticAnalyzer.html), [Clippy CI guidance](https://doc.rust-lang.org/clippy/continuous_integration/index.html), and [OSS-Fuzz](https://google.github.io/oss-fuzz/).

## Current state

- `.github/workflows/ci.yml` runs on pull requests, pushes to `main`/`master`, and manual dispatch.
- The native matrix is Ubuntu 24.04 x64, Windows 2022 x64, macOS 15 arm64, and macOS 15 Intel.
- `rust-toolchain.toml` pins Rust 1.97.1 with `rustfmt` and Clippy.
- `scripts/ci/desktop.py rust` runs `cargo fmt --check`, strict Clippy over all workspace targets, and workspace tests.
- The native matrix builds the C++23/Qt 6.8.3 application and runs 22 registered CTest executables.
- `.clang-format` exists. All 76 current hand-written C/C++ headers and sources pass local clang-format 23.1.0, but CI does not enforce it.
- Local Rust formatting, strict Clippy, and all non-ignored Rust tests passed during the brainstorm.
- Twenty-three Rust tests are ignored, principally live PostgreSQL and native credential-store tests.
- `deny.toml` and CI enforce Cargo licenses only. Other cargo-deny classes are not configured as gates.
- CI runs bootstrap tests under `scripts/ci` but omits the 37 passing unit tests under `scripts/release`.
- There is no explicit C++ compiler warning policy, warnings-as-errors, clang-tidy, path-sensitive static analysis, sanitizer job, coverage artifact, dependency vulnerability gate, or workflow lint gate.
- First-party Rust currently contains no `unsafe` blocks.
- `Cargo.toml` declares `rust-version = "1.88"`, but CI only exercises Rust 1.97.1. The 1.88 declaration is not an intended compatibility promise.

The first observed GitHub run of the existing workflow, [run 34686709688](https://github.com/choscor/choscordb/actions/runs/34686709688), was red:

- Ubuntu native/Rust and Cargo license jobs passed.
- Windows Clippy failed on a target-specific unreachable pattern in `crates/driver-postgres/tests/auxiliary_close.rs`.
- Both macOS runners failed `disconnect-workspace` and `history`; `workspace` timed out.

These failures are part of the baseline that must be diagnosed and repaired. A routinely red gate is not acceptable.

## Scope and requirements

### 1. Canonical developer interface

Add a repository-owned quality entry point, preferably `scripts/ci/quality.py`, with:

- `fast`: deterministic formatting, static checks that do not require Qt, all Python unit tests, and ordinary Rust tests;
- `full`: everything in `fast` plus a strict native build and CTest when Qt/QScintilla are available;
- focused stages so an individual failing gate can be reproduced without running the entire suite.

Commands must use the same flags and first-party file scope as CI, print the invoked subprocess commands, return nonzero on failure, and give an actionable message when a required tool or native dependency is unavailable. The implementation may reuse or delegate to `scripts/ci/desktop.py`; there must not be two divergent definitions of the same gate.

Pre-commit hooks may be documented but must remain optional.

### 2. Formatting and style

Required formatting checks:

- Rust: `cargo fmt --all -- --check` using the pinned Rust toolchain.
- C/C++: LLVM 23 `clang-format --dry-run --Werror` over hand-written files under `desktop/` and `tests/`, including headers.
- Python: a pinned Ruff release with both lint and format checks over repository-owned Python.

Exclude generated CXX/Qt output, dependencies, vendored sources, build trees, release artifacts, historical JSON evidence, and ordinary Markdown prose. Retain the existing LLVM-derived `.clang-format`, C++23, Qt idioms, and current exception behavior. Do not adopt Google naming, formatting, exception, or `cpplint` rules wholesale.

### 3. Rust static checks

Run on Ubuntu, Windows, macOS arm64, and macOS Intel:

```text
cargo check --workspace --all-targets --all-features --locked
cargo clippy --workspace --all-targets --all-features --locked -- -D warnings
cargo test --workspace --all-features --locked
```

Rust compiler warnings must be denied for the explicit check as well as for Clippy. Configure workspace lints so first-party `unsafe_code`, `dbg_macro`, `todo`, and `unimplemented` are denied by default. Unsafe code is not permanently forbidden: a future first-party exception must be narrowly scoped, documented with its safety invariant, and covered by focused tests.

Do not enable blanket Clippy `pedantic` or restriction groups and do not ban all `unwrap`, `expect`, or `panic` calls. Add individual high-signal lints only after the existing code either passes or has a narrow justified exception.

Rust 1.97.1 is the sole supported toolchain for this phase. Remove the unintended Rust 1.88 MSRV declaration rather than claiming untested compatibility.

Do not add Miri, `cargo-udeps`, or a separate `cargo-audit` gate initially. Miri and unstable unused-dependency analysis have low current value because first-party Rust has no unsafe code; cargo-deny advisories cover the RustSec vulnerability seam.

### 4. C++ compiler and static checks

All first-party C++ production and test targets must compile warning-free with:

- MSVC: `/W4 /WX`;
- GCC and Clang: `-Wall -Wextra -Wpedantic -Werror`.

Evaluate additional high-signal warnings such as shadowing, non-virtual destructor, and suspicious conversions against the baseline before enabling them. Do not enable every conversion warning merely to maximize lint count. Apply warning policy only to first-party code. Treat Qt, QScintilla, generated CXX sources, and other dependencies as system/external code so their diagnostics do not become project failures.

Pin LLVM major version 23 consistently for clang-format, clang-tidy, analyzer checks, sanitizer builds, and C++ coverage where applicable. Patch-level updates within the major are allowed when output remains reproducible.

Run clang-tidy once on Ubuntu over hand-written first-party C/C++ using a generated compilation database. The required profile is:

- `clang-analyzer-*`;
- `bugprone-*`;
- `performance-*`;
- `portability-*`;
- a reviewed subset of `cppcoreguidelines-*` and `modernize-*`.

Do not wholesale-enable readability/naming checks. Treat enabled clang-tidy findings as errors after baseline cleanup. Suppress only the specific diagnostic at the narrowest practical location and include a reason. Broad `NOLINT`, directory exclusions, or globally disabled warnings used only to make CI green are forbidden.

First-party headers must be self-contained and include their direct dependencies. Provide a compiler-backed header self-containment check or equivalent public stage. Run Include-What-You-Use in advisory mode until explicit Qt, QScintilla, MOC, and generated-CXX mappings/exclusions are reliable; it is not initially merge-blocking.

Do not add `cpplint`, Cppcheck, a duplicate standalone `scan-build` job, or a commercial analyzer. The selected compiler/clang-tidy/Clang Static Analyzer/CodeQL stack is the canonical static-analysis stack.

### 5. Tests and integration behavior

- Run all ordinary Rust workspace tests, including doc tests reached by Cargo.
- Run all Python unit tests in `scripts/ci` and `scripts/release`.
- Strictly build and run all registered CTest tests on all four supported runners with `QT_QPA_PLATFORM=offscreen` and existing runtime paths.
- Restore the current Windows and macOS gates to green before treating the expanded system as complete.
- Add a dedicated Linux PostgreSQL 17 integration job using the repository-owned fixture contract in `docs/testing/postgres.md` and `scripts/integration/postgres_fixture.py`.
- Run fixture-backed tests sequentially where required, including ignored driver/core suites and native CTest coverage. Preserve TLS, cancellation, restart, and exclusive-fixture behavior.
- Keep the native OS credential-store round-trip test manual because unattended CI keychains and prompt behavior are unreliable.

Required tests must not be automatically retried. A flaky test must be repaired or temporarily removed from the required set with a narrow documented reason and tracking issue. Do not hide product races by increasing timeouts alone. Preserve full failure logs and keep matrix `fail-fast: false` so cross-platform evidence is retained.

### 6. Dynamic analysis and stress checks

Add a Linux Clang ASan+UBSan configuration for first-party native C++ and CTest. Linked Rust or third-party libraries that are not instrumented must not be described as sanitizer-covered. Run the sanitizer job on a weekly schedule and manual dispatch initially. After it is stable, the latest default-branch sanitizer result becomes a documented release requirement.

Add a scheduled repeat/stress job for timing-sensitive Qt tests. Repetition is for detection, not automatic retry of a failed required test. Do not add ThreadSanitizer initially because Qt, FFI, and existing threaded tests are likely to produce disproportionate noise and maintenance cost.

### 7. Coverage

Generate separate Rust and C++ LCOV reports using pinned tools and upload them as GitHub artifacts. Validate that each artifact is syntactically readable and contains first-party source records. Do not merge them into a misleading single percentage.

Coverage generation itself must fail if broken, but no numeric line/branch threshold is imposed initially. Record a trustworthy baseline before separately ratcheting Rust and C++ coverage. Do not write superficial tests merely to increase a metric.

### 8. Dependency and security gates

Expand `deny.toml` and CI from license-only checking to the full locked Cargo graph:

- advisories, including vulnerable/yanked/unmaintained policy with explicit exceptions;
- bans/duplicates policy based on reviewed impact rather than blindly rejecting every duplicate;
- existing license policy;
- allowed registry and Git source policy.

Use one full `cargo deny --locked check`; do not duplicate its advisory work with cargo-audit.

Add GitHub dependency review for pull requests and advanced CodeQL analysis for Rust, C/C++, Python, and GitHub Actions. Pin actions to full commits and preserve least-privilege permissions. Baseline existing findings first; then new analysis errors and high/critical security findings are blocking. Any accepted finding requires narrow documented rationale, not blanket exclusion.

Add weekly grouped Dependabot updates for Cargo, pip requirements, and GitHub Actions. Dependabot creates reviewable PRs; it does not auto-merge.

### 9. CI layout and enforcement

Keep pull-request, `main`/`master` push, and manual triggers. Full supported-platform checks run on every pull request and default-branch push because current CI already exposed platform-specific defects. Avoid redundant full native builds solely to separate job labels; reuse a runner's installed dependencies/build where doing so does not obscure diagnostics.

Provide an early deterministic quality job, followed by or parallel with the platform matrix and specialized jobs. Use concurrency cancellation for superseded runs. Keep explicit timeouts, read-only defaults, pinned actions/tools, and disabled persisted checkout credentials.

Recommended enforcement layers:

1. **Immediately required once green:** formatting, Ruff, actionlint, Python tests, Rust check/Clippy/tests, strict native builds, CTest, PostgreSQL integration, full cargo-deny, and dependency review.
2. **Baseline then required:** clang-tidy/analyzer profile and CodeQL severity policy.
3. **Non-threshold reporting:** Rust and C++ coverage artifacts.
4. **Scheduled/manual then release gate:** ASan+UBSan and timing-stress runs.
5. **Initially advisory:** Include-What-You-Use.

Document optional GitHub branch protection/rulesets requiring the stable green checks but no approving review while the repository has one contributor. Repository settings are an owner-applied rollout step; committed workflow files cannot claim they changed those settings.

### 10. Documentation

Update `README.md` and `docs/CI.md`, and add a concise `CONTRIBUTING.md`. Documentation must cover:

- the canonical fast/full and focused local commands;
- prerequisite and pinned tool versions;
- exact first-party scope and exclusions;
- CI jobs, schedules, and which results block;
- PostgreSQL integration reproduction;
- coverage artifact interpretation;
- suppression/exception policy;
- the optional branch-protection setup;
- the fact that offscreen Qt tests are not real window-manager, accessibility, installer, or packaging tests.

## Non-goals

- Unrelated product refactoring or broad cleanup.
- Adopting Google's complete C++ style or disabling exceptions.
- Mandatory reviews, external reporting accounts, or required pre-commit hooks.
- A combined or arbitrary coverage percentage.
- Automatically testing the native credential store.
- Miri, ThreadSanitizer, blanket Clippy pedantic lints, Cppcheck, cpplint, standalone scan-build, or commercial analyzers in this phase.
- Fuzzing in this implementation. A later quality phase should evaluate Rust SQL parsing, result-store decoding, and protocol/value conversion as initial fuzz seams; Qt widgets are not the first target.
- Claiming packaging, signing, real-display UI, accessibility, or performance verification from these gates.

## Failure behavior

- Missing tools fail locally with installation guidance; CI installs pinned tools and fails if installation or integrity validation fails.
- A formatting, compiler, analyzer, test, security, or artifact-generation error returns a nonzero result and identifies the smallest useful stage/file/test.
- External/generated diagnostics do not fail first-party warning gates unless the project directly controls the offending code.
- Scheduled sanitizer or stress failures remain visible and invalidate the documented release prerequisite; they are not silently converted to success.
- A first remote workflow run may reveal host-only defects. Completion requires either a green actual run or an explicit handoff stating that remote execution awaits a push; local validation must not be presented as remote success.

## Acceptance criteria and test seams

| Acceptance criterion | Public test seam |
|---|---|
| A maintainer can reproduce required checks without reading workflow YAML. | Run the documented `fast`, `full`, and focused quality commands from the repository root and inspect their exit status/output. |
| Rust format and analysis cover every workspace target/feature on every supported OS. | Observe the exact Cargo commands and green Rust stages in all four matrix jobs; a target-specific warning must make its job fail. |
| First-party Rust remains safe-by-default and free of debug placeholders. | Workspace lint configuration plus `cargo check`/Clippy; a fixture or temporary first-party violation is rejected by the focused lint stage. |
| C++ formatting is deterministic. | LLVM 23 clang-format check over the enumerated first-party file list; a temporary formatting violation produces nonzero status without rewriting files. |
| First-party C++ is warning-free on all supported compilers. | Strict CMake build on GCC, MSVC, and both Apple Clang runners; a controlled warning fixture or compiler option check demonstrates warnings are errors. |
| Modern C++ static analysis is low-noise and blocking. | Ubuntu clang-tidy stage uses the compilation database, prints the enabled checks, excludes external/generated code, and exits cleanly with zero warnings. |
| Headers are self-contained. | Public header-check stage compiles each first-party header as the first/only project include with the normal include environment. |
| Ordinary automated tests are complete. | `cargo test`, both Python unittest discoveries, and all 22 registered CTest executables return success. |
| Live PostgreSQL behavior is exercised reproducibly. | Dedicated Linux PostgreSQL 17 job starts the owned fixture, runs the documented ignored/native suites sequentially, and stops or safely recovers the fixture. |
| Existing platform defects are not normalized as expected failures. | Windows Clippy and the failing/timing-out macOS CTest cases pass without broad lint suppression, retries, or blanket test exclusion. |
| Sanitizer and timing regressions remain observable. | Scheduled/manual workflows run ASan+UBSan and repeat/stress stages, retain logs, and fail on findings. |
| Coverage generation is trustworthy but not gamed. | Separate downloadable, parseable Rust and C++ LCOV artifacts contain first-party records; no numeric threshold is configured. |
| Cargo dependency policy covers more than licensing. | `cargo deny --locked check` reports and enforces advisories, bans, licenses, and sources. |
| GitHub-native security analysis covers the repository languages. | Dependency-review and CodeQL results appear on pull requests/security views with the approved severity policy and no blanket exclusions. |
| CI definitions are valid and supply-chain-conscious. | Pinned actionlint passes; workflow actions are full-commit pinned, checkout credentials are not persisted, and permissions are least privilege per job. |
| Solo maintenance remains lightweight. | No required pre-commit hook or approving review is needed; optional branch protection only requires stable automated checks. |

## Likely affected areas

- `.github/workflows/ci.yml` and likely additional specialized workflow files.
- `.github/dependabot.yml`.
- `Cargo.toml`, crate manifests inheriting workspace lints, `rust-toolchain.toml`, and `deny.toml`.
- `.clang-format` only if LLVM 23 compatibility requires an explicit setting; add a `.clang-tidy` configuration.
- `CMakeLists.txt`, `CMakePresets.json`, and focused CMake modules/options for warnings, analyzer integration, sanitizer, coverage, and header checks.
- `scripts/ci/desktop.py`, a canonical quality entry point, its unit tests, and PostgreSQL fixture orchestration.
- Existing Rust/C++ source or tests only where required to establish a clean, non-suppressed baseline.
- `README.md`, `docs/CI.md`, `docs/testing/postgres.md`, and new `CONTRIBUTING.md`.

Preserve unrelated user changes in the working tree. Use repository scripts/configuration rather than embedding large divergent shell sequences in workflow YAML.

## Rollout, compatibility, and observability

- Phase 0 repairs the currently red platform baseline.
- Deterministic checks become required only when green on their supported seam.
- CodeQL and clang-tidy findings are reviewed before their final blocking policy is enabled.
- Coverage begins as artifact-only evidence and may be ratcheted in a future explicit decision.
- IWYU remains advisory until Qt/MOC/CXX mappings are proven reliable.
- Sanitizer/stress workflows begin scheduled/manual and become a release prerequisite after stable operation.
- Retain current supported OS/architecture/compiler coverage and Qt 6.8.3 behavior.
- CI logs and named stages are the primary observability surface; retain failure output and uploaded reports long enough to diagnose regressions.

## Risks and assumptions

- LLVM 23 installation and Qt compile-command compatibility must be validated on Ubuntu; keep one pinned major across the LLVM-based gates.
- Strong C++ warnings and clang-tidy may expose legitimate baseline defects. Fix those defects or use narrow documented exceptions; do not weaken the global policy.
- Instrumented native builds do not automatically instrument linked Rust or third-party binaries.
- CodeQL C/C++ and Rust extraction may require custom build orchestration because this project downloads Qt and builds QScintilla.
- The PostgreSQL restart suite needs exclusive control of its repository-owned fixture and cannot safely be parallelized with other fixture users.
- macOS offscreen Qt timing failures may represent product races rather than CI slowness. Diagnose observable behavior before changing timeouts.
- GitHub dependency review, code-scanning enforcement, and branch rules can depend on repository settings; document any owner action that committed files cannot perform.
- Tool patch versions and advisory databases evolve. Pin executable/action versions while allowing reviewed updates through Dependabot or explicit maintenance.

No product behavior migration or user-data migration is expected. CI job names may change, so branch protection should be applied only after stable final names are known.

## Fresh-session implementation instruction

Start a fresh session, read this entire specification, inspect the current workspace and working-tree state, then invoke `$implement` with this exact spec path. Use test-driven development for scripts/configuration behavior, preserve unrelated changes, diagnose the existing platform failures from evidence, and independently review the completed gate implementation before handoff.

```text
Use $implement with docs/specs/2026-09-12-quality-gates.md.
```
