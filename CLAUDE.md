# Repository instructions

ChoscorDB is a native, cross-platform SQL client.
It has a Qt 6 Widgets frontend in C++ and an engine in Rust.

## Repository map

- `crates/`: the Rust engine, database drivers, storage and the CXX bridge.
- `desktop/`: the Qt Widgets frontend, the design system and the bridge adapter.
- `docs/`: developer documentation. Start at `docs/README.md`.
- `user-guide/`: documentation for people who use ChoscorDB.
- `scripts/`: quality gates (`scripts/ci/`), release tools and fixtures.
- `tests/`: C++ tests and the performance harness.
- `examples/`: sample databases.

## Quality gates

Before you finish a C++/Qt change, run `python3 scripts/ci/quality.py fast`.
If you cannot run it, run at least these gates in `scripts/ci/`:

- `cpp_ownership.py`
- `ui_policy.py`
- `qss_policy.py`
- `ui_consistency.py`
- `perf_policy.py`
- `source_inventory.py`
- `docs_policy.py`

Fix the findings.
Do not add markers or exceptions to hide findings.
If an exception is necessary, give the reason in the marker or the exception entry.

## Rust and C++ ownership

- C++/Qt owns only the application frontend.
  This includes widgets, display models, user interaction and transient view state.
  It also includes the Qt event loop, native UI integration and conversion at the CXX bridge.
- Keep the CXX bridge small and typed.
- Rust owns all application behavior other than presentation.
  This includes the engine, database drivers, SQL and result rules, and domain validation.
  It also includes durable state, application data, document I/O and network operations.
  It also includes diagnostics, exports and update policy.
- Put this behavior in `crates/core` or in the applicable Rust crate.
  Do not copy its rules into C++ or into the bridge.
- Qt can collect a path from a native dialog or show a backend result.
  Send the request to Rust for validation and execution.
- UI input hints do not replace Rust validation.
- Keep backend work off the UI thread.
- Rust is the source of truth for behavior, policy, limits, persisted encodings and backend contracts.
  Expose them as typed DTOs over the CXX bridge.
- Visual tokens (colors, sizes, radii, typography) belong to the C++ frontend.
  They never cross the bridge.
- Rust is the single source of defaults, limits, budgets, intervals and user-facing policy messages.
  It is also the source of persisted encodings: IDs, context strings, file names and storage locations.
  Expose these values through the bridge.
  Do not state them again in C++.
- C++ shows the Rust errors.
  C++ does not check a rule again before or after it calls Rust.
- Do not branch on object-kind or driver-name literals in screens, models or the bridge.
  This applies to equality, list membership, prefix and comparison.
- Use `EngineAdapter::objectKindTraits` or a Rust driver DTO instead.
  The DTOs are `driverWorkflow`, `profileDriverForm` and `transactionGuard`.
  If new behavior needs a trait, add a Rust trait.
- Keep kind-to-icon and kind-to-label maps in `desktop/app/object_kind_icon.h`.
- Filter lists and trees with `TextFilter` (`desktop/bridge/text_filter.h`).
  Do not use `Qt::CaseInsensitive` matches, lowercased searches or `QSortFilterProxyModel` text filters.
- Rank and cap search results in Rust, as `QuickSearchNeedle::plan` does.
  C++ shows the rows in the order that Rust returns.
- Do not give a display row the kind of a different meaning.
  For example, a folder row is not a `"schema"`.
  Add a display kind to `object_kind_icon.h` instead.
- Never compare or search `tr()` text to decide behavior.
- Read Rust policy through cached `EngineAdapter` accessors.
- A model's `data()` or a delegate can need a trait.
  Then store the trait on the node or page when you build it.
- A UI-only number can be equal to a Rust limit.
  Then put `// ui-budget: <reason>` on its line or on the line above.
- Existing C++ backend code is migration work.
  It is not a precedent for new code.
- When you change existing C++ backend code, move the affected non-frontend behavior to Rust.
  Keep the C++ tests on UI and bridge behavior.
  Test policy in Rust.
- After changes to C++/Qt code, the bridge or native build dependencies, run `python3 scripts/ci/cpp_ownership.py`.
  Use `--json` to see the file census and the findings.
- Resolve the findings that the change adds or touches.
  Report explicitly the remaining findings that existed before the change.
- Presentation exceptions must be narrow and documented.
  Review them against the current content.
- A passing scan enforces only the implemented checks.
  Also review the changed C++ for backend behavior that static rules cannot find.
