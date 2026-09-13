#!/usr/bin/env python3
"""Compare pinned browser/native component families without rescaling pixels.

Run after capture_mvp_native.cjs completes. Family fixtures intentionally differ:
only the Save/Run controls have matching content. Each other sheet names its
limited comparison seam and retains full source bounds in comparisons.json.
"""

import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "docs/design/mvp-reference"
NATIVE = ROOT / "docs/design/mvp-native"

# (reference crop, native specimen, named control, bounded detail size, purpose)
FAMILIES = (
    (
        "button-default",
        "buttons",
        "previewReferenceSave",
        None,
        "Same Save text: complete control bounds, typography, border, radius.",
    ),
    (
        "button-primary",
        "buttons",
        "previewReferenceRun",
        None,
        "Same Run text/icon: complete control bounds, color, typography, icon.",
    ),
    (
        "field",
        "fields",
        "field-readonly",
        (200, 32),
        "Leading 200px: 31px control, corner, border, text baseline. Text and "
        "container widths differ; this is not field-content equality.",
    ),
    (
        "selector",
        "selects",
        "designControlGlyphs",
        (200, 34),
        "Leading 200px: closed selector surface/height. Values and container "
        "widths differ; trailing arrow and popup are outside this crop.",
    ),
    (
        "toggle",
        "checks-toggles",
        None,
        None,
        "Checked switch indicator only: 32x19 track, 13px white thumb. "
        "Surrounding label and checkbox accessibility role are outside this crop.",
    ),
    (
        "pane-tabs",
        "tabs",
        "previewObjectTabs",
        (360, 35),
        "Leading 360px of object tabs: same tab labels, strip height, inset, "
        "active underline. Full container widths differ.",
    ),
    (
        "menu",
        "menus",
        None,
        None,
        "Whole menu panels: surface, border, radius, row treatment. Different "
        "commands, check/submenu gutters and selection states prevent pixel "
        "equality; overlay exposes differing panel sizes rather than correcting them.",
    ),
    (
        "modal",
        "dialogs",
        None,
        (120, 16),
        "Top-left 120x16 panel edge: 700px shared width, border, 8px corner. "
        "Different panel bodies are compared separately side by side; this "
        "detail does not establish header/footer or whole-dialog fidelity.",
    ),
)


def read_json(path):
    return json.loads(path.read_text())


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(path, expected):
    if sha256(path) != expected:
        raise ValueError(f"Stale or modified capture input: {path}")


def integer_box(bounds):
    return (
        math.floor(bounds["x"]),
        math.floor(bounds["y"]),
        math.ceil(bounds["x"] + bounds["width"]),
        math.ceil(bounds["y"] + bounds["height"]),
    )


def native_bounds(metadata, name, specimen, width, height):
    if name:
        # Selects records two designControlGlyphs; the first is the enabled field.
        result = next(item for item in metadata["controls"] if item["name"] == name)
        return result, "named widget rectangle from capture metadata"
    # These actual painted surfaces have no named widget rectangle in the capture
    # metadata. Coordinates were inspected in both themes and sizes, then pinned
    # with source-image hashes in each comparison record. No edge fitting is used.
    if specimen == "checks-toggles":
        result = {"x": 9, "y": 166, "width": 32, "height": 19}
    elif specimen == "menus":
        result = {
            "x": width // 2 - 128,
            "y": height // 2 - 69,
            "width": 255,
            "height": 137,
        }
    elif specimen == "dialogs":
        result = {
            "x": width // 2 - 350,
            "y": height // 2 - 91,
            "width": 700,
            "height": 182,
        }
    else:
        raise ValueError(f"No audited native bounds for {specimen}")
    return result, "literal painted bounds from inspection; source PNG hash pinned"


