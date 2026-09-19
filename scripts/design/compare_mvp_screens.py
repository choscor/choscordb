#!/usr/bin/env python3
"""Create unscaled MVP comparisons from recorded browser/native widget captures.

Requires Pillow. Native modal comparisons explicitly compose separately captured
owner/panel widgets at their recorded coordinates; they are not OS screenshots.
"""

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops

STATE_MAP = {
    "sql-ready": "workspace-ready",
    "sql-results": "workspace-results",
    "sql-cancelling": "workspace-cancelling",
    "preferences-0": "preferences-appearance",
    "preferences-1": "preferences-editor",
    "preferences-2": "preferences-results",
    "preferences-3": "preferences-history",
    "preferences-4": "preferences-shortcuts",
    "export": "export-ready",
    "connection": "connection-sqlite",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("native", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    reference = json.loads((args.reference / "manifest.json").read_text())
    native = json.loads((args.native / "manifest.json").read_text())
    if not reference["clientAligned"]:
        parser.error("Use the separately captured --client-aligned reference")
    args.output.mkdir(parents=True, exist_ok=True)
    panels = {c["owner"]: c for c in native["captures"] if c.get("dialog")}
    comparisons = []
    unmatched = []
    for capture in native["captures"]:
        if capture.get("dialog"):
            continue
        width, height = capture["clientWidth"], capture["clientHeight"]
        state = STATE_MAP.get(capture["state"], capture["state"])
        match = next(
            (
                c
                for c in reference["captures"]
                if c["state"] == state
                and c["theme"] == capture["theme"]
                and c["viewport"] == {"width": width, "height": height + 57}
            ),
            None,
        )
        if match is None:
            unmatched.append(capture["file"])
            continue
        ref_path = args.reference / match["file"]
        assert digest(ref_path) == match["sha256"], ref_path
        expected = Image.open(ref_path).convert("RGB")
        assert expected.size == (width, height + 57), expected.size
        expected = expected.crop((0, 57, width, height + 57))
        native_path = args.native / capture["file"]
        actual = Image.open(native_path).convert("RGB")
        ratio = capture["devicePixelRatio"]
        assert actual.size == (round(width * ratio), round(height * ratio)), native_path
        panel = panels.get(capture["file"])
        if panel:
            overlay = Image.open(args.native / panel["file"]).convert("RGBA")
            assert overlay.size == (
                round(panel["clientWidth"] * ratio),
                round(panel["clientHeight"] * ratio),
            )
            actual.paste(
                overlay,
                (
                    round(panel["ownerRelativeX"] * ratio),
                    round(panel["ownerRelativeY"] * ratio),
                ),
                overlay,
            )
        if ratio != 1:
            actual = actual.resize((width, height), Image.Resampling.LANCZOS)
        stem = native_path.stem
        pair = Image.new("RGB", (width * 2, height))
        pair.paste(expected, (0, 0))
        pair.paste(actual, (width, 0))
        outputs = {
            "sideBySide": pair,
            "overlay": Image.blend(expected, actual, 0.5),
            "difference": ImageChops.difference(expected, actual),
        }
        files = {}
        for kind, image in outputs.items():
            output = args.output / f"{stem}-{kind}.png"
            image.save(output)
            files[kind] = {"file": output.name, "sha256": digest(output)}
        comparisons.append(
            {
                "state": capture["state"],
                "theme": capture["theme"],
                "client": [width, height],
                "reference": match["file"],
                "referenceSha256": digest(ref_path),
                "native": capture["file"],
                "nativeSha256": digest(native_path),
                "nativeDevicePixelRatio": ratio,
                "modalWidgetComposite": bool(panel),
                "outputs": files,
            }
        )
    (args.output / "manifest.json").write_text(
        json.dumps(
            {
                "procedure": "Reference left, native right; 1:1 logical coordinates. "
                "Native backing pixels are normalized only by recorded devicePixelRatio using Lanczos; "
                "original full-resolution native captures remain unchanged. "
                "Browser mock chrome cropped at y=57. Overlay is 50%; difference is raw RGB. "
                "Modal owner/panel widget composites are explicitly identified. "
                "No numeric similarity threshold establishes acceptance.",
                "comparisons": comparisons,
                "nativeExtensionsWithoutReference": unmatched,
            },
            indent=2,
        )
        + "\n"
    )
    (args.output / "index.html").write_text(
        """<!doctype html><meta charset="utf-8"><title>MVP native screen review</title>
<style>body{font:14px system-ui;margin:24px;background:#f2f5f4;color:#1c2927}
nav{position:sticky;top:0;background:#f2f5f4;padding:12px 0;display:flex;gap:12px}
select{padding:8px}img{display:block;max-width:100%;height:auto;border:1px solid #cbd5d1}
p{max-width:1000px;line-height:1.5}</style>
<h1>MVP screen comparisons</h1>
<p>Side by side: prototype left, native right. Choose overlay or raw difference to inspect
geometry. Images retain logical proportions; this viewer scales them only to fit your window.
Open an image directly for full size. Native modal images combine separately captured owner
and panel widgets at recorded coordinates, not an OS compositor screenshot. Actual SQLite
values, available settings, execution times and ownership labels differ from the mock fixture.
See the adjacent manifest and implementation evidence for exact exceptions and validation.</p>
<nav><select id="capture" aria-label="Screen capture"></select>
<select id="kind" aria-label="Comparison"><option value="sideBySide">Side by side</option>
<option value="overlay">50% overlay</option><option value="difference">Raw difference</option>
</select><a id="full">Open full-size image</a></nav><img id="image" alt="Selected comparison">
<script>const captures="""
        + json.dumps(comparisons)
        + """;const select=document.querySelector('#capture'),kind=document.querySelector('#kind');
captures.forEach((c,i)=>{const o=document.createElement('option');o.value=i;
o.textContent=`${c.theme} · ${c.client.join('×')} · ${c.state}`;select.append(o)});
function update(){const path=captures[select.value].outputs[kind.value].file;
document.querySelector('#image').src=path;document.querySelector('#full').href=path}
select.onchange=kind.onchange=update;update();</script>"""
    )
    print(f"Created {len(comparisons)} comparisons; {len(unmatched)} native extensions")


if __name__ == "__main__":
    main()
