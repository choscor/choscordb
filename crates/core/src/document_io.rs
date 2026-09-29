//! Bounded SQL document persistence. Call from a blocking worker, never the UI thread.
use cap_fs_ext::{DirExt, FollowSymlinks, OpenOptionsFollowExt, OpenOptionsSyncExt};
use cap_std::fs::Dir;
use std::{
    collections::BTreeMap,
    fs::{self, File},
    io::{Read, Write},
    path::{Component, Path, PathBuf},
};

pub const MAX_SQL_DOCUMENT_BYTES: usize = 16 * 1024 * 1024;
pub const MAX_SAVED_SQL_DOCUMENTS: usize = 1000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SavedSqlDocument {
    pub path: PathBuf,
    pub relative_path: PathBuf,
    pub size_bytes: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SavedSqlDocuments {
    pub documents: Vec<SavedSqlDocument>,
    pub has_more: bool,
}

#[derive(Debug, thiserror::Error)]
pub enum DocumentIoError {
    #[error("Path is not a regular file.")]
    NotRegularFile,
    #[error("SQL files are limited to 16 MiB.")]
    TooLarge,
    #[error("Path is not a saved SQL file in the selected directory.")]
    InvalidSavedPath,
    #[error("{0}")]
    Io(#[from] std::io::Error),
}

/// Enumerate regular `.sql` files under the saved directory without following
/// symlinks. A missing directory is an empty collection, as on a first launch.
pub fn list_saved_sql_documents(root: &Path) -> Result<SavedSqlDocuments, DocumentIoError> {
    let mut listing = SavedSqlDocuments {
        documents: Vec::new(),
        has_more: false,
    };
    let root_directory = match open_saved_root(root) {
        Ok(directory) => directory,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(listing),
        Err(error) => return Err(error.into()),
    };
    let mut documents = BTreeMap::new();
    let mut pending = vec![(root_directory, PathBuf::new())];
    while let Some((directory, relative_dir)) = pending.pop() {
        let entries = match directory.entries() {
            Ok(entries) => entries,
            Err(_) if !relative_dir.as_os_str().is_empty() => {
                // A nested folder may disappear or become inaccessible during refresh.
                continue;
            }
            Err(error) => return Err(error.into()),
        };
        for entry in entries {
            let entry = entry?;
            let file_type = match entry.file_type() {
                Ok(file_type) => file_type,
                Err(_) => continue,
            };
            if file_type.is_symlink() {
                continue;
            }
            let name = entry.file_name();
            let relative_path = relative_dir.join(&name);
            if file_type.is_dir() {
                if let Ok(child) = directory.open_dir_nofollow(&name) {
                    pending.push((child, relative_path));
                }
            } else if file_type.is_file() && is_sql_path(&relative_path) {
                let mut options = cap_std::fs::OpenOptions::new();
                options.read(true).follow(FollowSymlinks::No).nonblock(true);
                let Ok(file) = directory.open_with(&name, &options) else {
                    continue;
                };
                let Ok(metadata) = file.metadata() else {
                    continue;
                };
                if !metadata.is_file() {
                    continue;
                }
                let document = SavedSqlDocument {
                    path: root.join(&relative_path),
                    relative_path,
                    size_bytes: metadata.len(),
                };
                documents.insert(document.relative_path.clone(), document);
                if documents.len() > MAX_SAVED_SQL_DOCUMENTS {
                    listing.has_more = true;
                    documents.pop_last();
                }
            }
        }
    }
    listing.documents = documents.into_values().collect();
    Ok(listing)
}

fn open_saved_root(root: &Path) -> std::io::Result<Dir> {
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

/// Open a saved SQL file only when every path component is still an ordinary
/// directory or file beneath the saved directory.
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

        // Each directory handle anchors the next lookup. A renamed component
        // cannot turn the final read into an open outside this directory tree.
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
        // The directory capability anchors all child opens even if a path is
        // renamed or replaced while this request is running. Open each child
        // without following a Windows reparse point.
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

fn is_sql_path(path: &Path) -> bool {
    path.extension()
        .is_some_and(|extension| extension.eq_ignore_ascii_case("sql"))
}

/// Prepare the default saved SQL directory before opening the native save dialog.
pub fn ensure_saved_sql_directory(root: &Path) -> Result<(), DocumentIoError> {
    fs::create_dir_all(root)?;
    Ok(())
}

/// Identify an existing document through its canonical path. Keep an absolute
/// fallback for a new save destination, matching the editor's tab identity.
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
    read_open_sql_document(file)
}

fn read_open_sql_document(file: File) -> Result<Vec<u8>, DocumentIoError> {
    if !file.metadata()?.is_file() {
        return Err(DocumentIoError::NotRegularFile);
    }
    if file.metadata()?.len() > MAX_SQL_DOCUMENT_BYTES as u64 {
        return Err(DocumentIoError::TooLarge);
    }
    let mut bytes = Vec::new();
    file.take((MAX_SQL_DOCUMENT_BYTES + 1) as u64)
        .read_to_end(&mut bytes)?;
    if bytes.len() > MAX_SQL_DOCUMENT_BYTES {
        return Err(DocumentIoError::TooLarge);
    }
    Ok(bytes)
}

pub fn write_sql_document(path: &Path, bytes: &[u8]) -> Result<(), DocumentIoError> {
    if bytes.len() > MAX_SQL_DOCUMENT_BYTES {
        return Err(DocumentIoError::TooLarge);
    }
    let parent = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    temporary.write_all(bytes)?;
    temporary.flush()?;
    temporary.as_file().sync_all()?;
    #[cfg(unix)]
    if let Ok(metadata) = std::fs::metadata(path) {
        temporary
            .as_file()
            .set_permissions(metadata.permissions())?;
    }
    temporary
        .persist(path)
        .map_err(|error| DocumentIoError::Io(error.error))?;
    #[cfg(unix)]
    File::open(parent)?.sync_all()?;
    Ok(())
}
