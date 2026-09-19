# MVP UI implementation evidence

Status: component foundation implemented. The user subsequently requested the
next implementation step with components already done. The first post-component
slice is recorded in [SQL document ownership evidence](mvp-document-ownership-evidence.md).
The checkpoint 1 record below is historical. Subsequent whole-spec work is recorded
in [complete flow evidence](mvp-flows-implementation-evidence.md); completed-screen
review was accepted on 2026-09-19. The checkpoint notes below remain historical.
Contract: [2026-09-13 MVP UI flows](../specs/2026-09-13-mvp-ui-flows.md).

## Baseline and scope

- Starting Git revision: `d2cbac7ad6b33d2d41a90bfb04baf0eeaf205bd4`.
- Initial tracked diff: empty. The user-supplied spec was the only untracked file;
  it is preserved unchanged.
- Baseline focused native suite: 8/8 passed using Qt 6.8.3 in `build/ci/native`
  (`typography`, `icons`, `modal-panel`, `control-style`, `components`, `preview`,
  `design-system`, `value-preview-model`).
- This checkpoint concerns reference capture and shared components/gallery.
  Production screen/navigation, metadata, SQL/object result ownership and
  Export/Preferences behavior await checkpoint 1, as required by the spec.
- Native offscreen tests establish widget behavior. Cocoa captures and native
  focus/input checks are reported separately. No historical screenshot counts
  as new visual evidence.

## Reference and component evidence

The [reference manifest](mvp-reference/manifest.json) pins the local prototype
files and records resolved browser styles, fixtures, environments and capture
rectangles. [Surface coverage](mvp-coverage.md) maps app-owned UI to production
components and named native exceptions. [Native evidence](mvp-native/README.md)
records concrete accessibility/rendering exceptions and reproduction commands;
its [family comparisons](mvp-native/family-comparisons.md) show unscaled
browser/native buttons, fields, selectors, switches, tabs, menus and panel treatment.

## Architecture audit for screen migration

The connection actor owns one active cursor (`crates/core/src/actor.rs`). A new
Execute closes that cursor and can finalize an automatic transaction. The SQL
workspace clears its busy flag after storing a page, before the cursor is
necessarily exhausted. A second object read must therefore coordinate with the
retained SQL cursor and export lifecycle; calling the existing Execute path
blindly would truncate paging/export and could change transaction state.

Navigator metadata currently retains identity/name/kind, discarding richer column
properties. Typed inspection contracts need real column/default/index/key
properties and explicit availability reasons. Metadata requests already have
correlation tokens; DDL success/failure needs equivalent correlation.

Document execution targets and result origins are implicit. Central navigation
needs a shared pointer/menu/shortcut guard, distinct browsing connection context,
and explicit disconnected document targets. The existing History dock and
last-tab replacement behavior are superseded.

Preferences currently acknowledges editor and appearance writes separately and
can report saved before both succeed. Its five-section modal conversion must
coordinate acknowledgments and retain failed drafts. Export already waits for
cancellation cleanup on Close/Escape; preserve this while enabling modality.
Layout migration should retain geometry, editor splitter and sidebar width,
retiring only incompatible dock placement. Recovery remains disconnected.

## Post-checkpoint vertical slices

1. Document targets and immutable shared SQL result origin; native two-document
   SQLite observations, undo/selection/scroll, no execution on switching.
2. Central Start/SQL/Object/History navigation with shared active-operation guards
   and reachable Cancel through pointer/menu/custom shortcut routes.
3. Real typed Columns/Indexes/Keys/DDL with correlation, delayed failures/retry,
   SQLite/PostgreSQL fixtures and unusual identifiers.
4. Separate bounded object Data with safe retained-cursor coordination, SQL return,
   page/copy/export/detail ownership, aggregate budgets and transaction effects.
5. Sidebar Test/Save/Connect and management; inert history/generated SQL and
   missing profiles, credential failures preserving drafts.
