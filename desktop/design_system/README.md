# Design-system component map

This library owns reusable presentation and Qt control customization. It has no
application, feature-widget, database, QScintilla or Rust dependency. Existing Qt
classes, dynamic properties, signals, input handling and visual output are
preserved by the component extraction.

## Ownership

| Component | Files and responsibility |
| --- | --- |
| Button | `button/`: semantic `Button` subclass, painting, stock QPushButton rules |
| Button group | `button_group/`: `ButtonGroup` layout and adjoining edges |
| Text | `text/`: semantic `Text` label and typography-aware sizing |
| Label, badge, keyboard hint | `label/`, `badge/`, `kbd/`: QLabel role/state styles |
| Fields | `field/`: shared single-line input, key-sequence and field-state rules; keyboard focus frame and focus painting |
| Text area | `text_area/`: QPlainTextEdit/QTextEdit rules |
| Select | `select/`: QComboBox rules and real Qt popup preparation |
| Spin box | `spin_box/`: QSpinBox/QDoubleSpinBox rules |
| Checkbox | `checkbox/`: checkbox painting and disabled-state handling |
| Switch | `switch/`: existing `designRole="switch"` track and thumb painting |
| Tool button | `tool_button/`: QToolButton states and menu-indicator rules |
| Toolbar | `toolbar/`: toolbar geometry and separators |
| Tabs | `tabs/`: pane/document variants and tab-close painting |
| Table | `table/`: table font and row styles |
| Tree | `tree/`: navigation rows and branch painting |
| List | `list/`: list item styles |
| Item view | `item_view/`: selectors genuinely shared by table/tree/list |
| Header | `header/`: header and table-corner styles |
| Scrollbar | `scrollbar/`: scrollbar dimensions and states |
| Splitter, separator | `splitter/`, `separator/`: handles and horizontal/vertical rules |
| Progress | `progress/`: progress track/chunk styles |
| Menu | `menu/`: menu styles, shadow, submenu placement and check painting |
| Tooltip | `tooltip/`: tooltip styles, painted surface and event lifecycle |
| Dock | `dock/`: existing dock-title styles |
| Dialog presentation | `dialog_presentation/`: shared backdrop, focus restoration, positioning and surface painting |
| Modal panel | `modal_panel/`: application-modal `QDialog` surface |
| Dialog shell | `dialog_shell/`: reusable nonmodal `QDialog` shell and content/status styles |
| Confirmation dialog | `confirmation_dialog/`: reusable QMessageBox contract and presentation |
| Toast region | `toast_region/`: transient/persistent notices and accessibility announcement |
| Shared glyphs | `control_glyphs/`: select/spin overlays and arrow painting; Qt retains hit testing |

Stock Qt widgets remain stock widgets. Their owning modules supply styles and,
where already present, private painting/event helpers. For example, input and
key-sequence fields share selectors in `field/`; radio/group captions share a
color rule there. They do not need empty subclasses to count as organized
components. Runtime helpers in `design::detail` are implementation details.

## Foundations and integration

- `colors/`: semantic colors, contrast and accent validation.
- `metrics/`: dimensions, spacing, radii, elevation, motion and layout metrics.
- `fonts/`: font registration and typography resolution.
- `tokens/`: discoverable token catalog used by the developer gallery.
- `icons.h/.cpp`: icon roles, resource lookup and themed rendering.
- `theme.h/.cpp`: resolved-theme value, widget-scope lookup and Qt palette.
- `theme_manager.h/.cpp`: application/root theme lifecycle.
- `platform_accessibility.h/.cpp`: platform preference integration.
- `control_style.h/.cpp`: Qt style coordinator; dispatches to component helpers,
  preserves event order and delegates unhandled primitives to the base style.
- `style/`: ordered stylesheet assembly, shared theme lookup and token expansion.
  It does not own component selectors or widget implementations.

`*_style` functions return unresolved fragments for the central assemblers; do
not apply them directly to a widget. Use `ThemeManager` for a complete themed
root. The existing property API (`variant`, `designRole`, `invalid`, etc.) stays
unchanged. Base rules precede component rules, and fragment order is deliberate:
changing order can change QSS precedence even when individual rules are unchanged.

Keep a component's presentation and private behavior together, put workflow state
in `desktop/widgets/` or application controllers, and register new source files
in `cmake/DesktopComponents.cmake`. Public headers must compile independently.
Internal consumers include the owning component header directly. The temporary
compatibility headers from the first split have been removed after verifying
that no source, test or tool uses them. `theme.h` remains the used aggregate API
for resolved themes and their foundation types.

The gallery host is `desktop/tools/preview/`. It exposes Tokens, Typography,
Icons, and Components. Every user-visible component family in the Ownership
table must have a rendered control in the gallery. Several related modules may
share one specimen; internal style, painting, and presentation helpers are
exercised through the controls that use them. The specimens use real design
system controls and styles with synthetic content. Source labels point to the
owning modules.

| Preview specimen | Component families shown |
| --- | --- |
| Typography | Text, label, keyboard hint |
| Icons | Shared icons |
| Buttons, Button groups | Button, button group |
| Tool buttons and dropdowns | Tool button, toolbar |
| Text and password fields, Shortcut entry | Field, focus indicator |
| Numeric fields | Spin box, shared control glyphs |
| Text areas and diagnostics | Text area |
| Selects and popup rows | Select, shared control glyphs |
| Checks and toggles | Checkbox, switch, radio button |
| Lists and navigation | List, tree, shared item view styles |
| Dock panel | Dock |
| Tabs with close and overflow | Tabs |
| Scroll areas and scrollbars | Scrollbar |
| Separators and splitters | Separator, splitter |
| Table headers, cells and selection | Table, header, shared item view styles |
| Tooltips and popovers | Tooltip |
| Modal panel, Nonmodal content, Destructive confirmations | Modal panel, dialog shell, confirmation dialog, shared dialog presentation |
| Menus and submenus | Menu |
| Feedback and toast states | Badge, progress, toast region |

When adding a component family or a new stock Qt control covered by an existing
family, add a visible Light/Dark specimen in `preview_window.cpp` and extend
`tests/desktop/preview_test.cpp` to select it and verify the real control is
present. Update this table in the same change. Tokens, fonts, colors, metrics,
theme, accessibility, and stylesheet assembly are foundations used by the
gallery; they are not separate controls.

## Verification

Run the development build, `ctest --preset dev`, the `choscordb-header-check`
target and `scripts/ci/ui_policy.py`. For appearance changes, also compare gallery
specimens under both themes. A source refactor should preserve the assembled QSS
and rendering; platform-specific visual/accessibility behavior still needs native
platform verification.

Extraction verification on 2026-09-13: both assembled raw stylesheet cascades
matched their pre-extraction contents exactly (8,740 control bytes and 2,238
application bytes). Of 42 offscreen Light/Dark comparison captures, 41 were
pixel-identical; the token page differed only in the source-column header
position after its source paths changed. All 33 desktop tests and four source
policy tests passed, as did standalone-header and development/release builds.
The release linker reported cached Rust objects targeting a newer macOS version;
older macOS compatibility and Windows/Linux/native accessibility were not
validated by this extraction check.
