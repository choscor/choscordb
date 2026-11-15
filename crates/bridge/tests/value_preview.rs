use choscordb_bridge::{value_preview, value_preview_error};

#[test]
fn previews_cross_the_bridge_with_parent_indexes() {
    let preview = value_preview(br#"{"a":[1]}"#, false, "json");
    assert_eq!(preview.kind, "json");
    let rows: Vec<_> = preview
        .nodes
        .iter()
        .map(|node| (node.parent, node.key.as_str(), node.kind.as_str()))
        .collect();
    assert_eq!(
        rows,
        [(-1, "$", "object"), (0, "a", "array"), (1, "[0]", "number")]
    );
    assert_eq!(value_preview(b"GIF89a..", true, "blob").image_format, "GIF");
    assert_eq!(value_preview_error(16 * 1024 * 1024), "");
    assert_eq!(
        value_preview_error(16 * 1024 * 1024 + 1),
        "A preview is limited to 16 MiB. Use the byte window for this value."
    );
}
