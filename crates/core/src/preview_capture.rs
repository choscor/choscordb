//! Developer capture documents. Invoke persistence from a blocking worker.
use std::path::Path;

pub const MAX_PREVIEW_PNG_BYTES: usize = 32 * 1024 * 1024;
pub const MAX_PREVIEW_CONTROLS: usize = 20_000;

#[derive(Debug, Clone, Default)]
pub struct PreviewControl {
    pub name: String,
    pub theme: String,
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

#[derive(Debug, Clone, Default)]
pub struct PreviewCapture {
    pub section: String,
    pub specimen: String,
    pub source: String,
    pub surface: String,
    pub logical_width: u32,
    pub logical_height: u32,
    pub source_device_scale: f64,
    pub themes: String,
    pub font: String,
    pub qt: String,
    pub platform: String,
    pub os: String,
    pub controls: Vec<PreviewControl>,
}

#[derive(Debug, thiserror::Error)]
#[error("{message}")]
pub struct PreviewCaptureError {
    pub png_written: bool,
    pub message: String,
}

pub fn write_preview_capture(
    path: &Path,
    png: &[u8],
    metadata: &PreviewCapture,
) -> Result<(), PreviewCaptureError> {
    if png.len() > MAX_PREVIEW_PNG_BYTES {
        return Err(failure(false, "Capture PNG exceeds 32 MiB."));
    }
    if !png.starts_with(b"\x89PNG\r\n\x1a\n") {
        return Err(failure(false, "Capture PNG signature is invalid."));
    }
    validate_metadata(metadata)?;
    let controls: Vec<_> = metadata
        .controls
        .iter()
        .map(|control| {
            serde_json::json!({
                "name":control.name, "theme":control.theme, "x":control.x, "y":control.y,
                "width":control.width, "height":control.height,
            })
        })
        .collect();
    let record = serde_json::json!({
        "section":metadata.section, "specimen":metadata.specimen, "source":metadata.source,
        "surface":metadata.surface, "logicalWidth":metadata.logical_width,
        "logicalHeight":metadata.logical_height, "scale":1,
        "sourceDeviceScale":metadata.source_device_scale,
        "rendering":"QWidget logical-pixel render; native popup content; excludes OS shell",
        "themes":metadata.themes, "font":metadata.font, "qt":metadata.qt,
        "platform":metadata.platform, "os":metadata.os,
        "fixture":if metadata.surface == "inline" {
            "synthetic; initial state; no focus; reduced motion"
        } else {
            "synthetic; open real surface; no action dispatched; reduced motion"
        }, "controls":controls,
    });
    let bytes = serde_json::to_vec_pretty(&record).map_err(|error| failure(false, error))?;
    atomic_write(path, png).map_err(|error| failure(false, error))?;
    let mut sidecar = path.as_os_str().to_os_string();
    sidecar.push(".json");
    atomic_write(Path::new(&sidecar), &bytes).map_err(|error| failure(true, error))?;
    Ok(())
}

fn validate_metadata(metadata: &PreviewCapture) -> Result<(), PreviewCaptureError> {
    let comparison = metadata.themes == "Light / Dark";
    if !matches!(metadata.themes.as_str(), "Light" | "Dark" | "Light / Dark")
        || !(if comparison { 640 } else { 320 }..=2560).contains(&metadata.logical_width)
        || !(320..=1800).contains(&metadata.logical_height)
        || (comparison && !metadata.logical_width.is_multiple_of(2))
        || !metadata.source_device_scale.is_finite()
        || metadata.source_device_scale <= 0.0
        || !matches!(
            metadata.surface.as_str(),
            "inline" | "menu" | "nonmodal" | "modal" | "selector-popup" | "tooltip"
        )
        || metadata.controls.len() > MAX_PREVIEW_CONTROLS
    {
        return Err(failure(false, "Invalid capture metadata."));
    }
    let strings = [
        &metadata.section,
        &metadata.specimen,
        &metadata.source,
        &metadata.surface,
        &metadata.themes,
        &metadata.font,
        &metadata.qt,
        &metadata.platform,
        &metadata.os,
    ];
    let mut text_bytes = 0;
    for text in strings.into_iter().chain(
        metadata
            .controls
            .iter()
            .flat_map(|control| [&control.name, &control.theme]),
    ) {
        if text.len() > 4096 {
            return Err(failure(false, "Capture metadata text exceeds its limit."));
        }
        text_bytes += text.len();
    }
    if text_bytes > 1024 * 1024
        || metadata.controls.iter().any(|control| {
            control.width < 0
                || control.height < 0
                || !matches!(control.theme.as_str(), "Light" | "Dark")
        })
    {
        return Err(failure(false, "Invalid capture controls."));
    }
    Ok(())
}

fn failure(png_written: bool, error: impl std::fmt::Display) -> PreviewCaptureError {
    PreviewCaptureError {
        png_written,
        message: error.to_string(),
    }
}

fn atomic_write(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    use std::io::Write;
    let destination = resolve_capture_destination(path)?;
    let path = destination.as_path();
    let permissions = match std::fs::metadata(path) {
        Ok(metadata) => {
            if !metadata.is_file() {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidInput,
                    "Capture destination is not a regular file.",
                ));
            }
            // Atomic replacement only needs a writable directory. Preserve
            // QSaveFile's separate check of this account's access to the file.
            // Open without truncating; permission bits alone miss ACLs/root.
            std::fs::OpenOptions::new().write(true).open(path)?;
            Some(metadata.permissions())
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
        Err(error) => return Err(error),
    };
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    temporary.write_all(bytes)?;
    temporary.flush()?;
    temporary.as_file().sync_all()?;
    if let Some(permissions) = permissions {
        temporary.as_file().set_permissions(permissions)?;
    }
    // The atomic rename commits this developer artifact. Report no fallible
    // work after commit: callers must know whether PNG replacement succeeded.
    temporary.persist(path).map_err(|error| error.error)?;
    Ok(())
}

// Preserve the gallery's former QSaveFile behavior, including dangling final
// links. Parent-directory links remain the filesystem's responsibility.
fn resolve_capture_destination(path: &Path) -> std::io::Result<std::path::PathBuf> {
    let mut destination = path.to_path_buf();
    for _ in 0..128 {
        match std::fs::symlink_metadata(&destination) {
            Ok(metadata) if metadata.file_type().is_symlink() => {
                let target = std::fs::read_link(&destination)?;
                destination = if target.is_absolute() {
                    target
                } else {
                    destination
                        .parent()
                        .unwrap_or_else(|| Path::new("."))
                        .join(target)
                };
            }
            Ok(_) => return Ok(destination),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(destination),
            Err(error) => return Err(error),
        }
    }
    Err(std::io::Error::new(
        std::io::ErrorKind::InvalidInput,
        "Capture destination has too many symbolic links.",
    ))
}
