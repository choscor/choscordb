# Shadcn production migration evidence

Date: 2026-09-13. Baseline: `288ce7f36df8ca380db0194976fa765db26350db`.
The user approved the working gallery with **“approve”** after the
[gallery checkpoint](shadcn-gallery-evidence.md). Production migration began only
after that decision, satisfying the specification's ordered review gate.

## Production changes

The workspace, connection profiles, preferences, query settings, find/replace,
history, export, value detail, confirmations and modeless windows now compose the
approved shared controls, typography, icons and spacing. Navigator–editor–results
structure and established window sizes remain. At minimum width, Run/Cancel use
32-pixel icon buttons; transaction actions move to More while their menu entries
and shortcuts remain available. Toolbar presentation actions retain visibility
through Qt relayout. Onboarding actions hide when a connection is selected and
return when no connection is selected. PostgreSQL profile fields scroll within the existing
780×540 window.

Appearance offers System, Light and Dark only. Legacy density/accent fields remain
read-compatible and unchanged in stored records, including formerly invalid
custom colors; they have no rendering effect. Apply/restart preserves theme and
valid layout. Failed appearance loads cannot be overwritten by Apply: corrupt or
unsupported records require explicit retry/reset. Existing editor/profile/query/
history/recovery data contracts are unchanged; no Rust storage schema changed.

Application confirmations retain QMessageBox's button roles/results and safe
cancellation contract inside the shared centered, dimmed panel. Long diagnostics
scroll with the action footer visible; full text remains accessible. Existing
modeless windows retain their modality and native outer frame. Preview/cancel/
Apply updates already-open modeless windows and menus. Named button icons and
confirmation icons follow live theme changes.

## Verification

The final implementation has **independent requirements and code review
approval**, with no material finding remaining. Verification on the pinned
Qt 6.8.3 / QScintilla 2.14.1 stack completed after the final rendering fixes:

| Check | Result / local evidence |
| --- | --- |
| Repository full quality command | Passed: C++ formatting, Ruff checks/formatting, actionlint 1.7.7, Python tests, Rust fmt/check/clippy/tests, cargo-deny, native build and **33/33 CTest suites**; `build/shadcn-production-quality-final.txt` |
| Standalone headers | Passed, including the corrected incomplete-type setter; `build/shadcn-production-header-check.txt` |
| Native Cocoa modal/modeless subset | **6 checks pass**, covering long diagnostics, live theme/geometry, Escape/default cancellation, focus restoration and nonmodal behavior; `build/shadcn-production-cocoa-modals.txt` |
| Deterministic gallery exports | All **36 comparisons** refreshed successfully from the final code; `build/shadcn-production-gallery-captures.txt` |
| Release build | Passed with developer functionality excluded; `build/shadcn-production-release-build.txt` |
| Local bundle | Relocatable macOS stage validated and launched without development runtime paths, using an isolated temporary profile; `build/shadcn-production-stage.txt` |

The native modeless pixel regression accounts for the captured pixmap's device
scale; its initial fixed-coordinate Cocoa failure was a test-coordinate issue.
Offscreen checks and synthetic input do not establish physical backdrop pointer
delivery, screen-reader integration, or all supported window managers.

Behavior-focused red/green evidence includes production button adoption, semantic
icon repaint, safe standard confirmation variants, neutral tab-close rendering
with click dispatch, minimum-width toolbar startup/relayout, actual confirmation
preview cancellation, theme-only preference controls, real legacy persistence,
corruption repair, shared secondary actions, search/history grouping and bounded
profile/confirmation content. Existing editor/query/export/recovery workflows
remain the behavioral regression surface.

The previous `preferences-workspace` segfault was a stale test lookup:
`command_new_query` did not exist; the production action is `newQuery`. The test
now checks the real action before triggering it and retains its existing/new
editor font assertions. The standalone-header failure was corrected by moving
an AppearanceController-dependent inline setter to its implementation file.

## Captures

Four isolated production startup captures use Qt 6.8.3, macOS offscreen, Geist,
DPR1, with metadata beside each image:

- [Light, 1280×900](shadcn-reference/app-light-1280.png)
- [Dark, 1280×900](shadcn-reference/app-dark-1280.png)
- [Light, 960×640](shadcn-reference/app-light-960.png)
- [Dark, 960×640](shadcn-reference/app-dark-960.png)

