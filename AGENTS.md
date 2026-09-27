# Repository instructions

## Desktop UI ownership

These rules apply to the Qt Widgets UI in `desktop/app/` and `desktop/widgets/`.
For changes in `desktop/design_system/`, also follow its `AGENTS.md`.

- Reuse presentation from `desktop/design_system/`. Stock Qt controls styled by
  `ThemeManager`, `ControlStyle`, or shared QSS count as shared components; do not
  add an empty subclass just to increase the subclass count.
- Use a semantic design component when it owns behavior or presentation beyond
  a stock control.
- Keep workflow and database state in app/widgets. Keep reusable appearance in
  the design system.
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

## Commit messages

Use Conventional Commits. Include a type, scope, description, body, and footer,
with a blank line between each section:

```
<type>(<scope>): <description>

<body>

<footer>
```
