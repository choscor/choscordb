# Repository instructions

ChoscorDB is a native, cross-platform SQL client.
The desktop frontend uses Qt 6 Widgets in C++. The Rust engine contains all other code.
Write this file in ASD-STE100 Simplified Technical English, as `docs/style-guide.md` specifies.

## Repository map

- `crates/`: the Rust engine, the drivers, storage and the CXX bridge.
- `desktop/`: the desktop frontend, the design system and the bridge adapter.
- `docs/`: developer documentation. Start at `docs/README.md`.
- `user-guide/`: documentation for the users of ChoscorDB.
- `scripts/`: quality gates (`scripts/ci/`), release tools and fixtures.
- `tests/`: C++ tests and the performance harness.
- `examples/`: sample databases.

## Core principles

- Keep each layer in its position. A layer uses only the layers below it.
- Give one feature the same behavior and the same UI in all screens and drivers.
- Write each rule, helper and component one time. Use it again. Do not copy it.
- Use C++/Qt only for the UI. Put all behavior in safe Rust.
- Use unsafe Rust only at FFI boundaries. Isolate each unsafe part.
- Make each failure loud. Do not hide a failure.

## Layers

`docs/architecture.md` gives the crate map and the dependencies.
The layers are in this order, from the bottom to the top:

1. `driver-api`: the contracts and traits for drivers and services.
2. The drivers and the services, for example `storage`, `export` and `result-cache`.
3. `core`: the composition of the services and the connection actors.
4. `bridge`: the CXX bridge and the composition root.
5. `desktop/bridge`: `EngineAdapter` and the conversion of DTOs.
6. `desktop/models`: the display models.
7. `desktop/widgets` and then `desktop/app`: the screens.

- Do not include or import a higher layer from a lower layer.
- Do not skip a layer. Screens use the Rust engine only through `EngineAdapter`.
- `core` uses only `driver-api`. Only `bridge` creates the drivers.
- `desktop/design_system` is below all screens. It does not use `app`, `widgets`, `models` or `bridge`.
- If two layers need the same type, move the type down to the lower layer.
- Do not add a cycle between crates, directories or files.

## Rust engine and desktop frontend

The Rust engine owns all behavior. The desktop frontend owns only presentation.

- The Rust engine owns the drivers, SQL rules, result rules and domain validation.
- The Rust engine owns stored data, document I/O, network, diagnostics, exports and update policy.
- The Rust engine owns defaults, limits, budgets, intervals and policy messages.
- The Rust engine owns stored encodings: IDs, context strings, file names and storage locations.
- The Rust engine owns the policy to filter, sort, search, rank, cap and show rows.
- The desktop frontend shows rows in the order that the Rust engine returns.
- The desktop frontend owns widgets, display models, user input, transient view state and the event loop.
- The desktop frontend collects input and shows results and errors.
- The Rust engine validates and executes each request.
- Do not check a Rust rule again in C++. A UI input hint is not a validation.
- Keep Rust engine work off the UI thread.
- Keep the CXX bridge small and typed.
- Do not send visual tokens across the bridge.
- Read Rust values through the cached `EngineAdapter` accessors. Do not copy the values into C++.
- If a UI-only number is equal to a Rust limit, put `// ui-budget: <reason>` on or above its line.
- Do not compare object-kind or driver-name literals in screens, models or the bridge.
- Use `EngineAdapter::objectKindTraits` or the `driverWorkflow`, `profileDriverForm` and `transactionGuard` DTOs.
- If new behavior needs a trait, add a Rust trait.
- If `data()` or a delegate needs a trait, store the trait on the node or page when you build it.
- Keep the maps from kind to icon and from kind to label in `desktop/app/object_kind_icon.h`.
- If a display row has a new meaning, add a display kind. A folder row is not a `"schema"`.
- Filter lists and trees with `TextFilter` (`desktop/bridge/text_filter.h`).
- Do not use case-insensitive matches, lowercase searches or proxy-model text filters.
- Do not use `tr()` text to make a decision.
- Existing C++ engine code is migration work. Do not use it as an example.
- When you change C++ engine code, move its behavior to the Rust engine.
- Test policy in Rust. Test only UI and bridge behavior in C++.
- Examine changed C++ for engine behavior that `cpp_ownership.py` cannot find.
- If you find a new type of drift, add a rule and a failing fixture in `test_cpp_ownership.py`.

