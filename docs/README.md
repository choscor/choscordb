# ChoscorDB developer documentation

This directory holds the documentation for developers and maintainers of ChoscorDB.
The root [README.md](../README.md) lists the features for people who use ChoscorDB.
Write all pages in Simplified Technical English, as the [style guide](style-guide.md) tells you.

## Pages

| Page | Contents |
| --- | --- |
| [style-guide.md](style-guide.md) | STE writing rules and the project term list. |
| [build.md](build.md) | Native build, dependencies and local run. |
| [ci.md](ci.md) | Quality gates and how to run them. |
| [architecture.md](architecture.md) | Crate map, Rust and C++ ownership, the ownership gate. |
| [design-system.md](design-system.md) | Visual tokens, components and icon rules. |
| [audit-ui.md](audit-ui.md) | UI audit of 2026-11-13: inventory, state matrix and findings. |
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
crates/driver-*/** -> README.md
crates/core/** -> docs/architecture.md
crates/bridge/** -> docs/architecture.md
crates/diagnostics/** -> README.md
crates/export/** -> README.md
desktop/design_system/** -> docs/design-system.md
desktop/resources/icons/** -> docs/design-system.md
desktop/widgets/sql_editor/** -> README.md
desktop/widgets/editor_completion/** -> README.md
desktop/widgets/search_panel/** -> README.md
desktop/widgets/preferences_dialog/** -> README.md
desktop/widgets/profile_dialog/** -> README.md
desktop/widgets/export_dialog/** -> README.md
desktop/widgets/value_detail_dialog/** -> README.md
desktop/app/result_* -> README.md
desktop/app/navigator_* -> README.md
desktop/app/object_explorer* -> README.md
desktop/widgets/history_dock/** -> README.md
desktop/app/workspace_recovery* -> README.md
desktop/app/diagnostics_* -> README.md
scripts/ci/** -> docs/ci.md
specs/** -> docs/architecture.md
.github/workflows/** -> docs/ci.md
scripts/release/** -> docs/release-macos.md, docs/release-windows-linux.md
CMakeLists.txt -> docs/build.md
cmake/** -> docs/build.md
tests/performance/** -> docs/testing.md
scripts/integration/** -> docs/testing.md
```
