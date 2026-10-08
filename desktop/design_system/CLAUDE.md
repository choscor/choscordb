# Design-system component work

Prefer to define component styles in a `.qss` file and load them from that file. Run the QSS lint check after changing styles.

Keep literal palette colors in `colors/` and reusable dimensions in `metrics/`.
Component QSS and painting code should use those semantic values. Use typography
roles from `fonts/`; document a new role when an existing one does not express
the required hierarchy. Stock Qt widgets styled by the shared cascade remain
valid components and do not need wrapper subclasses.

When adding or changing code in a component under this directory, add or update its visible Light and Dark specimen in `desktop/tools/preview/preview_window.cpp`. Use the real component with synthetic content.

Add or update the matching check in `tests/desktop/preview_test.cpp` so it selects the specimen and verifies the real component is present in both themes.

When introducing a component family, update the specimen table in `desktop/design_system/README.md` in the same change.

Run `python3 scripts/ci/ui_consistency.py`, `python3 scripts/ci/ui_policy.py`,
and `python3 scripts/ci/qss_policy.py` after visual edits. The first reports
source-level screen coverage and rejects visual constants outside foundations;
it does not measure rendered pixels or runtime branches.
