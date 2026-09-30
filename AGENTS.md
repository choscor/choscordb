# Repository instructions

## Rust and C++ ownership

- C++/Qt owns the application frontend only: widgets, display models, user
  interaction, transient view state, Qt event-loop and native UI integration,
  and conversion at the CXX bridge. Keep that bridge small and typed.
- Rust owns application behavior beyond presentation, including the engine,
  database drivers, SQL and result rules, domain validation, durable state,
  application data and document I/O, network operations, diagnostics, exports,
  and update policy. Put the behavior in `crates/core` or the relevant Rust
  crate; do not duplicate its rules in C++ or in the bridge.
- Qt may collect a path from a native dialog or present a backend result. Pass
  the request to Rust for validation and execution. UI input hints do not
  replace Rust validation. Keep backend work off the UI thread.
- Existing C++ backend code is migration work, not precedent for new code.
  When changing it, move the affected non-frontend behavior to Rust and keep
  C++ tests focused on UI and bridge behavior. Test policy in Rust.
- After changes to C++/Qt code, the bridge, or native build dependencies, run
  `python3 scripts/ci/cpp_ownership.py`. Use `--json` to inspect the file census
  and findings. Resolve findings introduced or touched by the change, and report
  remaining pre-existing failures explicitly. Presentation exceptions must be
  narrow, documented, and reviewed against current content.
  A passing scan enforces the implemented checks; also review changed C++ for
  backend behavior that static rules cannot identify.

## Desktop UI ownership

These rules apply to the Qt Widgets UI in `desktop/app/` and `desktop/widgets/`.
For changes in `desktop/design_system/`, also follow its `AGENTS.md`.

- Reuse presentation from `desktop/design_system/`. Stock Qt controls styled by
  `ThemeManager`, `ControlStyle`, or shared QSS count as shared components; do not
  add an empty subclass just to increase the subclass count.
- Use a semantic design component when it owns behavior or presentation beyond
  a stock control.
- Keep UI workflow and transient view state in app/widgets. Keep database and
  durable state in Rust. Keep reusable appearance in the design system.
- In screen code, choose semantic roles, theme colors, typography roles, and
  spacing tokens. Do not add screen-owned QSS, literal visual colors, font
  families or sizes, or another implementation of an existing component.
- Keep user-selected SQL editor fonts as user settings.
- Use `ConfirmationDialog` for message boxes and shared dialog shells for modal
  content when their contracts fit.

## Component changes

- Before extracting a component, check the ownership map in
  `desktop/design_system/README.md` and its call sites. Extract only repeated
  presentation or behavior with the same contract; similar-looking workflow
  code may remain separate.
- Keep changes compact. Preserve object names, accessibility names, focus, and
  signal behavior.
- For each new UI component, check that its call site appears in the
  `python3 scripts/ci/ui_consistency.py --json` census. If it uses a new
  construction pattern, extend the checker and add a regression test.

## Verification checklist for UI changes

- [ ] Run `python3 scripts/ci/ui_consistency.py`. Use `--json` to inspect the
      deterministic per-screen source census and visual-ownership violations.
- [ ] Run `python3 scripts/ci/ui_policy.py` and
      `python3 scripts/ci/qss_policy.py`.
- [ ] For native UI changes, build and run relevant CTest targets. Run the full
      native suite when dependencies are present.
- [ ] For design-system edits, follow `desktop/design_system/AGENTS.md`, including
      its Light and Dark gallery specimen and matching test.

The consistency report's construction-site percentage includes explicitly
instantiated design controls and stock Qt controls covered by the shared style.
It is a static estimate, not a runtime or pixel-level claim. The canonical
quality runner includes the consistency gate and Python tests.

## Release verification

- Before preparing a release, read `.agents/skills/release-new-version/SKILL.md`
  and `docs/testing/release-lessons.md`. Run focused Windows installer and
  production-updater configuration checks before expensive packaging or tagging.
- Require GCC/MSVC native gates and production package smoke checks for the final
  commit. A passing macOS suite or diagnostic branch does not establish another
  platform's readiness. Diagnose failures; do not add blind retries, skip checks,
  or use a timeout increase as the only fix.
- Synchronize Qt tests with actual recovery/request/operation readiness. An idle
  snapshot or one event-loop turn can precede deferred work. Track shared transient
  activity by its source; retain navigation guards for every active source and
  make destruction callbacks safe during parent-window teardown.
- Keep the verified manifest, both candidate receipts, and the remote tag on one
  exact source commit. An explicitly authorized replacement of an unpublished
  tag requires an exact force-with-lease and fresh packages for all three platforms.
  Keep published tags and asset bytes immutable.
- Treat GitHub Release publication and Pages feed deployment as separate steps.
  Announce completion only after exactly three public package assets and all three
  live signed feeds have been verified for the selected version. Report a partial
  rollout honestly; record manual attestations separately from automated evidence.

## Commit messages

Use Conventional Commits. Include a type, scope, description, body, and footer,
with a blank line between each section:

```
<type>(<scope>): <description>

<body>

<footer>
```