6. Modal Export with native focus, cancellation cleanup/atomicity and retry.
7. Five-section Preferences with coordinated persistence, live-preview discard,
   failed drafts and restart evidence.
8. Upgrade/restart fixtures, compatible layout preservation, full quality and
   performance comparisons, then the separate completed-screen visual review.

## Red → green evidence

Each behavior below was exercised at the real Qt widget/model or standalone CLI
seam before its implementation. Existing assertions encoding superseded Nova
colors/geometry were updated deliberately; unrelated data semantics were retained.

| Slice | Observed red | Green evidence |
| --- | --- | --- |
| Green/platform foundation | Rendered primary #171717 instead of #287f66; Geist instead of platform UI | Components/Typography/Design-system suites |
| Compact controls, fields and navigation | Button32 instead of33; selector31 instead of33; tree row28 instead of33 | Rendered sizes plus actual editing, switch/Space, tab/tree selection and icon input tests |
| Icon stroke/paths | Old Lucide silhouettes differ from literal prototype paths;4px instead of3.2px scaled stroke | Actual resource render witnesses and offline SVG validation |
| Editor action and focus | Missing12px/12px-padding variant; pressed geometry shifted; green focus disappeared into green action | Custom/native primary focus contrast, stationary press, Space dispatch, typed editor-action context |
| Capture dimensions | CLI rejected --theme/--width/--height | Fresh standalone Light1280×900 and Dark960×640 PNG/metadata checks |
| Narrow gallery | Requested960×640 became960×1066 | Exact window size, visible pinned export, keyboard Tab scroll/activation |
| Modal presentation | Flat black/26 overlay; no outside shadow; fine background stripes remained sharp | Tint/shadow/blur rendering, owner restoration, real modal input/geometry tests |
| Shutdown regression repair | Both hidden-owner close confirmations stack-overflowed through grab→Resize→capture | Scoped capture guard; both existing disconnect/workspace slots and full modal suite pass |
| Pending cancellation | Cancel immediately re-enabled Run | Run/Cancel disabled until explicit fixture acknowledgment; visible Cancelling state |
| Result feedback | Missing error/loading specimens | Zero rows while pending/failed; retry/manual completion restores literal NULL versus empty and unknown-total label |
| SQLite draft | No SQLite path field on driver switch | Actual driver switching retains entered path |
| Open selector capture | PNG lacked independently observed real popup text | Actual popup-content witness present in export |
| Legacy saved font | Saved Geist resolved to platform fallback in a fresh process | Startup registers legacy font while keeping UI platform typography |
| Scoped gallery paper | Scroll content inherited unrelated light/gray paper | Actual Light/Dark content render uses white/#20272b; final captures regenerated |
| Detached and nested Dark specimens | Dark modal owner rendered light after reparenting; nested scroll paper remained white | Standalone capture test checks actual center/lower pixels in Dark modal and nested-scroll exports, plus contrasting Light/Dark field cases |
| SQL pixel-font adapter | Default SQL capitals rendered at 1pt (measured cap height0) | Actual glyph capture is legible, default line box24px;22pt custom font grows without clipping, restoring the default retains text/selection/revision/undo |
| Export viewport settlement | Four Light/Dark field/tab cases clipped the right field edge or bottom active indicator beneath stale scrollbars | Showing the offline fixture without an on-screen window and processing its layout produces controls wholly within the captured viewport; six standalone data cases pass |
| Reference menu/tab/selector details | Short menu panel76px instead of minimum255; pane first9px painted white; selector arrow used muted ink | Public rendered geometry/ink tests plus actual menu/tab/selector input and refreshed family comparisons |

### Native input record

These commands use Qt's Cocoa platform and real widget event/focus paths. They
are separate from normalized widget-image captures. On macOS 26.5 with Qt 6.8.3,
the following 13 test invocations passed (23 QtTest outcomes including setup and
cleanup):

