"""Reject local build locations in the payload before it is signed or published."""

from pathlib import Path

from stage import regular_files


def verify_payload_privacy(app, forbidden_roots):
    """Inspect all payload bytes; callers supply local home and source roots."""
    roots = {str(Path(root)).rstrip("/") for root in forbidden_roots}
    needles = {
        root.encode(encoding)
        for root in roots
        if root and root != "."
        for encoding in ("utf-8", "utf-16-le", "utf-16-be")
    }
    for path in regular_files(Path(app)):
        data = path.read_bytes()
        if any(needle in data for needle in needles):
            raise ValueError(
                "Release payload contains a local build path: " + path.name
            )
