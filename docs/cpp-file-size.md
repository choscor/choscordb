# C++ file-size audit

The initial audit used the working tree on 2026-09-21, including staged changes.
It scanned handwritten C/C++/Objective-C++ sources and headers under `desktop/`
and `tests/`. Nine files exceeded 1,000 physical lines. This threshold is a local
maintenance policy, not a C++ standard requirement.

| Original file | Lines before | Split responsibilities |
| --- | ---: | --- |
| `desktop/app/main_window.cpp` | 2,689 | Window orchestration, UI construction, workspace connections, lifecycle/recovery, navigator |
| `tests/desktop/workspace_test.cpp` | 1,644 | Lifecycle, results/export, profiles/live databases |
| `desktop/app/query_workspace.cpp` | 1,342 | Workspace commands and event handling |
| `tests/desktop/modern_ui_test.cpp` | 1,333 | Menus/captures and workspace/appearance |
| `tests/desktop/control_style_test.cpp` | 1,301 | Menus/inputs and shared surfaces |
| `desktop/tools/preview/preview_window.cpp` | 1,280 | Gallery/export orchestration and standard specimens |
| `tests/desktop/preview_test.cpp` | 1,241 | Gallery/exports and specimen behavior |
| `tests/desktop/navigator_sql_workspace_test.cpp` | 1,224 | SQL/sidebar history and object/session navigation |
| `desktop/bridge/engine_adapter.cpp` | 1,154 | Engine/query operations and preferences/history/recovery storage |

Test suites retain their existing Qt test classes, slots and execution order.
Their class declarations live in shared headers registered with CMake AUTOMOC;
method definitions are compiled as separate translation units.

The next-largest original production files were `profile_dialog.cpp` (716),
`object_explorer.cpp` (595), `preferences_dialog.cpp` (562), and `history_dock.cpp`
(494). They remain below the limit; size alone does not justify splitting every
cohesive class. Review responsibility boundaries as they grow.

## Practices and tools

The [C++ Core Guidelines, F.2 and F.3](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#f2-a-function-should-perform-a-single-logical-operation)
recommend single-purpose, short functions. File size is a useful review trigger,
but does not measure cohesion or complexity. Extract meaningful operations,
keep implementation helpers private, and avoid arbitrary numbered file chunks.

| Tool | Relevant capability | Use here |
| --- | --- | --- |
| Repository `cpp-size` gate | Enforces physical lines per file, including headers and tests | Required in `quality.py fast/full` and available as a focused stage |
| [clang-tidy `readability-function-size`](https://clang.llvm.org/extra/clang-tidy/checks/readability/function-size.html) | Function line, statement, branch, parameter, variable and nesting thresholds | Complementary analysis; `LineThreshold` limits functions, not entire files |
| [Sonar community CXX plugin](https://github.com/SonarOpenCommunity/sonar-cxx/wiki/CXX-Rules) | `metrics:TooManyLinesOfCodeInFile` rule | An option for teams already operating the community plugin; not installed here |

The existing clang-tidy profile is unchanged. Enabling a function-size check as
a blocking rule needs a separate audit of current functions and an agreed
threshold. The file gate needs only Python, shares the formatter's suffix scope,
and scans files on disk so new, untracked source files are checked too. Generated
trees (`build/`, `target/`) and third-party dependencies are outside its scope.

Run `python scripts/ci/quality.py cpp-size`. A file at exactly 1,000 lines passes;
1,001 fails with its path, first excess line, actual count, and limit. There is
no per-file exemption list.
