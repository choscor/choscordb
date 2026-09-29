use choscordb_bridge::{
    ffi::{PreviewCaptureDto, PreviewControlDto},
    write_preview_capture_file,
};

fn metadata() -> PreviewCaptureDto {
    PreviewCaptureDto {
        section: "Icons".into(),
        specimen: "icons".into(),
        source: "desktop/icons.cpp".into(),
        surface: "inline".into(),
        logical_width: 640,
        logical_height: 900,
        source_device_scale: 1.0,
        themes: "Light".into(),
        font: "Sans".into(),
        qt: "6.9".into(),
        platform: "offscreen".into(),
        os: "Test OS".into(),
        controls: vec![PreviewControlDto {
            name: "iconControl".into(),
            theme: "Light".into(),
            x: 4,
            y: 7,
            width: 24,
            height: 20,
        }],
    }
}

#[test]
fn typed_capture_transport_persists_the_requested_document() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("icons.png");
    let png = b"\x89PNG\r\n\x1a\nicon image";
    let result = write_preview_capture_file(path.to_str().unwrap(), png, metadata());
    assert!(result.error.is_empty(), "{}", result.error);
    assert!(result.png_written);
    assert_eq!(std::fs::read(path).unwrap(), png);
    let record: serde_json::Value =
        serde_json::from_slice(&std::fs::read(dir.path().join("icons.png.json")).unwrap()).unwrap();
    assert_eq!(record["section"], "Icons");
    assert_eq!(record["specimen"], "icons");
    assert_eq!(record["themes"], "Light");
    assert_eq!(
        record["controls"],
        serde_json::json!([
            {"name":"iconControl", "theme":"Light", "x":4, "y":7, "width":24, "height":20}
        ])
    );
}

#[test]
fn typed_transport_preserves_prewrite_and_partial_failure_results() {
    let dir = tempfile::tempdir().unwrap();
    let png = b"\x89PNG\r\n\x1a\nicon image";
    let result = write_preview_capture_file(
        dir.path().join("missing/icons.png").to_str().unwrap(),
        png,
        metadata(),
    );
    assert!(!result.png_written);
    assert!(!result.error.is_empty());
    let path = dir.path().join("partial.png");
    std::fs::create_dir(dir.path().join("partial.png.json")).unwrap();
    let result = write_preview_capture_file(path.to_str().unwrap(), png, metadata());
    assert!(result.png_written);
    assert!(!result.error.is_empty());
    assert_eq!(std::fs::read(path).unwrap(), png);
}
