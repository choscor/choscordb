# Qt component architecture

Research date: 2026-09-13. This document separates observed upstream patterns from recommendations for ChoscorDB. It describes source architecture, not a performance comparison or a claim that Telegram's complete UI uses one rendering mechanism.

## Recommendation for this repository

Keep the existing hybrid: Qt semantic controls for interaction, typed design tokens for appearance, `QProxyStyle` for shared control details, and custom painting inside individual components when necessary. Organize each public component into its own header and implementation. Keep feature widgets separate from reusable visual primitives, and enforce that distinction with CMake targets.

The inspected starting point already has useful boundaries: `Button` derives from `QPushButton`, `ControlStyle` derives from `QProxyStyle`, `ThemeManager` resolves appearance and accessibility policy, and `DialogShell` composes `DialogPresentation`. However, the root CMake target combines design foundations, feature widgets, models, application code, QScintilla, and the Rust bridge. `components.h/.cpp` also combines Button and ButtonGroup. Separation should clarify these responsibilities without rewriting their behavior.

| Layer | Responsibility | Dependency rule |
| --- | --- | --- |
| Design foundations | Semantic colors, metrics, typography, icons, theme resolution, platform appearance/accessibility integration | Qt and platform facilities; no SQL, profiles, editor, or application controllers |
| Design components | Button, ButtonGroup, modal presentation and future reusable controls | Foundations and Qt; no feature widgets |
| Feature widgets | SQL editor, completion, search, history, preferences and other workflow dialogs | Design components, required models/services and editor integration |
| Application | Workspace composition, controller wiring, persistence coordination | Feature widgets and services |
| Preview/gallery | Demonstrations and component state coverage | Design components; isolate application-specific demonstrations |

A component directory should contain the component's `.h/.cpp` pair and any private painting helpers. A foundation module such as theme may appropriately contain several closely related value types; creating a file for every enum does not improve the boundary. Headers should expose the smallest useful API and forward declare implementation dependencies. Keep temporary compatibility include facades where required by existing consumers, then migrate internal includes to the owning component.

