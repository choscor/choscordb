//! Typed capture transport; validation, serialization and persistence belong to core.
use crate::ffi::{PreviewCaptureDto, PreviewCaptureResultDto};
use choscordb_core::{PreviewCapture, PreviewControl};
use std::path::Path;

pub fn write_preview_capture_file(
    path: &str,
    png: &[u8],
    metadata: PreviewCaptureDto,
) -> PreviewCaptureResultDto {
    let metadata = PreviewCapture {
        section: metadata.section,
        specimen: metadata.specimen,
        source: metadata.source,
        surface: metadata.surface,
        logical_width: metadata.logical_width,
        logical_height: metadata.logical_height,
        source_device_scale: metadata.source_device_scale,
        themes: metadata.themes,
        font: metadata.font,
        qt: metadata.qt,
        platform: metadata.platform,
        os: metadata.os,
        controls: metadata
            .controls
            .into_iter()
            .map(|control| PreviewControl {
                name: control.name,
                theme: control.theme,
                x: control.x,
                y: control.y,
                width: control.width,
                height: control.height,
            })
            .collect(),
    };
    match choscordb_core::write_preview_capture(Path::new(path), png, &metadata) {
        Ok(()) => PreviewCaptureResultDto {
            png_written: true,
            error: String::new(),
        },
        Err(error) => PreviewCaptureResultDto {
            png_written: error.png_written,
            error: error.message,
        },
    }
}