- If a review finds a new type of drift, add a failing fixture to `test_cpp_ownership.py`.
  Add a rule for it in the same change.

## Desktop UI ownership

These rules apply to the Qt Widgets UI in `desktop/app/` and `desktop/widgets/`.
For changes in `desktop/design_system/`, also obey `desktop/design_system/CLAUDE.md`.

### Design system

- `desktop/design_system/` is the single, compact source of visual tokens and components.
  `docs/design-system.md` lists the vocabulary.
- Each screen in `desktop/app/` and `desktop/widgets/` uses the design-system tokens, roles and components.
  It has no local colors, sizes, radii, font sizes or stylesheet strings.
- There is one stylesheet assembly.
  It uses only named tokens.
- A new token needs a real second use and a gallery specimen.
- Do not add aliases (two names for one value).
- Use the presentation in `desktop/design_system/` again.
  Stock Qt controls that `ThemeManager`, `ControlStyle` or shared QSS style are shared components.
  Do not add an empty subclass only to increase the subclass count.
- Use a semantic design component when it owns behavior or presentation that a stock control does not have.
- Keep UI workflow and transient view state in `app/` and `widgets/`.
  Keep database and durable state in Rust.
  Keep reusable appearance in the design system.
- In screen code, select semantic roles, theme colors, typography roles and spacing tokens.
- Do not add screen-owned QSS, literal visual colors, font families or font sizes.
  Do not add a second implementation of an existing component.
- Keep the SQL editor fonts that the user selects as user settings.
- Use `ConfirmationDialog` for message boxes.
  Use the shared dialog shells for modal content when their contracts fit.
- Keep sibling screens on one pattern:
  - Use `TextStyle::Headline` for dialog titles.
  - Use `design::DialogSections` for the dialog header, body and footer.
  - Use `design::Button` (Outline for Cancel/Close), not stock `QDialogButtonBox` buttons.
  - Use `FieldValidation` or `designRole="fieldError"` for errors.
  - Use the description role for help text.
  - Use `TextStyle::Body` with `FontDesign::Monospaced` and `designRole="codePreview"` for SQL previews.
  - Use `design::popupContextMenu` or `execContextMenu` for menus.
  - Use `design::Icon` roles, not Qt standard icons, outside macOS native menus.
- Each value that you set on `designRole`, `variant`, `state` or `designSurface` must be a value that the design system styles.
  Use a property that has no style for workflow-only state.
- Use metrics, spacing, radius and icon-size tokens in all argument positions.
  This includes paint geometry, column widths, section sizes and `themedIcon` sizes.
- Map object kinds to icons through the shared app helper.
  Do not write a switch for each screen.
- UI icons are unmodified upstream Lucide files from the release that `SOURCE-LUCIDE.json` pins.
  Each file has its upstream name and renders at the Lucide stroke width.
- Never edit or draw an icon by hand.
  To add an icon, get it from the pinned Lucide release and record its sha256.
- Only the app mark and the database logos are exempt from Lucide.
  They are SVG files with a square `viewBox` and no `width` or `height`.
  They keep their brand colors.
- Record each exempt logo in `SOURCE-DATABASE-LOGOS.json` or in the `adaptations` of `SOURCE-LUCIDE.json`.
- Do not add raster icons.
- Icons render only at `Dimension::IconSmall` or `Dimension::Icon`.
- `ui_policy.py` enforces these icon rules.

## Large-data performance

Result pages hold up to 10,000 rows.
Navigator trees, pins and history can hold tens of thousands of nodes.
On these paths, the work for each paint, keystroke or signal must not scale with the page, the selection or the tree.

- These paths are hot: paint, size hint, `data()`, `headerData()`, `flags()`, `parent()` and highlighters.
  Keep them free of bridge calls, SVG parsing and image file loads.
  Keep them free of regex compilation and linear searches.
- Calculate values before, for each page, or keep them in a cache for each theme.
- Use the cached `design::themedIcon` for icons.
- Make `parent()` and index lookup O(1).
  Keep maps from ID to node instead of tree walks.
- Walk `QItemSelection` ranges.
  Do not make a list from `selectedIndexes()`.
- Apply bulk edits for each range.
  Emit one change signal for each batch.
- Do not size result columns or rows to their contents.
  Measure a bounded sample.
- Use a member `QTimer` to debounce keystroke-driven filters and searches.
- Use one single-shot member timer to join bursts of model signals.
  Do not use one zero-delay timer for each signal.
- Keep navigator trees at uniform row heights.
  On hover, repaint only the changed rectangles.
  Expand only known ancestors.
