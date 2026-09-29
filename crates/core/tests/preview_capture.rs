use choscordb_core::{PreviewCapture, PreviewControl, write_preview_capture};
use serde_json::json;

fn capture() -> PreviewCapture {
    PreviewCapture {
        section: "Components".into(),
        specimen: "fields".into(),
        source: "desktop/field.cpp".into(),
        surface: "inline".into(),
        logical_width: 640,
        logical_height: 900,
        source_device_scale: 2.0,
        themes: "Dark".into(),
        font: "Sans".into(),
        qt: "6.9".into(),
        platform: "offscreen".into(),
        os: "Test OS".into(),
        controls: vec![PreviewControl {
            name: "field\"name".into(),
            theme: "Dark".into(),
            x: -2,
            y: 7,
            width: 120,
            height: 30,
        }],
    }
}
const PNG: &[u8] = b"\x89PNG\r\n\x1a\nencoded image";

#[test]
fn persists_encoded_capture_and_metadata_document() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("capture.png");
    write_preview_capture(&path, PNG, &capture()).unwrap();
    assert_eq!(std::fs::read(&path).unwrap(), PNG);
    let metadata: serde_json::Value =
        serde_json::from_slice(&std::fs::read(dir.path().join("capture.png.json")).unwrap())
            .unwrap();
    assert_eq!(
        metadata,
        json!({
            "section":"Components", "specimen":"fields", "source":"desktop/field.cpp", "surface":"inline",
            "logicalWidth":640, "logicalHeight":900, "scale":1, "sourceDeviceScale":2.0,
            "rendering":"QWidget logical-pixel render; native popup content; excludes OS shell",
            "themes":"Dark", "font":"Sans", "qt":"6.9", "platform":"offscreen", "os":"Test OS",
            "fixture":"synthetic; initial state; no focus; reduced motion",
            "controls":[{"name":"field\"name", "theme":"Dark", "x":-2, "y":7, "width":120, "height":30}]
        })
    );
}

#[test]
fn rejects_missing_png_signature_before_replacing_either_document() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("existing.png");
    std::fs::write(&path, b"previous image").unwrap();
    std::fs::write(dir.path().join("existing.png.json"), b"previous metadata").unwrap();
    let error = write_preview_capture(&path, b"not a PNG", &capture()).unwrap_err();
    assert!(!error.png_written);
    assert_eq!(std::fs::read(&path).unwrap(), b"previous image");
    assert_eq!(
        std::fs::read(dir.path().join("existing.png.json")).unwrap(),
        b"previous metadata"
    );
}

#[test]
fn rejects_unbounded_capture_payload_before_creating_documents() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("oversize.png");
    let mut png = vec![0; choscordb_core::MAX_PREVIEW_PNG_BYTES + 1];
    png[..8].copy_from_slice(&PNG[..8]);
    assert!(write_preview_capture(&path, &png, &capture()).is_err());
    assert!(!path.exists());
}

#[test]
fn validates_capture_metadata_before_disk_changes() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("invalid.png");
    let mut metadata = capture();
    metadata.logical_width = 10;
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    assert!(!path.exists());
    metadata = capture();
    metadata.source_device_scale = f64::NAN;
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    metadata = capture();
    metadata.themes = "Light / Dark".into();
    metadata.logical_width = 641;
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    metadata = capture();
    metadata.controls[0].width = -1;
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    metadata = capture();
    metadata.surface = "unknown".into();
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    metadata = capture();
    metadata.section = "x".repeat(4097);
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    metadata = capture();
    metadata.controls =
        vec![metadata.controls[0].clone(); choscordb_core::MAX_PREVIEW_CONTROLS + 1];
    assert!(write_preview_capture(&path, PNG, &metadata).is_err());
    assert!(!path.exists());
}

#[test]
fn overwrites_existing_capture_and_records_actual_surface_and_theme() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("updated.png");
    write_preview_capture(&path, PNG, &capture()).unwrap();
    let mut metadata = capture();
    metadata.surface = "modal".into();
    metadata.themes = "Light / Dark".into();
    metadata.logical_width = 1280;
    let replacement = b"\x89PNG\r\n\x1a\nreplacement image";
    write_preview_capture(&path, replacement, &metadata).unwrap();
    assert_eq!(std::fs::read(&path).unwrap(), replacement);
    let record: serde_json::Value =
        serde_json::from_slice(&std::fs::read(dir.path().join("updated.png.json")).unwrap())
            .unwrap();
    assert_eq!(record["themes"], "Light / Dark");
    assert_eq!(record["logicalWidth"], 1280);
    assert_eq!(record["surface"], "modal");
    assert_eq!(
        record["fixture"],
        "synthetic; open real surface; no action dispatched; reduced motion"
    );
}

#[test]
fn reports_png_failure_and_sidecar_failure_separately() {
    let dir = tempfile::tempdir().unwrap();
    let error = write_preview_capture(&dir.path().join("missing/capture.png"), PNG, &capture())
        .unwrap_err();
    assert!(!error.png_written);
    assert!(!error.message.is_empty());
    let path = dir.path().join("partial.png");
    std::fs::create_dir(dir.path().join("partial.png.json")).unwrap();
    let error = write_preview_capture(&path, PNG, &capture()).unwrap_err();
    assert!(error.png_written);
    assert!(!error.message.is_empty());
    assert_eq!(std::fs::read(path).unwrap(), PNG);
}

