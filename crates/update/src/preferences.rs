//! Durable update consent and migration from the previous native Qt preference.
use serde_json::{Value, json};
use std::{
    fs::File,
    io::{Read, Write},
    path::{Path, PathBuf},
};

const MAX_PREFERENCE_BYTES: u64 = 4096;
const MAX_LEGACY_BYTES: u64 = 1024 * 1024;

#[derive(Debug, thiserror::Error)]
pub enum UpdatePreferenceError {
    #[error("Update preference path is unsafe")]
    UnsafePath,
    #[error("Update preference file is malformed")]
    Malformed,
    #[error("Update preference file is too large")]
    TooLarge,
    #[error("Could not access update preferences: {0}")]
    Io(#[from] std::io::Error),
}

pub struct UpdatePreferenceStore {
    directory: PathBuf,
}

impl UpdatePreferenceStore {
    pub fn load_with_native_legacy(&self) -> Result<Option<bool>, UpdatePreferenceError> {
        if let Some(saved) = self.load()? {
            return Ok(Some(saved));
        }
        #[cfg(target_os = "linux")]
        {
            let user_config = std::env::var_os("XDG_CONFIG_HOME")
                .filter(|value| !value.is_empty())
                .map(PathBuf::from)
                .filter(|path| path.is_absolute())
                .or_else(|| {
                    std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".config"))
                });
            let system_config = std::env::var_os("XDG_CONFIG_DIRS")
                .filter(|value| !value.is_empty())
                .map(|value| {
                    std::env::split_paths(&value)
                        .filter(|path| path.is_absolute())
                        .collect::<Vec<_>>()
                })
                .unwrap_or_else(|| vec![PathBuf::from("/etc/xdg")]);
            self.load_with_legacy_ini(&legacy_config_paths(user_config.as_deref(), &system_config))
        }
        #[cfg(target_os = "windows")]
        {
            use winreg::{
                RegKey,
                enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE},
            };
            self.load_with_legacy_registry(
                &RegKey::predef(HKEY_CURRENT_USER),
                &RegKey::predef(HKEY_LOCAL_MACHINE),
            )
        }
        #[cfg(not(any(target_os = "linux", target_os = "windows")))]
        {
            Ok(None)
        }
    }

    #[cfg(target_os = "windows")]
    pub fn load_with_legacy_registry(
        &self,
        user: &winreg::RegKey,
        system: &winreg::RegKey,
    ) -> Result<Option<bool>, UpdatePreferenceError> {
        if let Some(saved) = self.load()? {
            return Ok(Some(saved));
        }
        for hive in [user, system] {
            for application in ["ChoscorDB", "OrganizationDefaults"] {
                let path = format!("Software\\com.choscor.ChoscorDB\\{application}\\updates");
                if let Some(value) = read_registry_consent(hive, &path)? {
                    self.save(value)?;
                    return Ok(Some(value));
                }
            }
        }
        Ok(None)
    }

    pub fn load_with_legacy_config(
        &self,
        user_config: &Path,
        system_config: &[PathBuf],
    ) -> Result<Option<bool>, UpdatePreferenceError> {
        self.load_with_legacy_ini(&legacy_config_paths(Some(user_config), system_config))
    }

    pub fn load_with_legacy_ini(
        &self,
        paths: &[PathBuf],
    ) -> Result<Option<bool>, UpdatePreferenceError> {
        if let Some(saved) = self.load()? {
            return Ok(Some(saved));
        }
        for path in paths {
            if let Some(value) = read_legacy_ini(path)? {
                self.save(value)?;
                return Ok(Some(value));
            }
        }
        Ok(None)
    }

    pub fn new(application_data_directory: &Path) -> Self {
        Self {
            directory: application_data_directory.to_path_buf(),
        }
    }

    fn path(&self) -> Result<PathBuf, UpdatePreferenceError> {
        if !self.directory.is_absolute() {
            return Err(UpdatePreferenceError::UnsafePath);
        }
        Ok(self.directory.join("update-preferences.json"))
    }

    pub fn load(&self) -> Result<Option<bool>, UpdatePreferenceError> {
        let path = self.path()?;
        let original = match std::fs::symlink_metadata(&path) {
            Ok(metadata) if metadata.is_file() && !metadata.file_type().is_symlink() => metadata,
            Ok(_) => return Err(UpdatePreferenceError::UnsafePath),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(error) => return Err(error.into()),
        };
        if original.len() > MAX_PREFERENCE_BYTES {
            return Err(UpdatePreferenceError::TooLarge);
        }
        let file = open_preference(&path)?;
        let opened = file.metadata()?;
        if !opened.is_file() || !same_file_identity(&original, &opened) {
            return Err(UpdatePreferenceError::UnsafePath);
        }
        let mut bytes = Vec::new();
        file.take(MAX_PREFERENCE_BYTES + 1)
            .read_to_end(&mut bytes)?;
        if bytes.len() as u64 > MAX_PREFERENCE_BYTES {
            return Err(UpdatePreferenceError::TooLarge);
        }
        let root: Value =
            serde_json::from_slice(&bytes).map_err(|_| UpdatePreferenceError::Malformed)?;
        if root.get("version").and_then(Value::as_u64) != Some(1) {
            return Err(UpdatePreferenceError::Malformed);
        }
        root.get("background_consent")
            .and_then(Value::as_bool)
            .map(Some)
            .ok_or(UpdatePreferenceError::Malformed)
    }

    pub fn load_or_migrate(
        &self,
        legacy: Option<bool>,
    ) -> Result<Option<bool>, UpdatePreferenceError> {
        if let Some(saved) = self.load()? {
            return Ok(Some(saved));
        }
        if let Some(value) = legacy {
            self.save(value)?;
        }
        Ok(legacy)
    }

    pub fn save(&self, value: bool) -> Result<(), UpdatePreferenceError> {
        let path = self.path()?;
        std::fs::create_dir_all(&self.directory)?;
        match std::fs::symlink_metadata(&path) {
            Ok(metadata) if metadata.is_file() && !metadata.file_type().is_symlink() => {}
            Ok(_) => return Err(UpdatePreferenceError::UnsafePath),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => return Err(error.into()),
        }
        let bytes = serde_json::to_vec(&json!({"version": 1, "background_consent": value}))
            .map_err(|_| UpdatePreferenceError::Malformed)?;
        let mut temporary = tempfile::NamedTempFile::new_in(&self.directory)?;
        temporary.write_all(&bytes)?;
        temporary.flush()?;
        temporary.as_file().sync_all()?;
        temporary.persist(&path).map_err(|error| error.error)?;
        #[cfg(unix)]
        File::open(&self.directory)?.sync_all()?;
        Ok(())
    }
}