def browser_element(item):
    source = REFERENCE / item["file"]
    verify(source, item["sha256"])
    with Image.open(source) as original:
        image = original.convert("RGBA")
    bounds = integer_box(item["bounds"])
    clip = item["clip"]
    return image.crop(
        (
            bounds[0] - clip["x"],
            bounds[1] - clip["y"],
            bounds[2] - clip["x"],
            bounds[3] - clip["y"],
        )
    )


def detail(image, size):
    if size is None:
        return image
    return image.crop((0, 0, min(size[0], image.width), min(size[1], image.height)))


def make_sheet(browser, native, theme, filename, *, overlay=True):
    size = (max(browser.width, native.width), max(browser.height, native.height))
    paper = "#20272b" if theme == "dark" else "white"
    a, b = Image.new("RGBA", size, paper), Image.new("RGBA", size, paper)
    a.alpha_composite(browser, (0, 0))
    b.alpha_composite(native, (0, 0))
    panels = [("Browser", a), ("Qt", b)]
    if overlay:
        panels += [
            ("50% overlay", Image.blend(a, b, 0.5)),
            (
                "Absolute RGB difference",
                ImageChops.difference(a.convert("RGB"), b.convert("RGB")),
            ),
        ]
    gap = 16
    column = max(150, size[0]) + gap
    sheet = Image.new("RGB", (column * len(panels) + gap, size[1] + 40), "#f6f7f8")
    draw = ImageDraw.Draw(sheet)
    for index, (label, image) in enumerate(panels):
        x = gap + index * column
        draw.text((x, 6), label, fill="#222b32")
        sheet.paste(image, (x, 25))
    sheet.save(NATIVE / filename)
    return [label for label, _ in panels]


def color_samples(browser, native, name):
    # Literal positions inside the compared painted controls, separate from any
    # padding added to comparison sheets. Samples describe actual pixels, not an
    # aggregate pass/fail score. Fractional browser origin remains observable.
    positions = {
        "button-default": (5, 14),
        "button-primary": (5, 14),
        "field": (190, 15),
        "selector": (190, 15),
        "toggle": (5, 9),
        "pane-tabs": (330, 5),
        "menu": (150, 8),
        "modal": (60, 10),
    }
    x, y = positions[name]
    return {
        "positionRelativeToElement": {"x": x, "y": y},
        "browserRgb": list(browser.convert("RGB").getpixel((x, y))),
        "nativeRgb": list(native.convert("RGB").getpixel((x, y))),
    }


def indicator_ink(image, theme):
    """Describe arrow ink; the threshold locates pixels, never grants acceptance."""
    rgb = image.convert("RGB")
    background = (32, 39, 43) if theme == "dark" else (255, 255, 255)
    points = [
        (x, y)
        for y in range(7, 25)
        for x in range(rgb.width - 24, rgb.width - 1)
        if max(abs(a - b) for a, b in zip(rgb.getpixel((x, y)), background)) > 80
    ]
    left = min(x for x, _ in points)
    top = min(y for _, y in points)
    right = max(x for x, _ in points) + 1
    bottom = max(y for _, y in points) + 1
    return {
        "width": right - left,
        "height": bottom - top,
        "leftInsetFromRight": rgb.width - left,
        "top": top,
        "centerInsetFromRight": rgb.width - (left + right) / 2,
        "centerY": (top + bottom) / 2,
    }


