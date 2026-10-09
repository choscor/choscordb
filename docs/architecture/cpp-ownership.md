# C++ ownership gate

Rust owns application behavior. C++ owns presentation, native UI integration,
transient view state, and typed bridge conversion, as defined in `CLAUDE.md`.

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
| `translated-control-flow` | Comparisons or searches against `tr()` text |
| `limit-revalidation` | Comparisons with fields of a Rust limits DTO (`limits.max…`) |
| `rust-limit-literal` | Literals equal to a Rust `MAX_`/`MIN_`/`DEFAULT_` constant of 4096 or more |
| `text-matching` | `contains`/`indexOf`/`startsWith`/`endsWith`/`compare` with `Qt::CaseInsensitive`, `toLower()`/`toCaseFolded()` followed by a search, and `QSortFilterProxyModel` text filters |
| `object-kind-literal` | Branches on object-kind words such as `"table"` or `"connection"`: `==`/`!=`, membership in a brace list, `compare`, and `startsWith`/`endsWith`/`contains`/`indexOf` with a kind or a three-character prefix or suffix of one |
| `driver-literal` | The same branch forms for driver names such as `"mysql"` or `"sqlite"`, matched case-insensitively |

The last three rules apply to presentation, display-model and bridge code.
Literals match plain, `u"…"`, `u8"…"`, `_s`/`_L1` suffixed and
`QStringLiteral`/`QLatin1String[View]`/`QStringView`/`QAnyStringView`/`QString`
wrapped forms. Use
`TextFilter` (`desktop/bridge/text_filter.h`) for list and tree filters,
`EngineAdapter::objectKindTraits` for kind-dependent behavior, and Rust driver
DTOs (`driverWorkflow`, `profileDriverForm`, `transactionGuard`) for driver
differences. Design-system and developer-tool code may map kinds and drivers to
appearance. Kind-to-icon and kind-to-label mapping for screens lives in
`desktop/app/object_kind_icon.h`, which carries one reviewed exception. Bridge
event dispatch compares event kinds through a variable named `eventKind`; only
`eventKind == "schema"` is exempt, because that event shares its word with an
object kind. Any other kind word compared with `eventKind` is still reported.

A UI-only number that equals a Rust limit (event-loop slicing, display paging)
needs a `// ui-budget: <reason>` marker on its line or the line above. A marker
without a reason does not count. `rust-limit-literal` scans only constants of
4096 or more, because small numbers collide with ordinary geometry and counts;
smaller limits (result caps, recent-item counts, history pages) still belong in
Rust and must arrive through a DTO or cached accessor, which review enforces.

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

The 2026-10-09 migration moved the remaining policy that the lexical rules could
not see into Rust, then added rules for each class of drift it found:

- object-kind traits for tabs, panes, pins, search, completion, templates, ER
  diagrams, connection roots and repeated index rows;
- driver workflow, profile form fields and manual-transaction guards;
- list and tree text filtering through one Rust `TextFilter`;
- per-query page sizes, page-lease bookkeeping and command-queue limits;
- result, value-preview, history-preview and JSON-view messages and budgets;
- pin edits, recovery document IDs, object-tab context encoding, saved SQL
  location, diagnostics export names and background check intervals;
- quick-search ranking tiers, tie-breaks, the shown-row cap, recent-object and
  history-page limits (`QuickSearchNeedle::plan` and `limits`);
- SQL-export table naming, system-schema visibility per driver, and which
  metadata rows carry column details.

`TextFilter` folds with Rust `to_lowercase` and trims Unicode whitespace, so it
can differ from Qt's `Qt::CaseInsensitive` for a few characters (for example
final sigma and title-case digraphs); every list filter now shares Rust's rule.
Quick-search ties order titles by Unicode scalar value rather than UTF-16 code
unit, which differs only between supplementary characters and U+E000–U+FFFF.

Judgment calls kept in C++, reviewed rather than exempted:

- The shutdown sequence (disconnect, history flush, window close) stays in the
  adapter as Qt event-loop integration. Admission while closing is still checked
  in C++; moving it means a Rust close protocol in the engine.
- The navigator search walks loaded tree nodes in time-sliced passes and tests
  each label with `TextFilter`; the walk is view traversal, the match is Rust's.
- The copy and JSON-view deferred-value loaders stay separate: each is Qt
  watcher and generation orchestration around the Rust `DeferredAssemblerJob`,
  with different completion contracts.
- `shortcut_catalog.cpp` compares Qt `QKeySequence` round trips. Rust validates
  the stored document, including sequence size and NUL bytes.

## Limits and maintaining the gate

This is a dependency-free lexical gate, not a C++ compiler, call-graph analyzer,
or proof of architectural correctness. It cannot infer the purpose of arbitrary
arithmetic, loops, validation or data transformations. It does not establish that
Rust revalidates UI input or that a bridge call runs off the UI thread. Macro token
pasting, encoded strings, indirect calls, unfamiliar APIs, transitive third-party
wrappers, dynamic CMake expressions and generated sources can evade these rules.
Ambiguous unqualified names such as `open` and `connect` are not banned because
they are also normal Qt UI methods. The kind and driver rules see literals at the
branch, so membership in a named container (`static const QSet<QString> kinds{…};
kinds.contains(kind)`) and comparison with a named constant (`kind == kTable`)
evade them; review rejects both forms the same as the literal ones. The CMake scan is lexical and does not resolve
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
