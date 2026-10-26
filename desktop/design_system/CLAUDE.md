# Design-system component work

Write this file in ASD-STE100 Simplified Technical English, as `docs/style-guide.md` specifies.
`docs/design-system.md` is the full reference.

## Tokens

- Keep literal palette colors in `colors/`. Keep reusable dimensions in `metrics/`.
- Component QSS and painting code use only those semantic values.
- A stock Qt widget that the shared cascade styles is a valid component. It needs no wrapper subclass.
- The token headers are:
  - `colors/colors.h`: `design::Colors`, with the kebab names from `colorTokens`.
  - `metrics/metrics.h`: `Dimension`, `Spacing`, `Radius`, `Elevation`, `Motion` and `LayoutMetrics`.
  - `fonts/fonts.h`: `TextStyle` and `FontDesign`.
- Give each value one name. Do not add aliases.
- Add a new token only for a real second use. Add a gallery specimen for it.

## Typography

- Use the eight text styles. Their names come from the macOS built-in styles:
  `LargeTitle`, `Title1`, `Title2`, `Title3`, `Headline`, `Body`, `Callout` and `Caption`.
- Use `FontDesign::Monospaced` for fixed-pitch text.
- The styles use the bundled Geist and Geist Mono fonts.
- Keep the style values equal to the table in the reference.
- Do not make a style smaller than 12px, the Geist minimum.

## Shapes and sizes

- The radii are `Small` 4, `Medium` 6 and `Large` 10.
- Tables, trees and document tab bars are square.
  Only the current document tab has `Medium` top corners.
- Pane tabs are a segmented control: a `Large` track with `Medium` segments.
- Put sizes on the 4px grid.
  Controls are 24, 28 or 32. Rows and headers are 28. Tabs and the toolbar are 32.

## States

- Hover uses `surface-raised`.
- Selection uses `selection` with `fg`. Do not make selected text bold.
- Navigator rows use `sidebar-hover` and `sidebar-selection` on the `surface-raised` surface.
- The menu highlight uses `surface-raised`.
- A primary control goes from `primary` to `primary-hover` to `primary-pressed`.
- Use the 2px `ring` only for keyboard focus.
- A status uses `<status>-surface` with `<status>` text.

## Icons

- Use unchanged upstream Lucide files at the Lucide stroke.
  `desktop/resources/icons/SOURCE-LUCIDE.json` pins the release.
- A brand logo is a square SVG with no size.
- Show icons only at `Dimension::IconSmall` or `Dimension::Icon`. `ui_policy.py` enforces this.

## Stylesheets

- If possible, put the styles of a component in a `.qss` file. Load them from that file.
- Send all QSS through the one assembly in `style/stylesheet.cpp`.
- Use only named `@token` placeholders.
  Do not use `%N` placeholders or literal `font-size` or `border-radius` values.
- Define each pair of selector and property in one file of the shared cascade.

## Specimens and tests

When you add or change a component in this directory, do these steps:

1. Add or update its Light and Dark specimen in `desktop/tools/preview/preview_window.cpp`.
   Use the real component with synthetic content.
2. Add or update the check in `tests/desktop/preview_test.cpp`.
   The check selects the specimen and finds the real component in the two themes.
3. If you add a component family, update the specimen table in `desktop/design_system/README.md`.
4. After a visual change, run `python3 scripts/ci/ui_consistency.py`, `python3 scripts/ci/ui_policy.py` and `python3 scripts/ci/qss_policy.py`.

`ui_consistency.py` reports the screen coverage of the source.
It also rejects visual constants outside the foundations.
It does not measure rendered pixels or runtime branches.