fn legacy_config_paths(user_config: Option<&Path>, system_config: &[PathBuf]) -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(user) = user_config {
        paths.push(user.join("com.choscor.ChoscorDB/ChoscorDB.conf"));
        paths.push(user.join("com.choscor.ChoscorDB.conf"));
    }
    // Qt checks every system application file before any system organization file.
    for suffix in [
        "com.choscor.ChoscorDB/ChoscorDB.conf",
        "com.choscor.ChoscorDB.conf",
    ] {
        paths.extend(system_config.iter().map(|directory| directory.join(suffix)));
    }
    paths
}

#[cfg(target_os = "windows")]
fn read_registry_consent(
    hive: &winreg::RegKey,
    path: &str,
) -> Result<Option<bool>, UpdatePreferenceError> {
    use winreg::enums::{KEY_READ, REG_DWORD, REG_SZ};
    let key = match hive.open_subkey_with_flags(path, KEY_READ) {
        Ok(key) => key,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    let raw = match key.get_raw_value("backgroundConsent") {
        Ok(raw) => raw,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    let value = match raw.vtype {
        REG_DWORD if raw.bytes.len() == 4 => {
            match u32::from_le_bytes(raw.bytes.try_into().expect("four bytes")) {
                0 => false,
                1 => true,
                _ => return Err(UpdatePreferenceError::Malformed),
            }
        }
        REG_SZ if raw.bytes.len() % 2 == 0 && raw.bytes.len() <= MAX_LEGACY_BYTES as usize => {
            let units = raw
                .bytes
                .chunks_exact(2)
                .map(|pair| u16::from_le_bytes([pair[0], pair[1]]))
                .collect::<Vec<_>>();
            let text = String::from_utf16(&units).map_err(|_| UpdatePreferenceError::Malformed)?;
            parse_legacy_bool(text.strip_suffix('\0').unwrap_or(&text))?
        }
        _ => return Err(UpdatePreferenceError::Malformed),
    };
    Ok(Some(value))
}

fn parse_legacy_bool(value: &str) -> Result<bool, UpdatePreferenceError> {
    match value {
        "true" | "1" => Ok(true),
        "false" | "0" => Ok(false),
        _ => Err(UpdatePreferenceError::Malformed),
    }
}

fn read_legacy_ini(path: &Path) -> Result<Option<bool>, UpdatePreferenceError> {
    if !path.is_absolute() {
        return Err(UpdatePreferenceError::UnsafePath);
    }
    let original = match std::fs::symlink_metadata(path) {
        Ok(metadata) if metadata.is_file() && !metadata.file_type().is_symlink() => metadata,
        Ok(_) => return Err(UpdatePreferenceError::UnsafePath),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    if original.len() > MAX_LEGACY_BYTES {
        return Err(UpdatePreferenceError::TooLarge);
    }
    let file = open_preference(path)?;
    let opened = file.metadata()?;
    if !opened.is_file() || !same_file_identity(&original, &opened) {
        return Err(UpdatePreferenceError::UnsafePath);
    }
    let mut bytes = Vec::new();
    file.take(MAX_LEGACY_BYTES + 1).read_to_end(&mut bytes)?;
    if bytes.len() as u64 > MAX_LEGACY_BYTES {
        return Err(UpdatePreferenceError::TooLarge);
    }
    let content = std::str::from_utf8(&bytes).map_err(|_| UpdatePreferenceError::Malformed)?;
    let mut section = "";
    let mut consent = None;
    for line in content.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with([';', '#']) {
            continue;
        }
        if let Some(name) = line
            .strip_prefix('[')
            .and_then(|line| line.strip_suffix(']'))
        {
            section = name;
        } else if section == "updates"
            && let Some((key, value)) = line.split_once('=')
            && key.trim() == "backgroundConsent"
        {
            consent = Some(parse_legacy_bool(value.trim())?);
        }
    }
    Ok(consent)
}

fn open_preference(path: &Path) -> Result<File, UpdatePreferenceError> {
    #[cfg(unix)]
    {
        use rustix::fs::{CWD, Mode, OFlags, openat};
        let fd = openat(
            CWD,
            path,
            OFlags::RDONLY | OFlags::NOFOLLOW | OFlags::NONBLOCK,
            Mode::empty(),
        )
        .map_err(std::io::Error::from)?;
        Ok(File::from(fd))
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::OpenOptionsExt;
        Ok(std::fs::OpenOptions::new()
            .read(true)
            .custom_flags(0x0020_0000)
            .open(path)?)
    }
    #[cfg(not(any(unix, windows)))]
    {
        Ok(File::open(path)?)
    }
}

fn same_file_identity(original: &std::fs::Metadata, opened: &std::fs::Metadata) -> bool {
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        original.dev() == opened.dev() && original.ino() == opened.ino()
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        original.file_attributes() & 0x0000_0400 == 0 && opened.file_attributes() & 0x0000_0400 == 0
    }
    #[cfg(not(any(unix, windows)))]
    {
        original.len() == opened.len()
    }
}
