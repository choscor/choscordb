#!/usr/bin/env python3
"""Render the existing app SVG into a macOS icon; generated files stay in build/."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def render(source, output):
    try:
        import cairosvg
    except (ImportError, OSError) as error:
        raise ValueError(
            "Icon rendering requires Cairo and scripts/release/requirements.txt"
        ) from error
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=output.parent, prefix="icon-") as temporary:
        iconset = Path(temporary) / "AppIcon.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                suffix = "@2x" if scale == 2 else ""
                cairosvg.svg2png(
                    url=str(Path(source).resolve()),
                    write_to=str(iconset / f"icon_{size}x{size}{suffix}.png"),
                    output_width=size * scale,
                    output_height=size * scale,
                )
        subprocess.run(
            ["iconutil", "-c", "icns", str(iconset), "-o", str(output)], check=True
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        render(args.source, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Icon generation failed: {error}\n")


if __name__ == "__main__":
    main()
