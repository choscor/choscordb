# Repository UI rules

The desktop UI is Qt Widgets in `desktop/app/` and `desktop/widgets/`. Reuse the
presentation owned by `desktop/design_system/`. Stock Qt controls covered by
`ThemeManager`, `ControlStyle`, and shared QSS are shared components too; do not
add an empty subclass merely to raise a subclass count. Use a semantic design
component when it owns behavior or presentation beyond the stock control.

Keep workflow and database state in app/widgets, and reusable appearance in the
design system. Screen code may choose a semantic role, theme color, typography
role, or spacing token. Do not add screen-owned QSS, literal visual colors, font
families/sizes, or a second implementation of an existing component. Keep user
selected SQL editor fonts as user settings. Use `ConfirmationDialog` for message
boxes and shared dialog shells for modal content when their contract fits.

Before extracting a component, check the design-system ownership map and its
call sites. Extract only repeated presentation or behavior with the same contract;
similar-looking workflow code can remain separate. Keep changes compact and
preserve existing object names, accessibility names, focus and signal behavior.

Run `python3 scripts/ci/ui_consistency.py` for a per-screen source census and
visual-ownership violations; `--json` emits the deterministic report. Its
construction-site percentage includes explicitly instantiated design controls
and stock Qt controls covered by the shared style. It is a static estimate, not
a runtime or pixel-level claim. Run `python3 scripts/ci/ui_policy.py` and
`python3 scripts/ci/qss_policy.py` as well. The canonical quality runner includes
the consistency gate and Python tests. For native UI changes, build and run the
relevant CTest targets, then the full native suite when dependencies are present.

Design-system edits must also follow `desktop/design_system/AGENTS.md`, including
the Light and Dark gallery specimen and its test.
