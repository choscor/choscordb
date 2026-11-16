#[cfg(windows)]
use cap_fs_ext::{DirExt, FollowSymlinks, OpenOptionsFollowExt};
use cap_std::fs::Dir;
use std::{
    fs::{self, File},
    io::{Read, Write},
    path::{Component, Path, PathBuf},
};

pub const MAX_SQL_DOCUMENT_BYTES: usize = 16 * 1024 * 1024;
#[derive(Debug, thiserror::Error)]
pub enum DocumentIoError {
    #[error("Path is not a regular file.")]
    NotRegularFile,
    #[error("SQL files are limited to 16 MiB.")]
    TooLarge,
    #[error("Cell files are limited to 64 KiB.")]
    CellTooLarge,
    #[error("Path is not a saved SQL file in the selected directory.")]
    InvalidSavedPath,
    #[error("{0}")]
    Io(#[from] std::io::Error),
}

pub(crate) fn open_saved_root(root: &Path) -> std::io::Result<Dir> {
    #[cfg(unix)]
    {
        use rustix::fs::{CWD, Mode, OFlags, openat};
        let handle = openat(
            CWD,
            root,
            OFlags::RDONLY | OFlags::DIRECTORY | OFlags::NOFOLLOW,
            Mode::empty(),
        )
        .map_err(std::io::Error::from)?;
        Ok(Dir::from_std_file(File::from(handle)))
    }
    #[cfg(windows)]
    {
        use cap_fs_ext::OpenOptionsMaybeDirExt;
        let mut options = cap_std::fs::OpenOptions::new();
        options
            .read(true)
            .follow(FollowSymlinks::No)
            .maybe_dir(true);
        let file =
            cap_std::fs::File::open_ambient_with(root, &options, cap_std::ambient_authority())?;
        if !file.metadata()?.is_dir() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::NotADirectory,
                "saved SQL path is not a directory",
            ));
        }
        Ok(Dir::from_std_file(file.into_std()))
    }
    #[cfg(not(any(unix, windows)))]
    {
        let _ = root;
        Err(std::io::Error::new(
            std::io::ErrorKind::Unsupported,
            "saved SQL directories are unsupported on this platform",
        ))
    }
}

pub fn read_saved_sql_document(root: &Path, path: &Path) -> Result<Vec<u8>, DocumentIoError> {
    let relative = path
        .strip_prefix(root)
        .map_err(|_| DocumentIoError::InvalidSavedPath)?;
    if !is_sql_path(path)
        || relative.as_os_str().is_empty()
        || relative
            .components()
            .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err(DocumentIoError::InvalidSavedPath);
    }
    #[cfg(unix)]
    {
        use rustix::fs::{CWD, Mode, OFlags, openat};

        let directory_flags = OFlags::RDONLY | OFlags::DIRECTORY | OFlags::NOFOLLOW;
        let mut directory =
            openat(CWD, root, directory_flags, Mode::empty()).map_err(std::io::Error::from)?;
        let components: Vec<_> = relative.components().collect();
        for component in &components[..components.len() - 1] {
            directory = openat(
                &directory,
                component.as_os_str(),
                directory_flags,
                Mode::empty(),
            )
            .map_err(|_| DocumentIoError::InvalidSavedPath)?;
        }
        let file = openat(
            &directory,
            components.last().unwrap().as_os_str(),
            OFlags::RDONLY | OFlags::NOFOLLOW | OFlags::NONBLOCK,
            Mode::empty(),
        )
        .map_err(|_| DocumentIoError::InvalidSavedPath)?;
        read_open_sql_document(File::from(file))
    }
    #[cfg(windows)]
    {
        let mut directory = open_saved_root(root)?;
        let components: Vec<_> = relative.components().collect();
        for component in &components[..components.len() - 1] {
            directory = directory
                .open_dir_nofollow(component.as_os_str())
                .map_err(|_| DocumentIoError::InvalidSavedPath)?;
        }
        let mut options = cap_std::fs::OpenOptions::new();
        options.read(true).follow(FollowSymlinks::No);
        let file = directory
            .open_with(components.last().unwrap().as_os_str(), &options)
            .map_err(|_| DocumentIoError::InvalidSavedPath)?;
        read_open_sql_document(file.into_std())
    }
    #[cfg(not(any(unix, windows)))]
    {
        Err(DocumentIoError::InvalidSavedPath)
    }
}

