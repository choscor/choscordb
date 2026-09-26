# Desktop UI consistency audit — 2026-09-26

`python3 scripts/ci/ui_consistency.py --json` is the reproducible source census.
It counts direct `new` sites for leaf visual controls in `desktop/app/` and
`desktop/widgets/`. A site is covered when it constructs a design-system control
or a stock Qt control whose presentation is owned by `ThemeManager`,
`ControlStyle`, and the shared QSS. Structural containers, feature composites,
dynamic factories, and runtime branches are outside this denominator. Thirteen
of 26 source groups contain counted leaf sites; the other groups are reported
as N/A, including the custom SQL editor and controller modules. A 100%
value here is **construction-site coverage**, not proof of rendered or runtime
coverage.

| Screen group | Design controls | Shared styled Qt controls | Covered sites |
| --- | ---: | ---: | ---: |
| Main window | 24 | 18 | 42/42 |
| Object data workspace | 1 | 5 | 6/6 |
| Object explorer | 4 | 2 | 6/6 |
| Query workspace | 0 | 2 | 2/2 |
| Result filter bar | 2 | 7 | 9/9 |
| Export dialog | 6 | 6 | 12/12 |
| History dock | 9 | 7 | 16/16 |
| Preferences dialog | 7 | 10 | 17/17 |
| Profile dialog and SSH subviews | 15 | 46 | 61/61 |
| Query settings dialog | 4 | 2 | 6/6 |
| Search panel | 6 | 4 | 10/10 |
| Sidebar section | 1 | 0 | 1/1 |
| Value detail dialog | 4 | 1 | 5/5 |
| **Total** | **83** | **110** | **193/193** |

The source scan found no unclassified leaf-control construction sites, local
screen QSS, literal screen colors or font sizes, or raw message-box construction.
The DDL editor now uses a shared text-area role; the pending-edits and grid-review
dialogs use shared shells; Preferences and Export use `DialogSections` for their
common header, body, separator and footer structure. Their dialog-specific
controls and insets remain in each screen. Toast surfaces are semantic color tokens, and
component-specific small fonts are resolved through typography roles. The
existing stock controls are intentional: wrapping every `QLabel`, `QComboBox`,
or view would add code without giving presentation one clearer owner.

The component inventory and call-site review found no second Button, tree, or
confirmation component implementation. Similar SQL and history delegates have
different model contracts, so they remain separate. This is a reviewed source
finding, not a claim that a deterministic scan can prove all semantic code is
duplicate free. The quality gate prevents the observable recurrence paths it can
identify: feature QSS and visual literals, unclassified Qt controls, direct
message boxes, and font-size overrides outside typography foundations.

Light and Dark gallery specimens and offscreen Qt tests check the shared visual
paths. Offscreen tests do not establish native window-manager rendering or
accessibility behavior on every platform.
