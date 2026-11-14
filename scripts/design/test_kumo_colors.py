import tempfile
import unittest
from pathlib import Path

import kumo_colors


class OklchConversionTest(unittest.TestCase):
    def test_converts_white_black_and_gray(self):
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("oklch(100% 0 0)")), "#ffffff"
        )
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("oklch(0% 0 0)")), "#000000"
        )
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("oklch(55.6% 0 none)")),
            "#737373",
        )

    def test_clips_wide_gamut_colors_like_the_tailwind_hex_fallbacks(self):
        self.assertEqual(
            kumo_colors.hex_color(
                kumo_colors.parse_color("oklch(62.3% 0.214 259.815)")
            ),
            "#2b7fff",
        )
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("oklch(63.7% 0.237 25.331)")),
            "#fb2c36",
        )

    def test_keeps_alpha_and_reads_hex_literals(self):
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("oklch(0% 0 0 / 0.08)")),
            "#00000014",
        )
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("#f6821f")), "#f6821f"
        )
        self.assertEqual(
            kumo_colors.hex_color(kumo_colors.parse_color("transparent")), "#00000000"
        )

    def test_mixes_in_oklch_with_black(self):
        mixed = kumo_colors.parse_color(
            "color-mix(in oklch, oklch(0.5 0.2 260), black 10%)"
        )
        self.assertAlmostEqual(mixed.lightness, 0.45)
        self.assertAlmostEqual(mixed.chroma, 0.18)
        self.assertAlmostEqual(mixed.hue, 260)

    def test_mixes_in_oklch_with_white_and_keeps_the_hue(self):
        mixed = kumo_colors.parse_color(
            "color-mix(in oklch, oklch(0.5 0.2 260), white 30%)"
        )
        self.assertAlmostEqual(mixed.lightness, 0.65)
        self.assertAlmostEqual(mixed.chroma, 0.14)
        self.assertAlmostEqual(mixed.hue, 260)


class EmphasisTest(unittest.TestCase):
    def test_button_emphasis_mixes_each_token_like_kumo(self):
        self.assertEqual(
            kumo_colors.emphasis("oklch(0.5 0.2 260)"),
            [
                ("ring", "color-mix(in oklch, oklch(0.5 0.2 260), black 10%)"),
                ("bg", "color-mix(in oklch, oklch(0.5 0.2 260), white 30%)"),
                ("start", "color-mix(in oklch, oklch(0.5 0.2 260), white 15%)"),
                ("end", "oklch(0.5 0.2 260)"),
            ],
        )


class TokenResolutionTest(unittest.TestCase):
    def test_variables_resolve_from_binding_then_tailwind_then_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            theme = root / "theme-kumo.css"
            theme.write_text(
                "@layer base {\n"
                "  :root,\n"
                '  [data-theme="kumo"] {\n'
                "    --color-kumo-a: var(--color-kumo-neutral-975, oklch(8.5% 0 0));\n"
                "    --color-kumo-b: var(--color-neutral-500, oklch(1% 0 0));\n"
                "    --color-kumo-c: var(--color-kumo-neutral-25, oklch(100% 0 0));\n"
                "    --text-color-kumo-d: oklch(14.5% 0 0 / 0.1);\n"
                "  }\n"
                '  :root[data-mode="dark"],\n'
                '  [data-mode="dark"]:not([data-theme]) {\n'
                "    --color-kumo-a: #000;\n"
                "    --color-kumo-b: var(--color-white, #fff);\n"
                "    --color-kumo-c: oklch(0% 0 0);\n"
                "    --text-color-kumo-d: transparent;\n"
                "  }\n"
                "}\n",
                encoding="utf-8",
            )
            binding = root / "kumo-binding.css"
            binding.write_text(
                "--color-kumo-neutral-975: oklch(0% 0 0);\n", encoding="utf-8"
            )
            tailwind = root / "theme.css"
            tailwind.write_text(
                "--color-neutral-500: oklch(55.6% 0 none);\n--color-white: #fff;\n",
                encoding="utf-8",
            )
            tokens = kumo_colors.resolve_tokens(theme, binding, tailwind)
        self.assertEqual(
            tokens,
            [
                ("kumo-a", "#000000", "#000000"),
                ("kumo-b", "#737373", "#ffffff"),
                ("kumo-c", "#ffffff", "#000000"),
                ("text-kumo-d", "#0a0a0a1a", "#00000000"),
            ],
        )


if __name__ == "__main__":
    unittest.main()
