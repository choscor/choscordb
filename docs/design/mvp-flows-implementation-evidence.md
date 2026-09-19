# MVP screens and complete flows

Contract: [approved MVP specification](../specs/2026-09-13-mvp-ui-flows.md).
Status: implementation complete; checkpoint 2 accepted by the user on 2026-09-19.
The acceptance applies to the presented comparisons and documented variations;
performance qualification limits remain as reported.

The user asked to continue through the complete specification after the first
[document ownership slice](mvp-document-ownership-evidence.md). All changes remain
uncommitted against `3c793bf555f02ef0392c9e03fbe21104d38e1552`; that first slice is
preserved. The interrupted session resumed on 2026-09-19. Earlier `/tmp` logs may
have expired; final verification records below must come from the resumed run.

## Implemented behavior

- Central Start, SQL, Object and History screens share the sidebar. Screen,
  document, menu and keyboard routes use active-operation guards. Closing the
  last SQL document returns to Start with dirty/recovery safeguards.
- SQL documents retain explicit targets and editing state. One shared SQL result
  captures its originating document/connection. History/generated statements
  open without execution; restored documents remain disconnected.
- Real SQLite/PostgreSQL Columns, Indexes, Keys and DDL use bounded typed
  inspection responses, per-request correlation, availability reasons, retry and
  stale-response rejection. SQLite metadata budgets are checked before identity,
  parent, column and property allocations, including long-name amplification.
- Object Data has a distinct result owner on the existing engine and connection.
  Short bounded reads preserve the SQL cursor. Paging, copy, export and deferred
  value inspection reuse the result lifecycle and aggregate transfer budgets.
  PostgreSQL isolates failed/cancelled reads with an internal savepoint without
  rolling back pre-existing user work or the SQL portal.
- Saved profile actions reuse credential-safe services. Save & connect waits for
  profile persistence, connects, then opens a new SQL document. Failed connection
  drafts remain editable and retryable.
- Export and Preferences use app-owned modal panels with contained/restored
  focus. Accepted export cancellation and invalidation wait for terminal cleanup.
  Preferences coordinates editor, execution, history and appearance writes;
  errors keep the draft. Reset is staged until Save, including corruption repair.
- History filtering explicitly covers the current bounded page; hidden selections
  cannot reopen SQL. Compatible geometry/sidebar/splitter settings remain readable;
  obsolete history-dock placement is retired.

## Red → green and review record

Real public-path regression tests cover two distinct SQLite sources, SQL/object
paging continuation, typed metadata and unusual identifiers, empty/unsupported
metadata, failure/retry/stale replies, pointer and keyboard navigation, accepted
export cancellation, modal focus, profile persistence and restart behavior.
Tests for superseded automatic retargeting/dock/nonmodal assumptions were updated
deliberately while keeping database, rollback and persistence assertions.

Independent reviews completed so far:

- D4 code and requirements: approved after staging Reset. Four restart cases
  cover valid/corrupt storage × Close/Save. Native Cocoa focus/input checks passed.
- D2 backend code: approved after incremental SQLite metadata accounting replaced
  a post-allocation budget check. Large Unicode table names with 45 and 2,000
  columns are rejected, with a valid small-object contrast. Peak heap allocation
  was not measured by this test; the bound also relies on reviewed construction.
- D2/central behavior requirements: no material findings. The minor cancellation
  explanation is being made valid for both SQL and Object Data.
- Final native code review found and closed a cancellation/completion race: a
  suppressed late page left `fetching_` set after successful terminal completion.
  The public adapter-event regression begins with a real SQLite schema, requests
  Cancel, delivers the already-finished page/terminal sequence, and checks Run and
  navigation. Real cancellation has a contrasting cancelled outcome test.
- Follow-up review found and closed a cached profile identity bug. Reopening New
  connection now starts a distinct draft; a two-profile save regression verifies
  the first saved profile is preserved. Targeted profile management still selects
  its requested identity.
- The independent visual audit identified modal composition, sidebar/Start and
  object metadata row differences. These were corrected with rendered sidebar/background/footer and metadata-row
  regressions before checkpoint 2;
  successful behavioral tests do not establish visual approval.

The resumed parent checks also cover cancellation confirmation when only Object
Data is active. The object detail test uses a complete pointer click/double-click
sequence; its export assertion follows the existing quoted, CRLF CSV contract.

## Visual evidence and remaining acceptance