pub(crate) fn is_sql_path(path: &Path) -> bool {
    path.extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("sql"))
}

pub fn saved_sql_root(storage_path: &Path, default_data_directory: &Path) -> PathBuf {
    match storage_path.parent() {
        Some(directory) if !storage_path.as_os_str().is_empty() => directory.join("sql"),
        _ => default_data_directory.join("sql"),
    }
}

pub fn ensure_saved_sql_directory(root: &Path) -> Result<(), DocumentIoError> {
    fs::create_dir_all(root)?;
    Ok(())
}

pub fn saved_sql_document_identity(path: &Path) -> Result<PathBuf, DocumentIoError> {
    if let Ok(canonical) = path.canonicalize() {
        return Ok(canonical);
    }
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir()?.join(path)
    };
    let mut cleaned = PathBuf::new();
    for component in absolute.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                cleaned.pop();
            }
            _ => cleaned.push(component.as_os_str()),
        }
    }
    Ok(cleaned)
}

pub fn read_sql_document(path: &Path) -> Result<Vec<u8>, DocumentIoError> {
    read_open_sql_document(open_regular_candidate(path)?)
}

pub fn read_cell_file(path: &Path) -> Result<Vec<u8>, DocumentIoError> {
    read_limited(
        open_regular_candidate(path)?,
        choscordb_driver_api::MAX_GRID_BINARY_BYTES,
        DocumentIoError::CellTooLarge,
    )
}

pub fn write_cell_file(path: &Path, bytes: &[u8]) -> Result<(), DocumentIoError> {
    write_atomically(path, bytes)
}

fn open_regular_candidate(path: &Path) -> Result<File, DocumentIoError> {
    #[cfg(unix)]
    let file = {
        use rustix::fs::{CWD, Mode, OFlags, openat};
        File::from(
            openat(CWD, path, OFlags::RDONLY | OFlags::NONBLOCK, Mode::empty())
                .map_err(std::io::Error::from)?,
        )
    };
    #[cfg(not(unix))]
    let file = File::open(path)?;
    Ok(file)
}

fn read_open_sql_document(file: File) -> Result<Vec<u8>, DocumentIoError> {
    read_limited(file, MAX_SQL_DOCUMENT_BYTES, DocumentIoError::TooLarge)
}

fn read_limited(
    file: File,
    limit: usize,
    too_large: DocumentIoError,
) -> Result<Vec<u8>, DocumentIoError> {
    if !file.metadata()?.is_file() {
        return Err(DocumentIoError::NotRegularFile);
    }
    if file.metadata()?.len() > limit as u64 {
        return Err(too_large);
    }
    let mut bytes = Vec::new();
    file.take((limit + 1) as u64).read_to_end(&mut bytes)?;
    if bytes.len() > limit {
        return Err(too_large);
    }
    Ok(bytes)
}

pub fn write_sql_document(path: &Path, bytes: &[u8]) -> Result<(), DocumentIoError> {
    if bytes.len() > MAX_SQL_DOCUMENT_BYTES {
        return Err(DocumentIoError::TooLarge);
    }
    write_atomically(path, bytes)
}

fn write_atomically(path: &Path, bytes: &[u8]) -> Result<(), DocumentIoError> {
    write_atomically_with(path, |file| {
        file.write_all(bytes).map_err(DocumentIoError::from)
    })
}

pub(crate) fn write_atomically_with<E: From<std::io::Error>>(
    path: &Path,
    fill: impl FnOnce(&mut File) -> Result<(), E>,
) -> Result<(), E> {
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    fill(temporary.as_file_mut())?;
    temporary.flush()?;
    temporary.as_file().sync_all()?;
    #[cfg(unix)]
    if let Ok(metadata) = std::fs::metadata(path) {
        temporary
            .as_file()
            .set_permissions(metadata.permissions())?;
    }
    temporary.persist(path).map_err(|error| error.error)?;
    #[cfg(unix)]
    File::open(parent)?.sync_all()?;
    Ok(())
}

pub fn document_path_exists(path: &Path) -> Result<bool, std::io::Error> {
    match fs::metadata(path) {
        Ok(_) => Ok(true),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(false),
        Err(error) => Err(error),
    }
}