```sh
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-preview-tests narrowGalleryKeepsNavigationAndActionsReachable examplesOpenActualDismissibleSurfaces completionUsesTheRealOfflinePopup connectionSpecimenSwitchesDriverFieldsWithoutLosingDrafts
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-modal-panel-tests questionEscapeKeepsCancelResultAndRestoresKeyboardFocus longDiagnosticsScrollWhileCancellationStaysVisible openConfirmationTracksThemeAndParentGeometry modalBackdropUsesReferenceTintAndRestoresItsOwner
QT_QPA_PLATFORM=cocoa QT_SCALE_FACTOR=1.25 build/ci/native/choscordb-preview-tests narrowGalleryKeepsNavigationAndActionsReachable
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-control-style-tests menusTabsAndScrollbarsUseSharedSurfacesAndRemainInteractive fieldsExposeInvalidBorderAndPreserveReadOnlyAndPopupInput
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-editor-tests defaultPixelFontRendersLegiblyAndCustomPointFontPreservesEditing fontChangePreservesDocumentUndoAndSyntaxEmphasis
```

Coverage includes Tab/Shift+Tab and restored focus, Escape rejection, long
diagnostic scrolling, resize/live theme changes, popup/menu/selector activation,
completion input, retained SQLite drafts, and reachable bottom controls at
960×640 with 125% scale. These are automated native input checks, not physical
keyboard, IME or VoiceOver certification. macOS emitted a text-service
CFMessagePort warning; the direct input/focus assertions passed.

The separate Cocoa capture regression command also passed all11 data rows
(13 outcomes including setup/cleanup):

```sh
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-preview-tests standaloneCapturesRequestedThemeAndViewport exportedPopupContainsItsVisibleContent
```

This checks requested Light/Dark image dimensions, actual palette pixels,
unclipped field/tab geometry and independently observed text ink in actual
modal/menu/completion/tooltip/selector captures. Native grab witnesses are
normalized from Retina device pixels to the declared logical output units.

The final family-detail fixes also passed three focused Cocoa tests (five
outcomes including setup/cleanup), plus the full offscreen control-style
(35 outcomes) and preview (38 outcomes) suites:

```sh
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-control-style-tests menuPanelHasReferenceMinimumWidthOutsideItsShadow paneTabsPaintReferenceInsetsWithoutMovingDocumentTabs selectArrowUsesForegroundInkAndPreservesPopupInput
```

## Verification and reviews

The final canonical full quality command passed after the font, capture and
family-detail fixes on2026-09-13:

```sh
PATH="$PWD/build/ci/python/bin:$PWD/build/ci/tools/bin:$PATH" build/ci/python/bin/python scripts/ci/quality.py full
```

Results: C++ formatting, Ruff lint/format, pinned actionlint,72 Python tests,
Rust format/check/clippy/tests (261 passed,23 explicitly ignored), cargo-deny
and the Release native build all passed. All33 CTest suites passed in51.98s.
Qt6.8.3/QScintilla2.14.1 were used in `build/ci/native`; the final terminal log is
`/tmp/choscordb-mvp-quality-final.log`. `git diff --check` is clean.

The reference run produced124 state screenshots plus60 component crops with
source/image hashes, computed style/geometry and actual resolved fonts. Twelve
browser modal interaction assertions passed; no browser runtime errors or source
modifications were observed. Native matrix:42 specimens × two themes × two sizes
(168 captures), regenerated after the last shared-style changes; all
source/executable hashes matched that capture build. The subsequent failed-row
expansion repair below changes behavior, not the captured styling. The comparison set contains44
unscaled family/detail/context sheets. Capture procedure, exact mappings and exceptions are documented in
[native evidence](mvp-native/README.md). These widget render captures are distinct
from native input checks and the later completed-screen review.

Separate independent review axes:

- **Code review — approved:** independent fix rounds closed synchronous backdrop
  capture recursion, live-theme and layout issues. The reviewer inspected the
  final font adapter, popup/Retina witness handling and menu/tab/selector fixes;
  no open material finding remains. Graphics ownership and input regressions
  were inspected separately.
