//! Update startup acknowledgment protocol. Call waiting functions off the UI thread.
#[cfg(not(windows))]
use std::fs::File;
use std::{
    fs::OpenOptions,
    io::{Read, Write},
    path::Path,
    thread,
    time::{Duration, Instant},
};

pub fn write_update_readiness_file(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
        return false;
    };
    let Some(suffix) = name
        .strip_prefix("ChoscorDB-update-ready-")
        .and_then(|name| name.strip_suffix(".txt"))
    else {
        return false;
    };
    if !path.is_absolute()
        || !(6..=64).contains(&suffix.len())
        || !suffix.bytes().all(|byte| byte.is_ascii_alphanumeric())
    {
        return false;
    }
    let (Some(parent), Ok(temp_directory)) = (path.parent(), std::env::temp_dir().canonicalize())
    else {
        return false;
    };
    let Ok(parent_directory) = parent.canonicalize() else {
        return false;
    };
    #[cfg(windows)]
    let trusted = parent_directory
        .to_string_lossy()
        .eq_ignore_ascii_case(&temp_directory.to_string_lossy());
    #[cfg(not(windows))]
    let trusted = parent_directory == temp_directory;
    if !trusted || std::fs::symlink_metadata(path).is_ok() {
        return false;
    }
    let Ok(mut file) = OpenOptions::new().write(true).create_new(true).open(path) else {
        return false;
    };
    let written = file.write_all(b"ready\n").is_ok() && file.flush().is_ok();
    drop(file);
    if !written {
        let _ = std::fs::remove_file(path);
    }
    written
}

pub fn wait_for_update_readiness(
    path: &Path,
    timeout: Duration,
    stable: Duration,
    mut process_alive: impl FnMut() -> bool,
) -> bool {
    if timeout.is_zero() || !process_alive() {
        return false;
    }
    let started = Instant::now();
    let mut acknowledged_at = None;
    while started.elapsed() < timeout {
        if !process_alive() {
            return false;
        }
        if !marker_is_exact(path) {
            acknowledged_at = None;
        } else if let Some(first_seen) = acknowledged_at {
            if Instant::now().duration_since(first_seen) >= stable {
                return process_alive();
            }
        } else {
            acknowledged_at = Some(Instant::now());
        }
        let remaining = timeout.saturating_sub(started.elapsed());
        if remaining.is_zero() {
            break;
        }
        thread::sleep(remaining.min(Duration::from_millis(100)));
    }
    false
}

fn marker_is_exact(path: &Path) -> bool {
    let Ok(path_metadata) = std::fs::symlink_metadata(path) else {
        return false;
    };
    if !path_metadata.is_file() || path_metadata.file_type().is_symlink() {
        return false;
    }
    #[cfg(windows)]
    let opened = {
        use std::os::windows::fs::OpenOptionsExt;
        // FILE_FLAG_OPEN_REPARSE_POINT makes the opened handle refer to the link itself.
        OpenOptions::new()
            .read(true)
            .custom_flags(0x0020_0000)
            .open(path)
    };
    #[cfg(not(windows))]
    let opened = File::open(path);
    let Ok(file) = opened else {
        return false;
    };
    let Ok(open_metadata) = file.metadata() else {
        return false;
    };
    if open_metadata.len() != 6 {
        return false;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        if path_metadata.dev() != open_metadata.dev() || path_metadata.ino() != open_metadata.ino()
        {
            return false;
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        // FILE_ATTRIBUTE_REPARSE_POINT; reject a link swapped in after symlink_metadata.
        if open_metadata.file_attributes() & 0x0000_0400 != 0 {
            return false;
        }
    }
    let mut bytes = Vec::with_capacity(7);
    file.take(7).read_to_end(&mut bytes).is_ok() && bytes == b"ready\n"
}
