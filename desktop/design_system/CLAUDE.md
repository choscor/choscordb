# Design-system component work

Prefer to define component styles in a `.qss` file and load them from that file. Run the QSS lint check after changing styles.

Keep literal palette colors in `colors/` and reusable dimensions in `metrics/`.
Component QSS and painting code should use those semantic values. Stock Qt
widgets styled by the shared cascade remain valid components and do not need
wrapper subclasses. `docs/design-system.md` is the full reference.

- Tokens: `colors/colors.h` (`design::Colors`, kebab names from `colorTokens`),
  `metrics/metrics.h` (`Dimension`, `Spacing`, `Radius`, `Elevation`, `Motion`,
  `LayoutMetrics`), `fonts/fonts.h` (`TextStyle`, `FontDesign`). One name per value; no
  aliases. A new token needs a real second use and a gallery specimen.
- Typography uses eight text styles named after Apple's macOS built-in styles:
  `LargeTitle`, `Title1`, `Title2`, `Title3`, `Headline`, `Body`, `Callout` and
  `Caption`, with `FontDesign::Monospaced` for fixed-pitch text, set in the
  bundled Geist and Geist Mono. Keep their values equal to the
  table in the reference; no style goes below Geist's 12px minimum.
- Radii are `Small` 4, `Medium` 6 and `Large` 10; tables, trees and document
  tab bars are square, except the current document tab's `Medium` top corners.
  Pane tabs are a segmented control: a `Large` track with `Medium` segments.
  Sizes sit on the 4px grid: controls 24/28/32, rows and headers 28, tabs and
  toolbar 32.
- State model: hover `surface-raised`; selected `selection` with `fg`, not bold
  (`sidebar-hover` and `sidebar-selection` on the `surface-raised` sidebar);
  menu highlight `surface-raised`; primary → `primary-hover` →
  `primary-pressed`; 2px `ring` for keyboard focus only; status uses
  `<status>-surface` with `<status>` text.
- Icons are unmodified upstream Lucide files (pinned in
  `resources/icons/SOURCE-LUCIDE.json`) at Lucide's stroke, or square, unsized
  SVG brand logos; they render only at `Dimension::IconSmall` or
  `Dimension::Icon`. `ui_policy.py` enforces this.
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
