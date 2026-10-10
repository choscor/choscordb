# Design-system component work

Prefer to define component styles in a `.qss` file and load them from that file. Run the QSS lint check after changing styles.

Keep literal palette colors in `colors/` and reusable dimensions in `metrics/`.
Component QSS and painting code should use those semantic values. Stock Qt
widgets styled by the shared cascade remain valid components and do not need
wrapper subclasses. `docs/design/design-system.md` is the full reference.

- Tokens: `colors/colors.h` (`design::Colors`, kebab names from `colorTokens`),
  `metrics/metrics.h` (`Dimension`, `Spacing`, `Radius`, `Elevation`, `Motion`,
  `LayoutMetrics`), `fonts/fonts.h` (`TypographyRole`). One name per value; no
  aliases. A new token needs a real second use and a gallery specimen.
- Typography has seven roles: `Caption`, `Small`, `Dense`, `Body`, `Title`,
  `Mono`, `Metadata`. Do not add a role without updating the reference.
- Radii are `Small` 4, `Medium` 6 and `Large` 10; tables, trees and tab bars are
  square. Sizes sit on the 4px grid: controls 24/28/32, rows and headers 28,
  tabs and toolbar 32.
- State model: hover `surface-raised`; selected `selection` with `fg`, not bold;
  menu highlight `surface-raised`; primary → `primary-hover` →
  `primary-pressed`; 2px `ring` for keyboard focus only; status uses
  `<status>-surface` with `<status>` text.
- QSS goes through the one assembly in `style/stylesheet.cpp`. Use named
  `@token` placeholders only: no `%N` placeholders and no literal `font-size`
  or `border-radius` values. Define each selector and property pair in one file
  of the shared cascade.

When adding or changing code in a component under this directory, add or update its visible Light and Dark specimen in `desktop/tools/preview/preview_window.cpp`. Use the real component with synthetic content.

Add or update the matching check in `tests/desktop/preview_test.cpp` so it selects the specimen and verifies the real component is present in both themes.

When introducing a component family, update the specimen table in `desktop/design_system/README.md` in the same change.

Run `python3 scripts/ci/ui_consistency.py`, `python3 scripts/ci/ui_policy.py`,
and `python3 scripts/ci/qss_policy.py` after visual edits. The first reports
source-level screen coverage and rejects visual constants outside foundations;
it does not measure rendered pixels or runtime branches.