- Filter, sort, search and visibility policy belong in Rust.
  C++ applies the result to the view.
- Run `python3 scripts/ci/perf_policy.py`.
  Use `--json` to see the findings.
- Exempt a line only with a reviewed `// perf-ok: <reason>` marker on it or on the line above.

## Dead and duplicated code

- When the last production caller of code goes, delete the code.
  Do not keep wrappers, legacy paths, unused parameters (`Q_UNUSED`) or unconnected signals for tests.
- Move tests to the production API.
  If they test policy, move them to Rust.
- Share helpers instead of copies.
  Use `desktop/bridge/rust_text.h` for `rust::String`/`QString` conversion.
  Use `desktop/bridge/request_token.h` for request tokens.
  Counters in each file with selected start values can collide.
- Run `python3 scripts/ci/source_inventory.py`.
  It rejects sources that CMake does not list and headers that no file includes.
  It also rejects uncalled members, unconnected signals, copied anonymous-namespace helpers and stale `ui_consistency.py` list names.
- Record a reviewed exception in its `EXCEPTIONS` map only for a deliberate seam.

## Component changes

- Before you extract a component, read the ownership map in `desktop/design_system/README.md`.
  Also read the call sites.
- Extract only repeated presentation or behavior that has the same contract.
  Workflow code that looks similar can stay separate.
- Keep changes compact.
  Keep object names, accessibility names, focus and signal behavior.
- For each new UI component, make sure that its call site is in the census of `python3 scripts/ci/ui_consistency.py --json`.
  If it uses a new construction pattern, extend the checker and add a regression test.

## Verification checklist for UI changes

- [ ] Run `python3 scripts/ci/ui_consistency.py`.
      Use `--json` to see the census of each screen and the visual-ownership violations.
- [ ] Run `python3 scripts/ci/ui_policy.py` and `python3 scripts/ci/qss_policy.py`.
      New audit rules accept a reviewed `// ui-ok: <reason>` marker only where no design token or component fits.
- [ ] Run `python3 scripts/ci/perf_policy.py` and `python3 scripts/ci/source_inventory.py`.
- [ ] For native UI changes, build and run the applicable CTest targets.
      If the dependencies are available, run the full native suite.
- [ ] For design-system edits, obey `desktop/design_system/CLAUDE.md`.
      This includes its Light and Dark gallery specimen and the matching test.

The construction-site percentage in the consistency report includes design controls that the code creates.
It also includes stock Qt controls that the shared style covers.
It is a static estimate, not a runtime or pixel-level measurement.
The canonical quality runner includes the consistency gate and the Python tests.

## Documentation

- A change that alters documented behavior must update the owning page in the same change.
  The map in `docs/README.md` gives the owning page for each code path.
- Write documentation in ASD-STE100 Simplified Technical English.
  Obey `docs/style-guide.md`.
- If a mapped change needs no documentation update, add `Docs: none needed, <reason>` to the pull request body or a commit message.
- Run `python3 scripts/ci/docs_policy.py`.
  It checks links and sentence length.
  In pull requests, CI also runs it with `--changed` to check the owning pages.

## Release verification

- Before you prepare a release, read `.claude/skills/release-new-version/SKILL.md` and `docs/release-lessons.md`.
  The procedures are in `docs/release-macos.md` and `docs/release-windows-linux.md`.
- Run the focused Windows installer and production-updater configuration checks first.
  Run them before expensive packaging or tagging.
- The final commit needs the GCC and MSVC native gates and the production package smoke checks.
  A passing macOS suite or a diagnostic branch does not show that another platform is ready.
- Find the cause of each failure.
  Do not add blind retries, skip checks or only increase a timeout.
- Synchronize Qt tests with the real recovery, request or operation readiness.
  An idle snapshot or one event-loop turn can come before deferred work.
- Track shared transient activity by its source.
  Keep the navigation guards for each active source.
  Make destruction callbacks safe during parent-window teardown.
- Keep the verified manifest, both candidate receipts and the remote tag on one exact source commit.
- An explicitly authorized replacement of an unpublished tag needs an exact force-with-lease.
  It also needs new packages for all three platforms.
- Do not change published tags or asset bytes.
- GitHub Release publication and Pages feed deployment are separate steps.
- Announce completion only after you verify the selected version.
  Verify exactly three public package assets and all three live signed feeds.
- Report a partial rollout honestly.
  Record manual attestations separately from automated evidence.

## Commit messages

Use Conventional Commits.
