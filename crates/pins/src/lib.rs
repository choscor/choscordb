//! Versioned local pin storage and pin identity policy.
use serde_json::{Value, json};
#[cfg(windows)]
use std::fs::OpenOptions;
use std::{
    collections::HashSet,
    fs::File,
    io::{Read, Write},
    path::{Path, PathBuf},
};

const MAX_FIELD_UTF16_UNITS: usize = 1024 * 1024;
const MAX_FILE_BYTES: usize = 64 * 1024 * 1024;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct PinRecord {
    pub profile_id: String,
    pub profile_name: String,
    pub object_id: String,
    pub name: String,
    pub qualified_name: String,
    pub kind: String,
    pub parent_object_id: String,
    pub relation_subtype: String,
    pub ancestry_ids: Vec<String>,
    pub ancestry_names: Vec<String>,
    pub unavailable: bool,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct LoadResult {
    pub pins: Vec<PinRecord>,
    pub error: String,
}

#[derive(Debug, thiserror::Error)]
pub enum PinError {
    #[error("A pin has an invalid object identity or context.")]
    InvalidPin,
    #[error("The same object is pinned more than once.")]
    DuplicatePin,
    #[error("The pin file is too large to save.")]
    TooLargeToSave,
    #[error("Could not create the pin storage directory.")]
    CreateDirectory,
    #[error("Pin storage path is not a regular file.")]
    UnsafePath,
    #[error("Could not save pins: {0}")]
    Save(#[source] std::io::Error),
    #[error("Could not finish saving pins: {0}")]
    FinishSave(#[source] std::io::Error),
}

pub struct PinStore {
    path: PathBuf,
}

impl PinStore {
    pub fn for_profile_storage(path: &Path) -> Self {
        let mut file = path.as_os_str().to_os_string();
        file.push(".pins.json");
        Self { path: file.into() }
    }

    pub fn for_application_data(directory: &Path) -> Self {
        Self {
            path: directory.join("pins.json"),
        }
    }

    pub fn load(&self) -> LoadResult {
        let original = match std::fs::symlink_metadata(&self.path) {
            Ok(metadata) if metadata.is_file() && !metadata.file_type().is_symlink() => metadata,
            Ok(_) => {
                return LoadResult {
                    pins: vec![],
                    error: "Pin storage path is not a regular file.".into(),
                };
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                return LoadResult::default();
            }
            Err(error) => {
                return LoadResult {
                    pins: vec![],
                    error: format!("Could not read pins: {error}"),
                };
            }
        };
        let file = match open_pin_file(&self.path) {
            Ok(file) => file,
            Err(error) => {
                return LoadResult {
                    pins: vec![],
                    error: format!("Could not read pins: {error}"),
                };
            }
        };
        let opened = match file.metadata() {
            Ok(metadata) => metadata,
            Err(error) => {
                return LoadResult {
                    pins: vec![],
                    error: format!("Could not read pins: {error}"),
                };
            }
        };
        if !opened.is_file() || !same_file_identity(&original, &opened) {
            return LoadResult {
                pins: vec![],
                error: "Pin storage path is not a regular file.".into(),
            };
        }
        if opened.len() > MAX_FILE_BYTES as u64 {
            return LoadResult {
                pins: vec![],
                error: "The pin file is too large to read.".into(),
            };
        }
        let mut bytes = Vec::new();
        if file
            .take((MAX_FILE_BYTES + 1) as u64)
            .read_to_end(&mut bytes)
            .is_err()
            || bytes.len() > MAX_FILE_BYTES
        {
            return LoadResult {
                pins: vec![],
                error: "Could not read the complete pin file.".into(),
            };
        }
        let Ok(document) = serde_json::from_slice::<Value>(&bytes) else {
            return LoadResult {
                pins: vec![],
                error: "The pin file is malformed.".into(),
            };
        };
        let Some(root) = document.as_object() else {
            return LoadResult {
                pins: vec![],
                error: "The pin file is malformed.".into(),
            };
        };
        if root.get("version").and_then(Value::as_f64) != Some(1.0)
            || !root.get("pins").is_some_and(Value::is_array)
        {
            return LoadResult {
                pins: vec![],
                error: "The pin file version or structure is unsupported.".into(),
            };
        }
        let mut pins = Vec::new();
        let mut identities = HashSet::new();
        let mut rejected = 0;
        for value in root["pins"].as_array().into_iter().flatten() {
            if let Some(pin) = read_pin(value) {
                let key = identity_key(&pin);
                if identities.insert(key) {
                    pins.push(pin);
                    continue;
                }
            }
            rejected += 1;
        }
        LoadResult {
            pins,
            error: if rejected == 0 {
                String::new()
            } else {
                format!("Skipped {rejected} invalid or duplicate pin record(s).")
            },
        }
    }

    pub fn save(&self, pins: &[PinRecord]) -> Result<(), PinError> {
        let mut identities = HashSet::new();
        let mut array = Vec::with_capacity(pins.len());
        for pin in pins {
            if !valid(pin) {
                return Err(PinError::InvalidPin);
            }
            if !identities.insert(identity_key(pin)) {
                return Err(PinError::DuplicatePin);
            }
            array.push(json_pin(pin));
        }
        let bytes = serde_json::to_vec(&json!({"version": 1, "pins": array}))
            .map_err(|_| PinError::TooLargeToSave)?;
        if bytes.len() > MAX_FILE_BYTES {
            return Err(PinError::TooLargeToSave);
        }
        let parent = self
            .path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .unwrap_or_else(|| Path::new("."));
        std::fs::create_dir_all(parent).map_err(|_| PinError::CreateDirectory)?;
        let original = match std::fs::symlink_metadata(&self.path) {
            Ok(metadata) if metadata.is_file() && !metadata.file_type().is_symlink() => {
                Some(metadata)
            }
            Ok(_) => return Err(PinError::UnsafePath),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
            Err(error) => return Err(PinError::Save(error)),
        };
        let mut temporary = tempfile::NamedTempFile::new_in(parent).map_err(PinError::Save)?;
        temporary.write_all(&bytes).map_err(PinError::Save)?;
        temporary.flush().map_err(PinError::Save)?;
        #[cfg(unix)]
        if let Some(metadata) = &original {
            temporary
                .as_file()
                .set_permissions(metadata.permissions())
                .map_err(PinError::Save)?;
        }
        #[cfg(not(unix))]
        let _ = &original;
        temporary.as_file().sync_all().map_err(PinError::Save)?;
        temporary
            .persist(&self.path)
            .map_err(|error| PinError::FinishSave(error.error))?;
        #[cfg(unix)]
        File::open(parent)
            .and_then(|directory| directory.sync_all())
            .map_err(PinError::FinishSave)?;
        Ok(())
    }
}

fn open_pin_file(path: &Path) -> std::io::Result<File> {
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
        // FILE_FLAG_OPEN_REPARSE_POINT makes a swapped link visible on the handle.
        OpenOptions::new()
            .read(true)
            .custom_flags(0x0020_0000)
            .open(path)
    }
    #[cfg(not(any(unix, windows)))]
    {
        File::open(path)
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
        // FILE_ATTRIBUTE_REPARSE_POINT: the nofollow handle must be a regular file.
        opened.file_attributes() & 0x0000_0400 == 0 && original.file_attributes() & 0x0000_0400 == 0
    }
    #[cfg(not(any(unix, windows)))]
    {
        original.len() == opened.len()
    }
}

pub fn valid(pin: &PinRecord) -> bool {
    let direct_root_schema =
        pin.kind == "schema" && pin.parent_object_id.is_empty() && pin.ancestry_ids.is_empty();
    safe_text(&pin.profile_id, true)
        && safe_text(&pin.profile_name, false)
        && safe_text(&pin.object_id, true)
        && safe_text(&pin.name, true)
        && safe_text(&pin.qualified_name, true)
        && choscordb_driver_api::object_kind_traits(&pin.kind).pinnable
        && (direct_root_schema || safe_text(&pin.parent_object_id, true))
        && safe_text(&pin.relation_subtype, false)
        && (pin.ancestry_names.is_empty() || pin.ancestry_names.len() == pin.ancestry_ids.len())
        && pin.ancestry_ids.iter().all(|value| safe_text(value, true))
        && pin
            .ancestry_names
            .iter()
            .all(|value| safe_text(value, true))
}

pub fn identity_key(pin: &PinRecord) -> String {
    if !valid(pin) {
        return String::new();
    }
    serde_json::to_string(&[
        &pin.profile_id,
        &pin.kind,
        &pin.object_id,
        &pin.qualified_name,
        &pin.relation_subtype,
    ])
    .expect("pin strings serialize")
}

fn safe_text(text: &str, required: bool) -> bool {
    (!required || !text.is_empty())
        && !text.contains('\0')
        && text.encode_utf16().count() <= MAX_FIELD_UTF16_UNITS
}

fn json_pin(pin: &PinRecord) -> Value {
    json!({
        "profileId": pin.profile_id,
        "profileName": pin.profile_name,
        "objectId": pin.object_id,
        "name": pin.name,
        "qualifiedName": pin.qualified_name,
        "kind": pin.kind,
        "parentObjectId": pin.parent_object_id,
        "relationSubtype": pin.relation_subtype,
        "ancestryIds": pin.ancestry_ids,
        "ancestryNames": pin.ancestry_names,
        "unavailable": pin.unavailable,
    })
}

fn read_pin(value: &Value) -> Option<PinRecord> {
    let object = value.as_object()?;
    let string = |key| object.get(key)?.as_str().map(str::to_owned);
    let strings = |key| {
        object
            .get(key)?
            .as_array()?
            .iter()
            .map(|value| value.as_str().map(str::to_owned))
            .collect::<Option<Vec<_>>>()
    };
    let pin = PinRecord {
        profile_id: string("profileId")?,
        profile_name: string("profileName")?,
        object_id: string("objectId")?,
        name: string("name")?,
        qualified_name: string("qualifiedName")?,
        kind: string("kind")?,
        parent_object_id: string("parentObjectId")?,
        relation_subtype: string("relationSubtype")?,
        ancestry_ids: strings("ancestryIds")?,
        ancestry_names: strings("ancestryNames")?,
        unavailable: object.get("unavailable")?.as_bool()?,
    };
    valid(&pin).then_some(pin)
}

/// The pin list after pinning or unpinning `candidate`; `None` when nothing changes.
/// New pins go first.
pub fn toggle_pin(pins: &[PinRecord], candidate: PinRecord, unpin: bool) -> Option<Vec<PinRecord>> {
    if !valid(&candidate) {
        return None;
    }
    let key = identity_key(&candidate);
    let existing = pins.iter().position(|pin| identity_key(pin) == key);
    match (existing, unpin) {
        (Some(row), true) => {
            let mut updated = pins.to_vec();
            updated.remove(row);
            Some(updated)
        }
        (None, false) => Some(
            std::iter::once(candidate)
                .chain(pins.iter().cloned())
                .collect(),
        ),
        _ => None,
    }
}

/// The pin list without the pin whose identity key is `key`.
pub fn remove_pin(pins: &[PinRecord], key: &str) -> Option<Vec<PinRecord>> {
    let row = pins.iter().position(|pin| identity_key(pin) == key)?;
    let mut updated = pins.to_vec();
    updated.remove(row);
    Some(updated)
}

/// The pin list without the pins of a deleted connection profile.
pub fn remove_profile_pins(pins: &[PinRecord], profile_id: &str) -> Option<Vec<PinRecord>> {
    let updated: Vec<_> = pins
        .iter()
        .filter(|pin| pin.profile_id != profile_id)
        .cloned()
        .collect();
    (updated.len() != pins.len()).then_some(updated)
}
