use choscordb_core::{MAX_JSON_PREVIEW_NODES, ValuePreviewKind, value_preview};

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|byte| format!("{byte:02x}")).collect()
}

fn le_point(x: f64, y: f64) -> Vec<u8> {
    let mut bytes = vec![1];
    bytes.extend_from_slice(&1u32.to_le_bytes());
    bytes.extend_from_slice(&x.to_le_bytes());
    bytes.extend_from_slice(&y.to_le_bytes());
    bytes
}

#[test]
fn json_text_gets_pretty_minified_and_ordered_tree_rows() {
    let preview = value_preview(br#"{"b": [1, true], "a": {"c": null}}"#, false, "jsonb");
    assert_eq!(preview.kind, ValuePreviewKind::Json);
    assert_eq!(preview.minified, r#"{"b":[1,true],"a":{"c":null}}"#);
    assert_eq!(
        preview.pretty,
        "{\n  \"b\": [\n    1,\n    true\n  ],\n  \"a\": {\n    \"c\": null\n  }\n}"
    );
    let rows: Vec<_> = preview
        .nodes
        .iter()
        .map(|node| {
            (
                node.parent,
                node.key.as_str(),
                node.value.as_str(),
                node.kind,
            )
        })
        .collect();
    assert_eq!(
        rows,
        [
            (None, "$", "{2 keys}", "object"),
            (Some(0), "b", "[2 items]", "array"),
            (Some(1), "[0]", "1", "number"),
            (Some(1), "[1]", "true", "boolean"),
            (Some(0), "a", "{1 keys}", "object"),
            (Some(4), "c", "null", "null"),
        ]
    );
    let invalid = value_preview(b"{oops", false, "text");
    assert_eq!(invalid.kind, ValuePreviewKind::None);
    assert!(invalid.summary.starts_with("The value is not JSON:"));
}

#[test]
fn large_json_trees_stop_at_the_node_limit() {
    let items = vec!["1"; MAX_JSON_PREVIEW_NODES + 5].join(",");
    let preview = value_preview(format!("[{items}]").as_bytes(), false, "json");
    assert_eq!(preview.nodes.len(), MAX_JSON_PREVIEW_NODES);
    assert!(preview.nodes_truncated);
    assert_eq!(
        preview.summary,
        "JSON. The tree shows the first 10000 items."
    );
}

#[test]
fn binary_values_are_detected_as_images_or_pdf_documents() {
    let png = value_preview(b"\x89PNG\r\n\x1a\nrest", true, "bytea");
    assert_eq!(
        (png.kind, png.image_format.as_str()),
        (ValuePreviewKind::Image, "PNG")
    );
    assert_eq!(
        value_preview(&[0xff, 0xd8, 0xff, 0xe0], true, "blob").image_format,
        "JPEG"
    );
    let pdf = value_preview(
        b"%PDF-1.7\n1 0 obj << /Type /Pages >> 2 0 obj << /Type /Page >> 3 0 obj << /Type /Page>>",
        true,
        "blob",
    );
    assert_eq!(pdf.kind, ValuePreviewKind::Pdf);
    assert!(
        pdf.summary
            .starts_with("PDF document, version 1.7, about 2 page(s)")
    );
    let other = value_preview(&[1, 2, 3], true, "blob");
    assert_eq!(
        other.summary,
        "This binary value is not an image, a PDF document or a geometry."
    );
}

#[test]
fn geometry_wkb_becomes_wkt_with_the_srid() {
    let point = value_preview(&le_point(1.5, -2.0), true, "geometry");
    assert_eq!(point.kind, ValuePreviewKind::Geometry);
    assert_eq!(point.pretty, "POINT (1.5 -2)");
    let mut mysql = 4326u32.to_le_bytes().to_vec();
    mysql.extend(le_point(10.0, 20.0));
    assert_eq!(
        value_preview(&mysql, true, "point").pretty,
        "SRID=4326;POINT (10 20)"
    );
    let mut ewkb = vec![1];
    ewkb.extend_from_slice(&(2u32 | 0x2000_0000).to_le_bytes());
    ewkb.extend_from_slice(&3857u32.to_le_bytes());
    ewkb.extend_from_slice(&2u32.to_le_bytes());
    for value in [0.0f64, 0.0, 1.0, 1.0] {
        ewkb.extend_from_slice(&value.to_le_bytes());
    }
    assert_eq!(
        value_preview(hex(&ewkb).as_bytes(), false, "geometry").pretty,
        "SRID=3857;LINESTRING (0 0, 1 1)"
    );
    let mut multi = vec![1];
    multi.extend_from_slice(&4u32.to_le_bytes());
    multi.extend_from_slice(&2u32.to_le_bytes());
    multi.extend(le_point(1.0, 2.0));
    multi.extend(le_point(3.0, 4.0));
    assert_eq!(
        value_preview(&multi, true, "geometry").pretty,
        "MULTIPOINT ((1 2), (3 4))"
    );
    let broken = value_preview(&le_point(1.0, 2.0)[..10], true, "geometry");
    assert_eq!(broken.kind, ValuePreviewKind::None);
    assert_eq!(broken.summary, "The geometry WKB ends too early.");
}