## Safe Rust

The workspace sets `unsafe_code = "deny"` in `Cargo.toml`.

- Write all Rust engine code as safe Rust.
- Use `unsafe` only at an FFI boundary. Examples are the `cxx::bridge` module and operating-system calls.
- Put each unsafe part in one small function or module. Give it a safe API.
- Do not let unsafe code or raw pointers go out of that function or module.
- Put `#[allow(unsafe_code, reason = "<reason>")]` on the smallest item that needs it.
- Put a `// SAFETY:` comment above each `unsafe` block. Give the invariant that makes it safe.
- Add a test for each unsafe part.
- If a safe crate or a safe API can do the work, use it. Do not write unsafe code.
- Do not change the workspace lint to allow unsafe code.

## Failures

Make each failure loud and visible.

- Return a typed error to the caller. Do not ignore a `Result` or an error code.
- Do not replace a failure with a default value, an empty result or a log line only.
- Do not use `unwrap_or_default`, `ok()` or `let _ =` to drop an error that can occur.
- Do not panic for a failure that can occur at run time. Return an error.
- Do not let a panic cross the bridge.
- The desktop frontend shows each Rust error to the user.
- Do not show a success message for a failed or partial operation.
- If a quality gate or a test fails, find the cause. Do not retry it blindly.
- Do not skip a check or only increase a timeout to make it pass.

## Desktop UI

These rules apply to `desktop/app/` and `desktop/widgets/`.
For `desktop/design_system/`, also obey `desktop/design_system/CLAUDE.md`.
For the vocabulary and the component map, read `docs/design-system.md` and `desktop/design_system/README.md`.

- Give one feature the same UI, labels, shortcuts and behavior on each screen.
- Before you add a screen, find a sibling screen. Use its pattern.
- `desktop/design_system/` is the single source of tokens, components and the stylesheet.
- Do not put local colors, sizes, radii, fonts or stylesheet strings in screens.
- The SQL editor fonts that the user selects are user settings. They are not tokens.
- Use tokens for all sizes. This includes paint geometry, column widths and icon sizes.
- Set `designRole`, `variant`, `state` and `designSurface` only to values that the design system styles.
- Add a new token only for a real second use. Add a gallery specimen for it.
- Do not add two names for one value.
- Use the existing components again. Do not add copies or empty subclasses.
- A stock control that the shared style covers is a shared component.
- Extract a component only for repeated code that has the same contract.
- Keep the object names, accessibility names, focus and signals.
- Make sure that the `ui_consistency.py --json` census includes each new component call site.
- Use one pattern for sibling screens:
  - For dialogs, use a `TextStyle::Headline` title, `design::DialogSections` and `design::Button`.
  - For Cancel and Close, use the Outline button. Do not use `QDialogButtonBox` buttons.
  - For message boxes, use `ConfirmationDialog`. For modal content, use the shared dialog shells.
  - For errors, use `FieldValidation` or `designRole="fieldError"`.
  - For help text, use the description role.
  - For SQL previews, use `TextStyle::Body`, `FontDesign::Monospaced` and `designRole="codePreview"`.
  - For menus, use `design::popupContextMenu` or `execContextMenu`.
- For icons, obey these rules:
  - Use `design::Icon` roles. Use Qt standard icons only in macOS native menus.
  - Use unchanged Lucide files from the release that `SOURCE-LUCIDE.json` pins.
  - Record the sha256 of each icon file.
  - Do not edit or draw an icon. Do not add raster icons.
  - Only the app mark and the database logos can be other files.
  - Record each logo in `SOURCE-DATABASE-LOGOS.json`.
  - A logo has a square `viewBox`, no `width` or `height`, and its brand colors.
  - Show icons only at `Dimension::IconSmall` or `Dimension::Icon` through `design::themedIcon`.