Use a design-system library and a small set of libraries for services, feature widgets, application composition and the developer preview (see the [implemented target map](README.md#component-organization)). A CMake target per button or dialog would add maintenance without enforcing a useful additional dependency boundary. The design-system library must build without linking the Rust bridge or QScintilla. Pure component tests should link that library directly; the existing gallery includes feature specimens and therefore links the widget layer. Register all moved sources with the repository's quality-target helpers, and verify static Qt resource registration after moving the resource collection between targets. These are project recommendations, not Telegram implementation claims.

## What Telegram actually does

The inspected Telegram Desktop revision is [`272f6f5`](https://github.com/telegramdesktop/tdesktop/tree/272f6f5c2d29d8cdb3aec15907d616b87451a3ca). Its [submodule configuration](https://github.com/telegramdesktop/tdesktop/blob/272f6f5c2d29d8cdb3aec15907d616b87451a3ca/.gitmodules) separates `lib_ui`, `lib_base`, `lib_rpl`, and code generation. That revision points at `lib_ui` revision `09f35036fd4f2d2f67422d294740f9a92c38827f`. The detailed library observations below use the independently inspected `lib_ui` master snapshot [`0b7594e`](https://github.com/desktop-app/lib_ui/tree/0b7594e385d647b40b3ed6898b6c7f0742ea6a13), rather than implying those two library revisions are identical.

- **A real UI library boundary.** `lib_ui` declares a static library, exposes a `desktop-app::lib_ui` alias, enables AUTOMOC, and registers reusable widgets, painting helpers, effects, styles, accessibility, and platform integrations. Its build generates styles and a palette from input files. [Library CMake](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/CMakeLists.txt).
- **Typed component appearance.** `widgets.style` defines structures such as `RoundButton`, with dimensions, padding, colors, text style, icons, and ripple configuration. Button constructors accept corresponding `style::...` references. This is generated C++ style data, distinct from Qt Style Sheets. [Style definitions](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/ui/widgets/widgets.style), [button API](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/ui/widgets/buttons.h).
- **Shared custom behavior and painting.** The inspected button family builds on `AbstractButton` and `RippleButton`. `FlatButton::paintEvent` paints its background, ripple and text; `RoundButton` shares ripple behavior and manages its own visual configuration. Palette changes can invalidate an active ripple. [Button implementation](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/ui/widgets/buttons.cpp).
- **Theme data has an explicit lifecycle.** The palette API supports named color updates, loading, finalization and application. That is a concrete separation between theme data and widget implementation. [Palette API](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/ui/style/style_core_palette.h).
- **Lifetime and accessibility belong to the toolkit.** `RpWidget` exposes geometry/visibility event producers and `lifetime()`, plus accessibility roles, names, states and actions. `RoundButton` binds text updates to its lifetime. The library also provides a `GenericBox` composition API. These mechanisms are part of the cost of operating a custom widget toolkit. [RpWidget](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/ui/rp_widget.h), [GenericBox](https://github.com/desktop-app/lib_ui/blob/0b7594e385d647b40b3ed6898b6c7f0742ea6a13/ui/layers/generic_box.h).

The useful architectural inference is to adopt typed appearance, reusable components, explicit dependency boundaries and lifetime discipline. This repository already has ordinary Qt signals/slots and token types; a style compiler or a new reactive framework is not required to obtain those benefits.

## Choosing a Qt customization mechanism

| Mechanism | Recommended use here | Constraint |
| --- | --- | --- |
| Palette and fonts | Shared semantic colors, disabled/active groups, typography | Some platform styles draw elements independently of palette choices |
| `QProxyStyle` | Focus primitives, common metrics and style hints through `ControlStyle` | Delegate unhandled cases; proxy behavior depends on the underlying style |
| Semantic Qt subclass | Button variants and reusable input behavior | Preserve base-class input, shortcut, focus and state semantics |
| Component-local painting | Branded button surfaces and decorations that need precise control | Own rendering of every relevant state and geometry change |
| Qt Style Sheets | Small, centrally owned rules for supported stock-widget appearance | Cascading precedence and dynamic-property changes need deliberate handling |
| Composition | Dialogs, toolbars, forms, search panels | Keep workflow state out of reusable visual controls |

`QProxyStyle` is designed to override selected style elements; Qt explicitly notes that proxy overrides are not guaranteed for user/system styles, and that macOS system-controlled menus are outside this mechanism. Test the selected base style on supported platforms. [Qt QProxyStyle](https://doc.qt.io/qt-6/qproxystyle.html).

Deriving from `QPushButton` retains the Qt button model, including checked/down states, shortcuts and signals. Overriding its painting does not eliminate the need to render these states correctly. Reuse `QStyleOptionButton` state where possible. Avoid rebuilding a standard control directly from `QWidget` solely to change its background. [Qt QAbstractButton](https://doc.qt.io/qt-6/qabstractbutton.html).

Qt Style Sheets are an official customization mechanism. Keep their rules in the design layer and scope feature-specific selectors deliberately; avoid scattering `setStyleSheet()` fragments through dialogs. Property changes may require style recomputation, and cascade specificity can make a local override win over the application theme. Do not make both a component painter and QSS independently responsible for its same surface. [Qt Style Sheets](https://doc.qt.io/qt-6/stylesheet.html), [stylesheet syntax and cascading](https://doc.qt.io/qt-6/stylesheet-syntax.html).

## Component contracts and verification

For each reusable control, define its variants, size policy, keyboard behavior, accessible name and disabled/loading behavior. A button group owns layout/composition; a button owns button visuals; the dialog owns its submit/cancel workflow. Continue using `ThemeManager` as the policy entry point and resolve colors from the widget/root theme so independent previews remain possible.

Treat theme/font/metric changes as invalidation: repaint colors and icons, clear relevant cached graphics, and call `updateGeometry()` when size hints change. Paint within `paintEvent()` and prefer scheduled `update()` to immediate repeated repainting. Qt documents both its paint-event contract and geometry notifications. [Qt QWidget](https://doc.qt.io/qt-6/qwidget.html).

Use QObject parents for child widgets and supply a receiver/context object for signal-to-lambda connections. A context-bound connection disconnects when its context is destroyed; separately captured objects still require valid lifetimes, for example through `QPointer`. [Qt signals and slots](https://doc.qt.io/qt-6/signalsandslots.html).

Prefer existing Qt accessible controls, give icon-only buttons explicit accessible names, and verify focus order and keyboard activation after composition changes. Custom semantic roles/actions may need `QAccessibleWidget` or another appropriate interface; painted pixels alone do not expose them. [Qt accessibility for widgets](https://doc.qt.io/qt-6/accessible-qwidget.html).

For the component separation, run existing design-system and widget regression suites and build the gallery against the extracted widget layer. For future visual changes, cover light/dark themes, contrast policy, enabled/disabled/loading/checked states, keyboard focus, narrow layouts, font changes, right-to-left layout and multiple display scales. Automated behavior checks complement native platform and assistive-technology checks; source inspection and an offscreen test run do not establish those visual results.
