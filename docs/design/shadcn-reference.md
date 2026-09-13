# Pinned shadcn reference

Selected 2026-09-12: **Base UI / Nova / Neutral**, default radius, Geist,
Lucide, subtle menu accent, default menu color. The official theming example
explicitly selects `base-nova` and `neutral`; `DEFAULT_PRESETS.nova` selects Geist
and Lucide. This resolves the ambiguity among the current configurable styles;
older New York and Radix examples are not mixed into this reference.

The immutable [upstream revision](https://github.com/shadcn-ui/ui/tree/2b3e6d4f8d9161fe5c19340dc383aade392012dd)
is `2b3e6d4f8d9161fe5c19340dc383aade392012dd`.
[Machine-readable provenance](shadcn-reference/manifest.json) records source paths
and SHA-256 checksums. Cached MIT-licensed sources and the license are adjacent.
The official [theming documentation](https://ui.shadcn.com/docs/theming) and
[Base button documentation](https://ui.shadcn.com/docs/components/base/button)
were inspected; subsequent website changes do not update this pin.

| Source at pinned revision | Purpose |
| --- | --- |
| `apps/v4/content/docs/(root)/theming.mdx` | Full light/dark semantic CSS and radius scale |
| `apps/v4/registry/styles/style-nova.css` | All Nova component dimensions and states |
| `apps/v4/registry/bases/base/ui/button.tsx` | Button variants, sizes, disabled semantics |
| `apps/v4/registry/bases/base/ui/<component>.tsx` | Corresponding input, checkbox, dialog, menu, table, tabs, scroll-area, separator, tooltip, badge, field, toggle structure |
| `packages/shadcn/src/preset/defaults.ts` | Selected preset identity |
| `apps/v4/lib/fonts.ts` | Default UI and heading Geist family |

## Color mapping

`oklch` maps to sRGB by OKLab conversion, gamut clipping, then nearest 8-bit
channel; preserve alpha until composition over the actual surface. Do not flatten
dark borders against background when rendering a card/popover. The exact CSS
values remain authoritative in [theming.mdx](shadcn-reference/theming.mdx).

| Role | Light sRGB | Dark sRGB |
| --- | --- | --- |
| background / foreground | #ffffff / #0a0a0a | #0a0a0a / #fafafa |
| card / card-foreground | #ffffff / #0a0a0a | #171717 / #fafafa |
| popover / popover-foreground | #ffffff / #0a0a0a | #171717 / #fafafa |
| primary / primary-foreground | #171717 / #fafafa | #e5e5e5 / #171717 |
| secondary / secondary-foreground | #f5f5f5 / #171717 | #262626 / #fafafa |
| muted / muted-foreground | #f5f5f5 / #737373 | #262626 / #a3a3a3 |
| accent / accent-foreground | #f5f5f5 / #171717 | #262626 / #fafafa |
| destructive | #e7000b | #ff6467 |
| border | #e5e5e5 | white 10% |
| input | #e5e5e5 | white 15% |
| ring | #a3a3a3 | #737373 |
| sidebar / sidebar-foreground | #fafafa / #0a0a0a | #171717 / #fafafa |
| sidebar-primary / foreground | #171717 / #fafafa | #1447e6 / #fafafa |
| sidebar-accent / foreground | #f5f5f5 / #171717 | #262626 / #fafafa |
| sidebar-border / ring | #e5e5e5 / #a3a3a3 | white 10% / #737373 |

The blue dark sidebar-primary is present in the official neutral scaffold; it
is not the removed ChoscorDB cobalt accent. Charts are unused and need no gallery
port. Success, warning, cancellation and database syntax roles are application
extensions: use text plus an icon so color never conveys the state alone, and
record their final token values in the preview.

## Measurements and state treatment

At 100% scale, 1 CSS px = 1 Qt logical pixel, 1 rem = 16 logical pixels, Tailwind
spacing unit = 4 logical pixels. Use pixel-sized UI fonts; Qt/OS device scaling
then supplies physical pixels. Fractional values should remain fractional where
Qt supports them, otherwise document rounding. Border width is 1. Base radius
is 10, derived sm/md/lg/xl/2xl/3xl/4xl = 6/8/10/14/18/22/26.

| Component | Pinned measurements / behavior |
| --- | --- |
| Text button xs/sm/default/lg | Height 24/28/32/36; font 12/12.8/14/14; medium 500; horizontal padding 8/10/10/10; gap 4/4/6/6; xs/sm radius 8, default/lg radius 10 |
| Icon button xs/sm/default/lg | Square 24/28/32/36; icon 12/16/16/16 (text sm icon 14) |
| Button states | Default hover primary at 80%; destructive tint 10% light, 20% dark; hover 20% light, 30% dark; disabled opacity 50%; active moves down 1 except popup triggers |
| Focus / invalid | Border ring; 3px ring at 50%; invalid destructive border and ring 20% light / 40% dark |
| Input | Height 32, radius 10, border 1; dark input surface white 15% at 30% opacity; read-only retains selection |
| Checkbox | 16 square, radius 4, check icon 14; primary checked fill |
| Table | Body row37 = 20 line box + 16 vertical padding + collapsed1px border; header40. Navigator rows retain their separate28px size. |
| Badge | Height 20, text 12 medium, padding x8, gap4; full radius |
| Card/dialog | Radius14, padding16; card ring foreground 10%, 1px; dialog popover colors |
| Modal overlay | Black10%, optional browser backdrop blur; duration100ms |
| Menu/popover | Nova CSS definitions; menu min width144, padding4, radius10, shadow-md, duration100ms |
| Typography | Body14/20, small12/16, heading16/22 (leading-snug); regular400, medium500, semibold600, bold700 |

Do not use solid red destructive buttons from older styles. Do not substitute
legacy 36/40px density metrics for Nova's 32px default.

## Font and icon provenance

Bundled static Geist Regular, Medium, SemiBold and Bold TTF files originate from
[vercel/geist-font revision 10dc7658f13c38a474cde201bb09a4617267545b](https://github.com/vercel/geist-font/tree/10dc7658f13c38a474cde201bb09a4617267545b),
`fonts/Geist/ttf/`. See `desktop/resources/fonts/SOURCE-GEIST.json` for checksums
and `OFL.txt` for SIL OFL 1.1. Static files avoid variable-font differences between
Qt installations. The pinned shadcn source selects the family through Google
Fonts without pinning binary bytes; these explicit upstream binary bytes are
our reproducible font realization. Qt Unicode fallback is required. SQL editor
family/size remain user-controlled. Existing Lucide SVG source/license records
remain under `desktop/resources/icons/`; icons use a 24-unit viewBox, scalable
2-unit stroke and semantic foreground coloring.

## Reference captures and limits

[nova-neutral-chromium.png](shadcn-reference/nova-neutral-chromium.png) is an
actual inspected Chromium render of the pinned Nova CSS, with synthetic HTML
specimens in [specimen.html](shadcn-reference/specimen.html). The exact environment
is in [capture-environment.json](shadcn-reference/capture-environment.json):
1280×620 logical pixels, DPR1, reduced motion, macOS, bundled Geist.
[Compiled CSS](shadcn-reference/capture-built.css) works offline with the bundled
fonts. `capture.css` contains the pinned theme plus applicable Nova rules; the
unused input-OTP caret animation utility is omitted because the standalone
capture does not load shadcn's animation plugin. No pictured specimen uses it.

This is a static baseline covering normal, disabled and invalid appearances;
“Keyboard focus” is an interactive button label, not a forced focus claim. It is
not a complete upstream React interaction harness. Pressed/menu, icon and full composition comparisons must be captured before
full AC12 approval; the additional focus/hover capture is documented below.
An image existing is not evidence that Qt matches it. Native Qt comparisons and
user gallery acceptance are separate pending evidence.

## Explicit exceptions

- Font rasterization/hinting and Unicode fallback vary by Qt/OS; geometry, family,
  weight and color still require comparison.
- Native title bars, OS menu bars and system file pickers remain native.
- Native modal panels use dimming; web backdrop blur is not required where Qt
  cannot provide an equivalent reliably.
- Accessibility takes priority. A 3:1 focus outline may require stronger
  #737373 light / #a3a3a3 dark rather than the reference 50% ring. Preserve raw
  reference ring tokens separately from accessible rendering overrides.
- Muted #737373 text over #f5f5f5 is marginally below 4.5:1; affected text must use
  the rendered #707070 foreground. Raw tokens remain #737373. Do not weaken existing contrast checks.
- Destructive text over the light tinted destructive surface uses #bf000a to
  preserve 4.5:1 contrast even at the 20% hover tint; the raw destructive token
  and background tint retain the reference value.
- Forced contrast and reduced motion override default colors/animation; preserve
  input semantics, accessible names, keyboard access and DPI scaling.

To rebuild the reference capture, from `docs/design/shadcn-reference/` use a
throwaway npm installation (do not commit node_modules or generated npm metadata):

```sh
npm install --no-save tailwindcss@4.2.1 @tailwindcss/cli@4.2.1 playwright@1.58.2
npx tailwindcss -i capture.css -o capture-built.css
npx playwright install chromium
node capture.cjs
```

Alternatively set `CHROMIUM_PATH` to an installed Chrome executable; the checked
capture used `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome`.
The script fails visibly with a nonzero exit on browser/capture errors.
[nova-neutral-focus-hover.png](shadcn-reference/nova-neutral-focus-hover.png)
adds actual keyboard-induced focus on the light outline button and actual hover
on the dark default button. Its interaction sequence is in `capture.cjs`.

The pinned Base tooltip structure (`apps/v4/registry/bases/base/ui/tooltip.tsx`)
uses foreground as its background and background as its text color, with Nova
radius8, padding12×6 and text12. The shared Qt tooltip surface paints the
directional arrow, follows the originating theme, and dismisses on leave, input
or timeout. Its actual popup is covered by the control interaction tests.

Production `Text` uses fixed line boxes: body14/20, small12/16, base16/24,
card heading16/22 (leading-snug), dialog title16/16 (leading-none). Qt rounds
Nova small-button12.8px to13px; this explicit subpixel-font mapping affects only
the small variant. Menu shadows consume both medium-elevation layers; Qt blur
rasterization is compared separately from the source offsets/blur/spread values.