[Reference manifest](mvp-reference/manifest.json) records 124 states, 60 crops,
12 passed browser interaction checks, source/image hashes, font observations,
fixture setup and zero browser runtime errors. It was regenerated from the
unmodified local prototype. Capture tooling remains in `scripts/design/`.

Native captures cover both themes and both approved sizes, identify actual
client rectangles, and distinguish the prototype's 57-pixel mock menu/title region
from native chrome. No image is silently scaled to hide a mismatch. At minimum
size, font legibility and reachable actions take precedence; record any actual
exception. Component approval does not imply final screen approval.

Final native comparisons, exact differences, quality/PostgreSQL validation, Release
resource checks and repeated baseline/current probes are linked below. The user
accepted checkpoint 2 on 2026-09-19 after these artifacts were presented.


## Verification record (2026-09-19 integration)

The first complete integration run passed all formatting, Python, Rust,
dependency and build stages: 268 Rust tests passed, 25 fixture/platform-dependent
tests were explicitly ignored. Native results were 33/36 suites passing. Three
failures identified shared-metric policy literals and two older tests locating
Object Data's settings controller instead of the SQL workspace's controller.
Those tests now address the owning workspace; the final full rerun passed all
36 native suites after the visual fixes.

The owned PostgreSQL fixture passed all 32 driver tests and both disconnect tests,
including real inspection, separate SQL/object pages, transactions, deferred
values, export, cancellation and restart. No PostgreSQL test in that run was skipped.
The SQL workspace suite passed 35 cases plus one PostgreSQL fixture-dependent skip
before the final UI adjustments.

Release installation initially exposed missing historical shadcn license/provenance
inputs, also absent at the baseline. Their exact bytes were restored from local
Git revision `bf03b8c`, without changing the third-party license. Local installation
then passed resource, required notice and gallery-exclusion checks. This is local
staging, not signing, notarization or publication.

## Comparison procedure

The original browser reference retains its requested 1280×900 and 960×640 viewports.
Additional unmodified-prototype captures at 1280×957 and 960×697 provide the same
app client rectangles after removing the 57-pixel mock menu/title region. Native
captures exclude OS title/menu chrome and record Qt, OS, fonts, logical DPI and
backing device-pixel ratio. Full-resolution originals are retained. Comparison
images normalize only the recorded backing-pixel ratio to one pixel per logical
unit; they never stretch geometry to hide differences.

`scripts/design/compare_mvp_screens.py` produces side-by-side, 50% overlay and raw
RGB differences with source/output hashes and a local HTML viewer. Modal owner and
panel widgets are captured separately and explicitly composed at their recorded
coordinates. They are not represented as an OS compositor screenshot. No whole-image
similarity score is treated as acceptance.

## Flow and surface coverage

| Acceptance | Implementation and observable evidence |
| --- | --- |
| AC1–2 | Approved component foundation remains shared. Final Cocoa screen matrix covers Start, SQL ready/results/error/cancelling, five Object panes, History, all five Preferences pages, Export and New connection in Light/Dark at both client sizes. Independent visual review is complete; user checkpoint 2 accepted on 2026-09-19. |
| AC3, AC7 | Workspace/navigation tests retain two distinct drafts and original result/connection labels; history and generated SQL remain inert. |
| AC4 | Object explorer/inspection adapter suites plus SQLite/PostgreSQL fixtures cover typed properties, empty/unsupported states, retries and stale tokens. |
| AC5 | Object Data suite follows SQL → object → SQL with distinct numeric values, paging, copied bytes, exported CSV and 70,000-byte deferred values; driver tests preserve transactions and SQL cursors. |
| AC6 | SQL/Object navigation and close guards keep Cancel accessible, including immediate Cancel and successful completion racing cancellation; disconnect invalidates detail/export handles. |
| AC8 | Real profile services test save-before-connect, retained failure drafts, separate new-profile identities, queued management, protected credentials and real connection outcomes. |
| AC9 | Preferences/export native modal tests exercise pointer/keyboard input, inert parent, focus restoration, delayed persistence failures, export cleanup and staged Reset through restart. |
| AC10 | Native component/editor/navigation/modal suites cover accessible names and text, theme changes, Unicode, custom fonts, focus and minimum width. Cocoa backing scale is recorded per capture. |
| AC11 | Appearance/recovery/preferences restart fixtures retain compatible records and custom layout; recovery remains disconnected and inert. |
| AC12 | Full native/Rust gates, owned PostgreSQL tests and repeated Release probes against the clean production baseline. Final native and Release comparison results are linked below. |
| AC13 | UI-source policy, component boundaries and local Release install inspect shipped resources/notices and absence of developer gallery targets/symbols/payload. |