- **Requirements review — approved for checkpoint1:** independent fix rounds
  closed missing state specimens, concrete exception records and family
  comparison/input evidence. The reviewer verified all168 native captures,
  source/executable/browser hashes and44 comparison sheets, then inspected the
  corrected menu/tab/selector details and final notes. No critical or important
  finding remains. The explicit selector glyph mapping awaits user visual review.

The full D1–D5 specification is not complete at checkpoint1. User review must
precede central-screen and flow migration; checkpoint2 remains separate. Remote
cross-platform CI, real PostgreSQL workflow/performance acceptance and final
screen captures belong to post-checkpoint integration, not this gallery claim.

## Files changed

Shared foundation: `desktop/design_system/{theme,theme_manager,components,
control_style,icons,modal_panel,preview_window}.*`; editor font adapter:
`desktop/widgets/sql_editor.cpp`; developer CLI: `desktop/tools/component_gallery.cpp`.
The subsequent gallery crash repair also changes `desktop/models/navigator_model.cpp`
and adds model/gallery regression coverage.
Nine reference-present SVG icons and their `SOURCE-LUCIDE.json` provenance were
updated. No engine/bridge/storage or production screen-controller file changed.

Tests: design-system, typography, components, control-style, icons, modal-panel,
preview; modern-ui/secondary-design/editor assertions follow the replacement
appearance. Evidence/tooling: `scripts/design/`, `docs/design/mvp-reference/`,
`docs/design/mvp-native/`, active design README/coverage/evidence and performance
reference authority; `.gitignore` admits the new pinned PNG evidence.
The supplied spec remains unchanged. No commit, push, PR or publication was made.

## Gallery failed-row expansion repair

The user reported a gallery crash after checkpoint1 was presented. The macOS
crash report showed SIGBUS in QTreeView expansion. An actual Cocoa pointer test
on the gallery's disconnected connection reproduced SIGBUS (exit138). Failed
nodes were automatically fetchable, so expansion removed their existing error
row synchronously inside Qt's layout.

`NavigatorModel::canFetchMore` now permits automatic fetching only for unloaded
nodes. Failed rows remain visible until explicit Refresh; existing toolbar and
context-menu Refresh still issue a new request. The model regression checks
retained error rows, no automatic dispatch, explicit retry with a fresh token,
stale reply rejection and successful recovery. The actual gallery regression
passed five additional Cocoa runs after the fix. Both independent cause/regression
and code reviews approved the change without material findings.

Final verification after the repair: the canonical `quality.py full` command
passed (including all33 native suites in65.23s,72 Python tests and261 Rust tests
with23 explicitly ignored). Log: `/tmp/choscordb-gallery-crash-quality.log`.

The gallery was rebuilt and relaunched on `sidebar-tree`. The later visual
cleanup refresh below replaces the capture matrix with the repaired build.

## Cleanup from native component review

The user supplied four screenshots showing blurred icons, dark modal corners,
duplicated menu outlines, boxed dropdown rows and a whole-tree focus frame.
[Native cleanup evidence](mvp-native/cleanup-review.md) records their causes,
corrections and actual Cocoa compositor captures. The shared SVG renderer,
gallery icon display, confirmation DPR handling, popup/menu styling and frameless
modal opening paths were corrected. The tree-expansion repair remains intact.

Both independent review axes approved this cleanup. Review found and closed the
important difference between testing `show()` and the actual gallery `open()`
path: the latter forced native sheet mode until the shared asynchronous methods
were overridden. The final canonical full quality command passed after those
fixes: all33 native suites in55.60s,72 Python tests,261 Rust tests with23 ignored,
formatting/lint/type checks, dependency audit and Release build. Log:
`/tmp/choscordb-clean-quality-final.log`.

The native matrix and comparison sheets were refreshed for this build. User
component approval is still separate; production screen migration has not begun.