## Driver consistency

- Give each driver the same workflow for one feature.
- Put the differences between drivers in Rust driver traits and DTOs.
- Do not add a feature to one driver only. If a driver cannot support it, return a typed Rust error or trait.

## Large-data performance

A result page has up to 10,000 rows.
The navigator, pins and history can have tens of thousands of nodes.
The work for each paint, keystroke or signal must not increase with the quantity of data.

- These paths are hot: paint, size hint, `data()`, `headerData()`, `flags()`, `parent()` and highlighters.
- Do not put bridge calls, SVG parse, file loads, regex compilation or linear searches in hot paths.
- Calculate values before you need them, for each page. Or keep them in a cache for each theme.
- Use maps from ID to node. Then `parent()` and index lookup are O(1).
- Use `QItemSelection` ranges. Do not use `selectedIndexes()`.
- Apply bulk edits for each range. Send one signal for each batch.
- Measure a limited sample of rows to size result columns and rows.
- Use a member `QTimer` to delay filters and searches until the user stops to type.
- Use one single-shot member timer to join a burst of model signals.
- Keep all navigator rows at one height.
- On hover, paint again only the changed rectangles.
- Expand only known ancestors.

## Dead and duplicated code

- Before you write code, search for an existing helper, component or Rust function. Use it.
- Write each rule in one location. Do not write a rule again in a different layer or language.
- If two copies have the same contract, merge them into one shared helper.
- When code has no production caller, delete it.
- Do not keep wrappers, old paths, unused parameters or signals only for tests.
- Make tests use the production API.
- Use `desktop/bridge/rust_text.h` to convert strings.
- Use `desktop/bridge/request_token.h` for request tokens.
- `source_inventory.py` finds unlisted, unused and copied code.
- Add an `EXCEPTIONS` entry only for a deliberate seam.

## Quality gates

- Before you complete a C++ change, run `python3 scripts/ci/quality.py fast`.
- If you cannot run it, run these quality gates in `scripts/ci/`:
  `cpp_ownership.py`, `ui_policy.py`, `qss_policy.py`, `ui_consistency.py`,
  `perf_policy.py`, `source_inventory.py` and `docs_policy.py`.
- To see the details, add `--json`.
- Before you complete a Rust change, run `cargo clippy --workspace --all-targets --locked` and `cargo test --workspace --locked`.
- Fix the findings. Do not hide findings with markers or exceptions.
- Give a reason in each necessary marker (`ui-ok`, `perf-ok`, `ui-budget`) or exception.
- Use `// ui-ok: <reason>` only where no token or component is applicable.
- Fix the findings that your change adds or touches. Report the older findings.
- For native changes, build and run the applicable CTest targets.
- If the dependencies are available, run the full native suite.

## Documentation

- Put developer documentation in `docs/`.
- Put documentation for the users of ChoscorDB in `user-guide/`.
- Update the owning page in the same change. `docs/README.md` gives the owning page for each code path.
- If a change needs no update, add `Docs: none needed, <reason>` to the PR body or a commit message.
- Write in ASD-STE100 Simplified Technical English. Obey `docs/style-guide.md`.

## Release verification

Before you start a release, read `.claude/skills/release-new-version/SKILL.md` and `docs/release-lessons.md`.

- Before you package or tag, check the Windows installer and updater configuration.
- The final commit must pass the GCC and MSVC native gates and the package smoke checks.
- A macOS pass does not show that the other platforms are ready.
- Keep the manifest, the two receipts and the remote tag on one exact commit.
- Do not change published tags or asset bytes.
- To replace an unpublished tag, get explicit approval. Use an exact force-with-lease.
- After you replace a tag, make new packages for all three platforms.
- GitHub Release publication and Pages feed deployment are two separate steps.
- Verify three public assets and three live signed feeds. Then announce the release.
- If a rollout is partial, report it.
- Keep manual attestations separate from automated evidence.

## Commit messages

Use Conventional Commits.