#[cfg(unix)]
#[test]
fn writes_documents_when_parent_allows_create_without_directory_read() {
    use std::os::unix::fs::PermissionsExt;
    struct Restore(std::path::PathBuf);
    impl Drop for Restore {
        fn drop(&mut self) {
            std::fs::set_permissions(&self.0, std::fs::Permissions::from_mode(0o700)).unwrap();
        }
    }
    let dir = tempfile::tempdir().unwrap();
    let parent = dir.path().join("write-only-directory");
    std::fs::create_dir(&parent).unwrap();
    let _restore = Restore(parent.clone());
    std::fs::set_permissions(&parent, std::fs::Permissions::from_mode(0o300)).unwrap();
    let path = parent.join("capture.png");
    write_preview_capture(&path, PNG, &capture()).unwrap();
    assert_eq!(std::fs::read(path).unwrap(), PNG);
    let record: serde_json::Value =
        serde_json::from_slice(&std::fs::read(parent.join("capture.png.json")).unwrap()).unwrap();
    assert_eq!(record["specimen"], "fields");
}

#[cfg(unix)]
#[test]
fn follows_existing_and_dangling_capture_links_without_replacing_links() {
    use std::os::unix::fs::symlink;
    let dir = tempfile::tempdir().unwrap();
    std::fs::create_dir(dir.path().join("targets")).unwrap();
    std::fs::write(dir.path().join("targets/existing.png"), b"previous image").unwrap();
    symlink("targets/existing.png", dir.path().join("existing.png")).unwrap();
    symlink("targets/dangling.png", dir.path().join("dangling.png")).unwrap();
    for name in ["existing.png", "dangling.png"] {
        let link = dir.path().join(name);
        write_preview_capture(&link, PNG, &capture()).unwrap();
        assert!(
            std::fs::symlink_metadata(&link)
                .unwrap()
                .file_type()
                .is_symlink()
        );
        assert_eq!(
            std::fs::read(dir.path().join("targets").join(name)).unwrap(),
            PNG
        );
        let record: serde_json::Value = serde_json::from_slice(
            &std::fs::read(dir.path().join(format!("{name}.json"))).unwrap(),
        )
        .unwrap();
        assert_eq!(record["specimen"], "fields");
    }
}

#[cfg(unix)]
#[test]
fn rejects_cyclic_capture_links_without_damaging_destinations() {
    use std::os::unix::fs::symlink;
    let dir = tempfile::tempdir().unwrap();
    let first = dir.path().join("first.png");
    let second = dir.path().join("second.png");
    symlink("second.png", &first).unwrap();
    symlink("first.png", &second).unwrap();
    let error = write_preview_capture(&first, PNG, &capture()).unwrap_err();
    assert!(!error.png_written);
    assert_eq!(
        std::fs::read_link(first).unwrap(),
        std::path::Path::new("second.png")
    );
    assert_eq!(
        std::fs::read_link(second).unwrap(),
        std::path::Path::new("first.png")
    );
    assert!(!dir.path().join("first.png.json").exists());
}

#[cfg(unix)]
#[test]
fn preserves_existing_readonly_capture_when_account_cannot_write() {
    use std::os::unix::fs::PermissionsExt;
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("readonly.png");
    std::fs::write(&path, b"previous image").unwrap();
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o444)).unwrap();
    assert!(
        std::fs::OpenOptions::new().write(true).open(&path).is_err(),
        "Readonly rejection requires an unprivileged test account."
    );
    let error = write_preview_capture(&path, PNG, &capture()).unwrap_err();
    assert!(!error.png_written);
    assert_eq!(std::fs::read(&path).unwrap(), b"previous image");
    assert!(!dir.path().join("readonly.png.json").exists());
}

#[cfg(unix)]
#[test]
fn writes_metadata_through_dangling_sidecar_link() {
    use std::os::unix::fs::symlink;
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("capture.png");
    let sidecar = dir.path().join("capture.png.json");
    symlink("metadata.json", &sidecar).unwrap();
    write_preview_capture(&path, PNG, &capture()).unwrap();
    assert!(
        std::fs::symlink_metadata(sidecar)
            .unwrap()
            .file_type()
            .is_symlink()
    );
    let record: serde_json::Value =
        serde_json::from_slice(&std::fs::read(dir.path().join("metadata.json")).unwrap()).unwrap();
    assert_eq!(record["specimen"], "fields");
    assert_eq!(std::fs::read(path).unwrap(), PNG);
}

#[test]
fn rejects_nonregular_capture_destination_without_changing_contents() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("directory.png");
    std::fs::create_dir(&path).unwrap();
    std::fs::write(path.join("existing"), b"untouched").unwrap();
    let error = write_preview_capture(&path, PNG, &capture()).unwrap_err();
    assert!(!error.png_written);
    assert_eq!(std::fs::read(path.join("existing")).unwrap(), b"untouched");
    assert!(!dir.path().join("directory.png.json").exists());
}
