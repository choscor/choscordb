# Design-system component work

Prefer to define component styles in a `.qss` file and load them from that file. Run the QSS lint check after changing styles.

When adding or changing code in a component under this directory, add or update its visible Light and Dark specimen in `desktop/tools/preview/preview_window.cpp`. Use the real component with synthetic content.

Add or update the matching check in `tests/desktop/preview_test.cpp` so it selects the specimen and verifies the real component is present in both themes.

When introducing a component family, update the specimen table in `desktop/design_system/README.md` in the same change.
