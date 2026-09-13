# Native shadcn gallery evidence

Dated 2026-09-13. This is the review record for the foundation/component/gallery
checkpoint in [the implementation specification](../specs/2026-09-12-shadcn-qt-design-system.md),
not approval of production screen migration. **User gallery acceptance received on 2026-09-13:** the user replied “approve”
after reviewing the working gallery and this evidence. AC6 is satisfied;
production layout/screen migration is now authorized and in progress.
The foundation/component/gallery layer was presented for user review, with known
baseline and platform limitations below. The full specification is not complete
at this checkpoint.

The starting ref was `288ce7f`: tracked files were clean; the supplied specification
was untracked. The initial local desktop baseline passed 25 of 26 CTest targets;
`preferences-workspace` crashed. Preserve that distinction when assessing the
current failure; it is not a newly passing test or a waived acceptance criterion.

## Reference and implemented seams

[Reference provenance, exact conversions and exceptions](shadcn-reference.md)
pin shadcn `2b3e6d4f8d9161fe5c19340dc383aade392012dd`, Base UI / Nova / Neutral,
Geist and Lucide. The [coverage matrix](shadcn-coverage.md) maps each app-owned
surface to its gallery ID and production seam. Inventory rows alone do not prove
rendering fidelity or interaction coverage.

| Evidence seam | What it checks | Current limit |
| --- | --- | --- |
| `design-system`, `typography`, `icons` | Literal semantic values, obsolete accent/density rendering invariance, bundled fonts, actual line spacing/weights, Unicode, named SVG assets and scale | Rasterization and fallback require visual review on each supported platform |
| `components`, `control-style`, `modal-panel` | Real input, variants, focus/disabled/invalid/loading, popup surfaces, modal rejection and focus restoration | Native window-manager/accessibility behavior remains separate from offscreen tests |
| `preview` | Navigation, independent themes, token/source copying, synthetic fixtures, export pixels and visible failures | 24 preview checks pass, including actual visible popup content; four deliberate export-render omissions each fail the corresponding regression |
| Existing desktop/Rust suites | Editor/model/controller, persistence, bounded values, cancellation and database behavior | Existing preferences-workspace crash remains unresolved; not all server integration fixtures run locally |
| Release configuration and install inventory | Developer functionality excluded; fonts/icons and notices present | Local staged install is not a signed, self-contained distribution |

Foundational shared rendering and the development menu are integrated. Production
layouts/screens and removal of legacy appearance controls remain behind the user
review gate. Stored theme/layout/editor settings are not intentionally reset.

## Reproduce the gallery

From the repository root, build the developer configuration. Qt Widgets, Svg,
Concurrent and QScintilla are required; see [build instructions](../BUILD.md).

```sh
cmake -S . -B build/gallery -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/gallery --target choscordb-component-gallery
build/gallery/choscordb-component-gallery --list
build/gallery/choscordb-component-gallery --specimen buttons
```

The standalone process creates synthetic fixtures without opening a database,
credentials or production profile. The development app entry opens the same
preview. Search operates on navigation/specimen names. Token values and source
paths are copyable; design definitions are edited in source, not in the gallery.

For deterministic exports, use the same executable and Qt installation for both
runs and keep the metadata JSON beside the PNG:

```sh
capture_dir="$(mktemp -d)"
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 build/gallery/choscordb-component-gallery \
  --specimen buttons --export "$capture_dir/buttons.png"
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=1 build/gallery/choscordb-component-gallery \
  --specimen typography --single --export "$capture_dir/typography-light.png"
```

Default comparison size is 1280×900 logical pixels; `--single` exports Light at
640×900. Metadata records specimen, environment, font, scale, themes and fixture
state. Export uses deterministic fixtures and reduced motion; interactive
controls can still be inspected in the normal window. Unknown specimen/section
names and image/metadata write failures return nonzero and report an error.
Actual menus, completion/tooltip popups and modal examples require inspection of
the popup content, not merely a changed screenshot hash.

The checked-in browser reference is 1280×620, DPR1, Chrome 152.0.7977.84 on macOS,
bundled Geist, reduced motion; see its [capture metadata](shadcn-reference/capture-environment.json).
Its static and focus/hover examples are not a complete upstream interaction
harness. Compare individual controls at their logical dimensions rather than
stretching either full image. The [capture index](shadcn-reference/qt-capture-index.json) records all 36 Qt
comparisons and adjacent metadata. Representative captures include
[buttons](shadcn-reference/qt-buttons.png),
[typography](shadcn-reference/qt-typography.png),
[fields](shadcn-reference/qt-fields.png),
[menus](shadcn-reference/qt-menus.png), and
[SQL editor](shadcn-reference/qt-sql-editor.png). All 36 exports were refreshed from the final pinned Qt 6.8.3 build. The
checkbox, select, numeric-field and SQL-editor comparisons were inspected after
the final corrections; user visual acceptance was subsequently received on 2026-09-13.

## Verification ledger

The following `build/` files are local evidence artifacts, not portable checked-in
results. Preserve or regenerate them when reviewing another checkout.

