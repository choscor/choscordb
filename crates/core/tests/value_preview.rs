use choscordb_core::{MAX_JSON_PREVIEW_NODES, ValuePreviewKind, pdf_page_image, value_preview};

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
    assert!(
        pdf.summary
            .ends_with("ChoscorDB cannot render it: the document is not valid."),
        "{}",
        pdf.summary
    );
    assert!(pdf.page_image.is_empty());
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

fn pdf_with_pages(pages: &[(&str, u32, u32)]) -> Vec<u8> {
    let kids: Vec<String> = (0..pages.len())
        .map(|index| format!("{} 0 R", 3 + 2 * index))
        .collect();
    let mut objects = vec![
        "<< /Type /Catalog /Pages 2 0 R >>".to_string(),
        format!(
            "<< /Type /Pages /Kids [{}] /Count {} >>",
            kids.join(" "),
            pages.len()
        ),
    ];
    for (index, (content, width, height)) in pages.iter().enumerate() {
        objects.push(format!(
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {width} {height}] /Contents {} 0 R >>",
            4 + 2 * index
        ));
        objects.push(format!(
            "<< /Length {} >>\nstream\n{content}\nendstream",
            content.len()
        ));
    }
    let mut pdf = b"%PDF-1.4\n".to_vec();
    let mut offsets = Vec::new();
    for (index, object) in objects.iter().enumerate() {
        offsets.push(pdf.len());
        pdf.extend(format!("{} 0 obj\n{object}\nendobj\n", index + 1).bytes());
    }
    let xref = pdf.len();
    pdf.extend(format!("xref\n0 {}\n0000000000 65535 f \n", objects.len() + 1).bytes());
    for offset in offsets {
        pdf.extend(format!("{offset:010} 00000 n \n").bytes());
    }
    pdf.extend(
        format!(
            "trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n",
            objects.len() + 1
        )
        .bytes(),
    );
    pdf
}

fn one_page_pdf(content: &str) -> Vec<u8> {
    pdf_with_pages(&[(content, 200, 100)])
}

fn png_size(png: &[u8]) -> (u32, u32) {
    assert!(png.starts_with(b"\x89PNG\r\n\x1a\n"));
    let field = |at: usize| u32::from_be_bytes(png[at..at + 4].try_into().unwrap());
    (field(16), field(20))
}

#[test]
fn a_pdf_document_shows_its_first_page_as_an_image() {
    let filled = one_page_pdf("1 0 0 rg 0 0 100 100 re f");
    let preview = value_preview(&filled, true, "bytea");
    assert_eq!(preview.kind, ValuePreviewKind::Pdf);
    assert_eq!(
        preview.summary,
        format!(
            "PDF document, version 1.4, 1 page(s), {} bytes.",
            filled.len()
        )
    );
    assert_eq!(png_size(&preview.page_image), (400, 200));
    let blank = value_preview(&one_page_pdf(""), true, "bytea");
    assert_eq!(png_size(&blank.page_image), (400, 200));
    assert_ne!(blank.page_image, preview.page_image);
}

#[test]
fn each_pdf_page_renders_on_request() {
    let pdf = pdf_with_pages(&[("", 200, 100), ("0 0 1 rg 0 0 50 50 re f", 100, 300)]);
    let preview = value_preview(&pdf, true, "bytea");
    assert_eq!(preview.pdf_pages, 2);
    assert!(
        preview.summary.contains(", 2 page(s), "),
        "{}",
        preview.summary
    );
    assert_eq!(png_size(&preview.page_image), (400, 200));
    assert_eq!(pdf_page_image(&pdf, 1).unwrap(), preview.page_image);
    assert_eq!(png_size(&pdf_page_image(&pdf, 2).unwrap()), (200, 600));
    assert_eq!(
        pdf_page_image(&pdf, 3).unwrap_err(),
        "Page 3 does not exist. The document has 2 page(s)."
    );
    assert_eq!(
        pdf_page_image(&pdf, 0).unwrap_err(),
        "Page 0 does not exist. The document has 2 page(s)."
    );
    assert_eq!(
        pdf_page_image(b"%PDF-1.4 broken", 1).unwrap_err(),
        "ChoscorDB cannot render the PDF document: the document is not valid."
    );
}
