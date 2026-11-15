use crate::ffi;
use choscordb_core::ValuePreviewKind;

pub fn value_preview_error(bytes: u64) -> String {
    choscordb_core::value_preview_too_large(bytes).unwrap_or_default()
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
    }
}
