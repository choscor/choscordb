use crate::ffi;
use choscordb_core::ValuePreviewKind;

pub fn value_preview_error(bytes: u64) -> String {
    choscordb_core::value_preview_too_large(bytes).unwrap_or_default()
}

pub fn pdf_page_image(bytes: &[u8], number: usize) -> ffi::PdfPageDto {
    match choscordb_core::pdf_page_image(bytes, number) {
        Ok(png) => ffi::PdfPageDto {
            png,
            error: String::new(),
        },
        Err(error) => ffi::PdfPageDto {
            png: Vec::new(),
            error,
        },
    }
}

pub fn value_preview(bytes: &[u8], binary: bool, database_type: &str) -> ffi::ValuePreviewDto {
    let preview = choscordb_core::value_preview(bytes, binary, database_type);
    ffi::ValuePreviewDto {
        kind: match preview.kind {
            ValuePreviewKind::Json => "json",
            ValuePreviewKind::Image => "image",
            ValuePreviewKind::Pdf => "pdf",
            ValuePreviewKind::Geometry => "geometry",
            ValuePreviewKind::None => "none",
        }
        .into(),
        summary: preview.summary,
        pretty: preview.pretty,
        minified: preview.minified,
        nodes: preview
            .nodes
            .into_iter()
            .map(|node| ffi::JsonPreviewNodeDto {
                parent: node.parent.map_or(-1, |parent| parent as i64),
                key: node.key,
                value: node.value,
                kind: node.kind.into(),
            })
            .collect(),
        image_format: preview.image_format,
        page_image: preview.page_image,
        pdf_pages: preview.pdf_pages,
    }
}
