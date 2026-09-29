# C++ ownership gate

Rust owns application behavior. C++ owns presentation, native UI integration,
transient view state, and typed bridge conversion, as defined in `AGENTS.md`.

## Run it

```sh
python3 scripts/ci/quality.py cpp-ownership
python3 scripts/ci/cpp_ownership.py --json > ownership.json
python3 -m unittest discover -s scripts/ci -p 'test_cpp_ownership.py'
```

`quality.py fast` and `full` include the gate, so the existing required quality
job runs it. Exit codes: **0** means no outstanding checks, **1** means findings
or inventory/exception errors, **2** means the audit could not run. JSON output
retains the same exit status. Do not hide that status with a shell pipeline.

## What the report measures

The report contains sorted paths, ownership roles, physical line counts, SHA-256
file digests (CRLF normalized to LF), rule findings with source lines, exceptions, and summary counts.
It has a schema version and contains no timestamps or machine-specific paths.
Identical source content produces identical JSON across LF/CRLF checkouts.

Discovery includes tracked and untracked non-ignored Git files throughout the
repository. It also includes ignored native files inside `desktop/` and `tests/`
so a new ignore pattern cannot hide those sources. C, C++, Objective-C, headers,
inline/template fragments, and C++ modules are included. Platform `#if` branches
are all scanned without requiring a compiler or the host platform's SDK.
CMake files are checked for known backend library dependencies.

Every discovered native path appears in the census. New first-party locations
fail until their role is explicitly classified in `ROLES`. Tests are scanned and
reported separately; their fixture findings do not block. Developer tools and
the bridge receive the production rules. Vendored native files appear as
unscanned third-party entries. Generated files under ignored build directories
are outside this source audit. Source symlinks fail instead of being silently
followed outside the checkout.

**File/line counts measure scan coverage, not the percentage of code that obeys
the architecture.** There is no meaningful automatic "presentation percentage."
A clean report means only that the implemented checks have no unresolved findings.

## Enforced checks

| Rule | What it flags |
| --- | --- |
| `storage` | Qt settings and file access, C++ file streams and filesystem APIs |
| `network` | Qt sockets/networking, curl and selected native networking APIs |
| `database` | Qt SQL and common SQLite, PostgreSQL, MySQL and ODBC APIs |
| `process`, `native-io` | Process execution and selected native file/socket APIs |
| `backend-include`, `backend-link` | Known backend headers and CMake dependencies |
| `excluded-dependency` | Explicit production includes of tests or vendor sources |
| `filesystem-query` | Filesystem existence and directory enumeration/mutation signals |
| `serialization` | JSON/XML/data serialization and cryptographic APIs |
| `domain-parsing` | Regular expression and version parsing APIs |
| `sql-text` | SQL statement literals, including adjacent literals |

`error` findings identify capabilities normally owned by Rust. `review` findings
identify ambiguous code, such as syntax highlighting, JSON display/transport,
example SQL, or domain parsing. **Both block until resolved.** A review finding
is not a confirmed violation. Findings are deduplicated by path, rule and line;
they are neither function counts nor counts of distinct architectural problems.
Comments and string contents are masked for capability checks. SQL literals are
examined separately. Escapes, raw strings and physical line continuations have
regression coverage. Ordinary Qt signal connections and native file dialogs are
allowed.

## Resolve findings

Move backend behavior to Rust and retain UI orchestration and conversion in C++.
Remove unused backend includes. For an actual presentation use, review the whole
file for that capability, then add an entry to
`scripts/ci/cpp_ownership_exceptions.json` with:

- `path`: one exact repository-relative file, without globs;
- `rule`: one rule that currently produces findings in that file;
- `sha256`: SHA-256 of the current file bytes with CRLF normalized to LF;
- `reason`: the presentation contract and why the capability belongs in C++.

Exceptions remain visible in JSON. Changed content (apart from LF/CRLF conversion), deleted files, disappeared
findings, duplicate entries and empty reasons fail. An exception for one rule
cannot suppress another. There is no baseline generation or accept-all command.
Do not refresh hashes without reviewing the changed capability. Hashes provide
change detection, not evidence that a person approved an exception.

Initial exceptions cover compiled style/icon resources, SVG validation and
syntax highlighting. The root CMake exception covers the test-only Qt Network
link; any edit to that file requires reviewing that exception again.

## Current assessment

The initial audit of the working tree on 2026-09-30 scanned **351 native files**:
264 under `desktop/` and 87 under `tests/`, plus 12 CMake files. After six narrow
exceptions, **35 findings remain blocking**. Run the tool for current counts.
That initial gate failed; existing code was not automatically grandfathered.

The six storage findings have since been resolved. Rust now reads and migrates
legacy updater preferences, writes preview PNG files, and serializes and writes
capture metadata. Qt retains rendering, in-memory PNG encoding, control geometry,
and worker dispatch. The unused preview storage include was removed. The storage
regression test rejects reintroducing these C++ capabilities.

The follow-up review resolved the filesystem probes through a Rust document-path
service executed on Qt workers. Object label/identity interpretation and proxy
credential requirements now use Rust policy. ER graphs cross CXX as typed nested
DTOs; the frontend only converts them to Qt display structures.

The remaining review matches have narrow content-hashed presentation exceptions:
ephemeral quick-search row keys, Qt-native shortcut feedback, synthetic gallery
SQL, SSH form hydration, local-bind warning hints, and inspection-row selection.
Rust remains authoritative for persistence, profile/SSH validation, inspection,
and approval. The current gate has **zero blocking findings and zero inventory
errors**. These exceptions are reviewed against the current contents, not a
baseline exemption for future code.

## Limits and maintaining the gate

This is a dependency-free lexical gate, not a C++ compiler, call-graph analyzer,
or proof of architectural correctness. It cannot infer the purpose of arbitrary
arithmetic, loops, validation or data transformations. It does not establish that
Rust revalidates UI input or that a bridge call runs off the UI thread. Macro token
pasting, encoded strings, indirect calls, unfamiliar APIs, transitive third-party
wrappers, dynamic CMake expressions and generated sources can evade these rules.
Ambiguous unqualified names such as `open` and `connect` are not banned because
they are also normal Qt UI methods. The CMake scan is lexical and does not resolve
target dependency graphs; test-only matches need reviewed exceptions.

Review changes to discovery, rules, exceptions, `.gitignore`, build configuration
and generated-source inputs as architecture changes. For each new backend API or
missed pattern, add a failing fixture, implement its rule and test a legitimate
UI counterpart. Keep semantic review of changed C++ functions: does this choose
SQL, validate domain data, persist or interpret application data, make network
requests, or decide update policy? Those responsibilities belong in Rust even
when this script is green. Use existing compiler/static-analysis and Rust tests
alongside this gate. A future Clang analysis pass can strengthen symbol and call
resolution but cannot replace that ownership review.