## Actual semantic/accessibility differences

- Native OS title/menu/file-picker surfaces are excluded from image comparisons.
  Application-modal panels center in their actual owner client rectangle; browser
  mock dialogs center in a viewport that also includes 57px of simulated OS chrome.
- SQL target selection and immutable originating document/connection labels are
  required by D1/D3 and replace the prototype's static target caption. Transaction
  controls remain available through More. Object Data adds real paging and a
  separately owned bounded result; deferred Changes controls are absent.
- Actual SQLite schema names, affinities, NULL/numeric values and DDL remain real.
  Production does not infer colored status badges from arbitrary column contents.
  Loaded-row counts and real duration replace illustrative totals/timing.
- Connection forms begin with an empty user draft, offer the real SQLite file
  picker, and expose real driver/security fields. Preferences presents supported
  settings only. Error/retry/reset controls exist where real persistence requires
  them; their semantics are covered by tests.
- SQL syntax contrast corrections: Light comment `#9ca6a7` → `#6f7879`
  (2.492:1 → 4.527:1); Light number `#b3834f` → `#936b3f`
  (3.346:1 → 4.756:1); Dark keyword `#885da7` → `#a984c8`
  (3.008:1 → 4.920:1). Light keyword and string hues remain literal. Selected
  connection-driver text uses existing `#24765e` for 4.881:1 against `#eaf4ef`
  instead of the literal 4.327:1 combination.
- Saved SQL font families/sizes remain effective. The approved shared default uses
  Qt's platform fixed font; Chromium resolves the prototype monospace fallback to
  Courier on this host. Captures record the actual native family, pixel size and
  line height so this family/rasterization difference is visible for review.
- The sidebar retains Qt's 6px draggable dock separator outside the persisted
  235/260px sidebar width. The wider hit target preserves pointer resizing and
  compatible saved widths; it shifts the content origin by 6px relative to the
  prototype's single border. This is an explicit resize-accessibility exception,
  not a claim of identical geometry.


## Final review and artifacts

- [Interactive comparison viewer](mvp-native/comparisons/index.html): 68 matched
  screen states, each with side-by-side, overlay and raw difference. Four real SQL
  error captures extend states absent from the prototype, including this
  [minimum-size error state](mvp-native/captures/light-960x640-sql-error.png).
- [Native capture manifest](mvp-native/captures/manifest.json): 100 original Cocoa
  images, both themes/sizes, actual fonts/DPR, isolated fixture, executable and
  changed-source hashes. Full-resolution pixels remain available.
- [Validation records](mvp-native/validation/full-quality.txt): all 36 native suites
  passed with Rust/Python/format/lint/dependency/build gates. A final Cocoa modern
  screen suite also passed 27 checks (the optional capture case is normally skipped)
  and the explicitly invoked capture case passed separately.
- [Release performance comparison](mvp-native/performance/README.md): matched
  1280×800/DPR2 baseline/current runs, one million rows per run and archived reread.
  Measured memory, typing, repeated startup and shutdown remained within their
  targets; one current idle-footprint sample was higher and is disclosed. The
  existing 16ms maximum GUI dispatch gate fails in both versions; cold-start, PostgreSQL performance and platform-wide
  qualification remain unverified, as stated in the report.
- [Independent review record](mvp-native/validation/reviews.md). Code review: approved after cancellation terminal-state and new
  profile identity fixes. Independent visual review: sidebar, modal and metadata
  findings closed; the final Start panel/muted-text correction passed a literal Light/Dark rendering
  regression and was re-reviewed in refreshed captures, closing the last finding.

The presented and accepted form-layout variations are: Preferences
uses a full-width theme selector, and the connection form retains the read-only
switch before its label rather than at the row's right edge. These are disclosed
variations in the accepted review package, not claims of exact matching.

The user accepted the checkpoint 2 package with “ok good, continue” on 2026-09-19,
after the final response linked the comparisons and validation/differences. This
closes the spec's required final visual review. No commit, push, deployment or
publication was performed. The existing performance-limit miss and unmeasured
release qualifications remain documented; approval does not turn them into passes.