def main():
    reference = read_json(REFERENCE / "manifest.json")
    native_manifest = read_json(NATIVE / "manifest.json")
    for item in reference["sources"]:
        verify(ROOT / item["path"], item["sha256"])
    for item in native_manifest["sources"]:
        verify(ROOT / item["path"], item["sha256"])
    records = []
    for width, height in ((1280, 900), (960, 640)):
        for theme in ("light", "dark"):
            viewport = {"width": width, "height": height}
            for name, specimen, control_name, detail_size, scope in FAMILIES:
                reference_item = next(
                    item
                    for item in reference["crops"]
                    if item["name"] == name
                    and item["theme"] == theme
                    and item["viewport"] == viewport
                )
                native_item = next(
                    item
                    for item in native_manifest["captures"]
                    if item["specimen"] == specimen
                    and item["theme"] == theme
                    and item["viewport"] == viewport
                )
                source = NATIVE / native_item["file"]
                verify(source, native_item["sha256"])
                metadata = read_json(NATIVE / native_item["metadata"])
                bounds, origin = native_bounds(
                    metadata, control_name, specimen, width, height
                )
                with Image.open(source) as original:
                    actual = original.convert("RGBA").crop(integer_box(bounds))
                browser = browser_element(reference_item)
                filename = f"comparison-{theme}-{width}x{height}-{name}.png"
                panels = make_sheet(
                    detail(browser, detail_size),
                    detail(actual, detail_size),
                    theme,
                    filename,
                )
                record = {
                    "file": filename,
                    "family": name,
                    "theme": theme,
                    "viewport": viewport,
                    "reference": reference_item["file"],
                    "referenceSha256": reference_item["sha256"],
                    "native": native_item["file"],
                    "nativeSha256": native_item["sha256"],
                    "referenceBounds": reference_item["bounds"],
                    "nativeBounds": bounds,
                    "nativeBoundsSource": origin,
                    "detailSize": list(detail_size) if detail_size else None,
                    "scope": scope,
                    "alignment": "Top-left outer bounds; floor/ceil integer clip; "
                    "fractional browser rasterization retained; no resampling. "
                    "Transparent pixels and unused canvas use panel color; original inputs "
                    "are unchanged. Cropped details do not imply whole-control equality.",
                    "panels": panels,
                    "samples": color_samples(browser, actual, name),
                    "sha256": sha256(NATIVE / filename),
                    "nativeCaptureTime": native_manifest["capturedAt"],
                    "nativeExecutableSha256": native_manifest["executableSha256"],
                }
                if name == "selector":
                    record["indicatorInk"] = {
                        "method": "Last24px, rows7..24, any RGB channel differing "
                        "from panel by more than80; detection only, no acceptance threshold.",
                        "browser": indicator_ink(browser, theme),
                        "native": indicator_ink(actual, theme),
                    }
                if name in ("field", "selector"):
                    trailing_file = (
                        f"comparison-{theme}-{width}x{height}-{name}-trailing.png"
                    )
                    browser_tail = (
                        browser.width - 80,
                        0,
                        browser.width,
                        browser.height,
                    )
                    native_tail = (actual.width - 80, 0, actual.width, actual.height)
                    make_sheet(
                        browser.crop(browser_tail),
                        actual.crop(native_tail),
                        theme,
                        trailing_file,
                    )
                    record["trailingFile"] = trailing_file
                    record["trailingSha256"] = sha256(NATIVE / trailing_file)
                    record["trailingCropsRelativeToIntegerBounds"] = {
                        "browser": browser_tail,
                        "native": native_tail,
                    }
                    record["trailingScope"] = (
                        "Last 80px at original scale: right border/radius and selector "
                        "arrow if present. Field widths deliberately remain different."
                    )
                if name == "modal":
                    context_file = (
                        f"comparison-{theme}-{width}x{height}-modal-context.png"
                    )
                    make_sheet(browser, actual, theme, context_file, overlay=False)
                    record["contextFile"] = context_file
                    record["contextSha256"] = sha256(NATIVE / context_file)
                    record["contextScope"] = (
                        "Shared panel treatment only; Preferences browser body "
                        "and synthetic native dialog body intentionally differ. "
                        "No full-body difference/overlay is presented as fidelity proof."
                    )
                records.append(record)
    (NATIVE / "comparisons.json").write_text(json.dumps(records, indent=2) + "\n")
    print(
        f"Wrote {len(records)} family sheets plus 8 trailing-edge and 4 panel context sheets."
    )


if __name__ == "__main__":
    main()
