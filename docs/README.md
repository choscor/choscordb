# ChoscorDB developer documentation

This directory holds the documentation for developers and maintainers of ChoscorDB.
The guide for people who use ChoscorDB is in [user-guide/](../user-guide/README.md).
Write all pages in Simplified Technical English, as the [style guide](style-guide.md) tells you.

## Pages

| Page | Contents |
| --- | --- |
| [style-guide.md](style-guide.md) | STE writing rules and the project term list. |
| [build.md](build.md) | Native build, dependencies and local run. |
| [ci.md](ci.md) | Quality gates and how to run them. |
| [architecture.md](architecture.md) | Crate map, Rust and C++ ownership, the ownership gate. |
| [design-system.md](design-system.md) | Visual tokens, components and icon rules. |
| [testing.md](testing.md) | Database fixtures and the performance harness. |
| [release-macos.md](release-macos.md) | macOS package, signature, notarization and update feed. |
| [release-windows-linux.md](release-windows-linux.md) | Windows and Linux packages and update feeds. |
| [release-lessons.md](release-lessons.md) | Release rules that come from past release failures. |

## Keep documentation current

A change that alters documented behavior must update the owning page in the same change.
Write the update in STE.
If a mapped change needs no documentation update, add this line to the pull request body or a commit message:

```text
Docs: none needed, <reason>
```

`python3 scripts/ci/docs_policy.py --changed --base origin/main` applies this rule.
CI runs it on each pull request.

## Code to documentation map

Each line maps a code path to its owning pages.
`scripts/ci/docs_policy.py` reads this block.
Each path must match at least one tracked file.

```docs-map
crates/driver-*/** -> user-guide/connections.md
crates/core/** -> docs/architecture.md
crates/bridge/** -> docs/architecture.md
crates/diagnostics/** -> user-guide/diagnostics.md
crates/export/** -> user-guide/results.md
desktop/design_system/** -> docs/design-system.md
desktop/resources/icons/** -> docs/design-system.md
desktop/widgets/sql_editor/** -> user-guide/sql-editor.md
desktop/widgets/editor_completion/** -> user-guide/sql-editor.md
desktop/widgets/search_panel/** -> user-guide/sql-editor.md
desktop/widgets/preferences_dialog/** -> user-guide/sql-editor.md
desktop/widgets/query_settings_dialog/** -> user-guide/sql-editor.md
desktop/widgets/profile_dialog/** -> user-guide/connections.md
desktop/widgets/export_dialog/** -> user-guide/results.md
desktop/widgets/value_detail_dialog/** -> user-guide/results.md
desktop/app/result_* -> user-guide/results.md
desktop/app/navigator_* -> user-guide/navigator.md
desktop/app/object_explorer* -> user-guide/navigator.md
desktop/widgets/history_dock/** -> user-guide/history-recovery.md
desktop/app/workspace_recovery* -> user-guide/history-recovery.md
desktop/app/diagnostics_* -> user-guide/diagnostics.md
scripts/ci/** -> docs/ci.md
.github/workflows/** -> docs/ci.md
scripts/release/** -> docs/release-macos.md, docs/release-windows-linux.md
CMakeLists.txt -> docs/build.md
cmake/** -> docs/build.md
tests/performance/** -> docs/testing.md
scripts/integration/** -> docs/testing.md
```