| Recorded run | Result |
| --- | --- |
| `build/shadcn-quality-full.txt` | C++ formatting, Ruff checks/formatting, actionlint 1.7.7, Python tests, Rust formatting/check/clippy/tests and dependency policy completed before native testing |
| Same full run, pinned Qt 6.8.3 | 30/32 CTest targets passed. `preview` failed its menu-content witness assertion; `preferences-workspace` segfaulted in `confirmedPreferencesReachExistingAndNewEditors`, matching the failing baseline area |
| `build/shadcn-final-native-tests.txt` | Final rebuild after all rendering/review fixes: 31/32 targets pass; only the baseline `preferences-workspace` crash remains |
| Final `control-style` suite | 28 checks pass, including scoped neutral checkbox rendering, visible combo/spin arrows, live themes, resize, disabled/boundary states and unchanged click behavior |
| `build/shadcn-final-format.txt` | Final C++ formatting passes; `git diff --check` passes |
| `build/shadcn-cocoa-controls-dpr1.txt` | Native Cocoa checkbox/arrow subset: 8 checks pass at DPR1 (`QT_SCALE_FACTOR=0.5` on the Retina display) |
| `build/shadcn-cocoa-preview.txt` | Native Cocoa modal/menu/completion/tooltip interaction subset passes on Qt 6.8.3 |
| `build/shadcn-header-check.txt`, `build/shadcn-new-header-check.txt` | Header check encounters the existing incomplete `AppearanceController` type in unchanged `editor_preferences.h:20`; all new/changed design-system header units compile independently |
| `build/shadcn-final-release-build.txt` | Release build completed with `BUILD_TESTING=OFF`; linker warned about Rust objects built for newer macOS deployment targets |
| `build/shadcn-final-install.txt` | Staged app includes shadcn, Geist/OFL and Lucide licenses/source manifests. Fonts and the semantic SVG catalog are compiled resources |
| `build/shadcn-final-release-smoke.txt` | Installed app smoke exited successfully with explicit pinned Qt/QScintilla `DYLD` environment; this does not establish standalone dependency bundling |
| Staged binary inspection | `build/shadcn-install-inventory.json` records required notices and absence of `openDesignSystemPreview`, `PreviewWindow`, and the development menu label; developer executable is absent |
| Independent requirements review | Approved the pre-gallery source scope after fixes to modal geometry, focus, selection, typography, popup evidence and documentation; final checkbox/arrow delta also approved; user visual acceptance was subsequently received on 2026-09-13 |
| Independent code review | Approved after fixes to popup export, focus-frame restoration, runtime rendering and release exclusion; final checkbox/arrow delta independently reviewed and approved; no material findings remain |

The canonical full command is `python scripts/ci/quality.py full`. Focused native
reruns must use the same pinned Qt/QScintilla environment as
`scripts/ci/desktop.py`; mixing Homebrew Qt 6.11.2 and pinned Qt 6.8.3 results must
be labelled. Pixel-coordinate control tests assume DPR1: an extra Cocoa run at
the display default DPR2 failed those coordinate assertions; the explicit DPR1
Cocoa rerun passes. This is not evidence of full high-DPI platform qualification.
Offscreen success does not establish native menu placement,
screen-reader integration, signing, installers or shipping platform compatibility.

## Performance observation

Two native Cocoa Release observations are recorded in
`build/performance/shadcn-gallery-final-1.json` and
`build/performance/shadcn-gallery-final-2.json`, on arm64 macOS 26.5 / Qt 6.8.3.
Each traversed 1,000,000 rows with one query submission, verified an archived
reread after eviction, and collected 200 editor input samples.

| Measurement | First run | Repeated run |
| --- | ---: | ---: |
| Process to workspace ready | 1086.89 ms | 293.11 ms |
| Idle / connected SQLite physical footprint | 43.56 / 43.69 MiB | 43.92 / 44.05 MiB |
| Editor key-to-viewport-paint p95 | 0.32 ms | 0.33 ms |
| Worst interactive dispatch / count above 16 ms | 17.97 ms / 1 | 15.02 ms / 0 |
| Close request to window closed | 56.08 ms | 62.60 ms |

The repeated run meets the warm-start and dispatch targets; the first run does
not. These observations do not establish all performance gates as passing.
Existing same-stack modern-UI runs also show slower first startup and occasional
18–25 ms dispatch. OS caches/background load were not controlled, so these are
not qualified commit-matched regressions or controlled cold-start measurements.
Memory is physical footprint, with RSS separately retained in JSON. Follow the
[measurement contract](../performance/measurement-contract.md): PostgreSQL and
supported-platform qualification remain outstanding. Viewport paint is not
compositor presentation.

## Review boundary

Documented accessibility overrides include an opaque 3:1 focus treatment,
stronger muted/destructive text where the default tint fails contrast, forced
contrast and reduced motion. Native title bars, OS menu bars/file pickers,
font rasterization/fallback, absent backdrop blur and Qt shadow rasterization are
explicit comparison exceptions; they do not exempt app-owned controls.

The final affected checks, regenerated comparisons, documented differences and
separate requirements/code review approvals are recorded above. Record the user's
actual gallery decision here when it occurs. The user subsequently approved this checkpoint on 2026-09-13. AC6 is satisfied;
[production migration evidence](shadcn-implementation-evidence.md) records the
authorized next stage, its final checks and remaining platform limits.

## Changed-file groups

- `desktop/design_system/`: tokens, fonts/icons, production controls, modal panels,
  typography, independent preview and theme integration.
- `desktop/resources/`: bundled licensed Geist/Lucide assets and provenance.
- `desktop/tools/component_gallery.cpp`, `desktop/app/main_window.cpp`: gallery
  CLI/export and development-only application entry.
- `desktop/widgets/sql_editor.cpp`: themed fold-margin paper.
- `tests/desktop/`: rendering/input/export regressions and updated theme assertions.
- `CMakeLists.txt`, `scripts/release/`: shared Qt Svg dependency, test targets,
  development-build install guard and licensed-resource inventory.
- `docs/design/`, `docs/BUILD.md`, `.gitignore`: reference, coverage, captures and
  reproducible build/review evidence. The supplied specification is unchanged.