The [coverage matrix](shadcn-coverage.md) and
[36-specimen capture index](shadcn-reference/qt-capture-index.json) retain the
component/reference comparisons. The [workflow capture index](shadcn-reference/app-workflow-capture-index.json)
records 12 production surfaces, including [profiles](shadcn-reference/app-profiles.png),
[preferences](shadcn-reference/app-preferences.png), [export](shadcn-reference/app-export.png),
[value detail](shadcn-reference/app-value-detail.png), [history](shadcn-reference/app-history.png),
and [query settings](shadcn-reference/app-query-settings.png). The corrected
[disconnect confirmation](shadcn-reference/app-disconnect.png) retains its full
message and footer. Native Cocoa captures show
[Light at 1280×900](shadcn-reference/app-cocoa-light-1280.png) and
[Dark at 960×640](shadcn-reference/app-cocoa-dark-960.png), at device scale 2 with
adjacent metadata. Production and representative secondary captures were inspected
after regeneration. Synthetic test fixtures do not demonstrate behavior
against a user's database or credentials.

## Performance and packaging

Two native Cocoa Release runs on arm64 macOS 26.5 / Qt 6.8.3 each traversed
1,000,000 rows with one query submission, verified the archived reread, and
collected 200 editor input samples. Reports are
`build/performance/shadcn-production-final-{1,2}.json`.

| Measurement | First run | Repeated run | Target |
| --- | ---: | ---: | ---: |
| Process to ready | 1069.47 ms | 315.55 ms | Warm ≤800 ms |
| Idle physical footprint | 79.95 MiB | 81.69 MiB | ≤120 MiB |
| Connected SQLite footprint | 81.28 MiB | 99.47 MiB | ≤160 MiB |
| Editor key-to-paint p95 | 0.64 ms | 0.73 ms | ≤30 ms |
| Worst interactive dispatch | 17.43 ms | 17.46 ms | ≤16 ms |
| Normal close | 45.40 ms | 57.14 ms | ≤3000 ms |

The repeated warm startup, memory, editor latency and normal close meet their
measured targets. Both runs contain one dispatch above 16 ms, so that gate remains
**unqualified**. Earlier same-stack baseline observations also exceeded it
(18–25 ms); these runs do not establish a causal regression or controlled
commit-matched comparison. First startup is fresh-profile, not controlled
cold-cache evidence. Display/cache/background conditions were not controlled;
physical footprint is distinct from RSS. PostgreSQL and other supported-platform
measurements remain unverified.

`build/shadcn-production-stage/choscordb.app` is a **local unsigned macOS bundle**.
Its `manifest.json` and `SHA256SUMS` record the packaged inventory. Validation
resolved all non-system runtime dependencies inside the bundle; the Cocoa smoke
created exactly one temporary metadata database without development runtime
search paths. The binary contains no developer preview/menu markers. Geist/OFL,
Lucide and shadcn notices/provenance are present; fonts/icons are compiled resources.
The stage references a working-tree source snapshot; only evidence documentation
was updated after staging. No package was published.

This is not a signed/notarized installer or cross-platform release qualification.
The cached Rust build emitted newer-macOS-object deployment warnings, so it does
not qualify macOS 13 support. Full dependency-notice/SBOM completeness, physical
screen-reader/window-manager checks and Windows/Linux release runs remain outside
this local evidence. Browser/Qt font and shadow rasterization and native shell
exceptions remain those in the [pinned reference](shadcn-reference.md).

## Changed-file groups

- `desktop/design_system/` and `desktop/resources/`: shared rendering, preview,
  tokens, typography and bundled licensed assets.
- `desktop/app/` and `desktop/widgets/`: workspace and secondary-screen adoption,
  shared modal presentation, appearance compatibility and cancellation behavior.
- `tests/desktop/`: real UI/storage/rendering regressions and corrected legacy
  appearance assertions.
- `CMakeLists.txt`, `scripts/release/`: Qt Svg, test registration, developer-build
  install guard and resource/notices packaging.
- `docs/design/`, `docs/BUILD.md`, `docs/performance/measurement-contract.md` and
  `.gitignore`: reference, captures, coverage and verification records. The supplied
  implementation specification is unchanged.
