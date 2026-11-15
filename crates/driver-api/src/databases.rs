use crate::{DriverError, ErrorKind, Result};
use serde::{Deserialize, Serialize};
use std::path::Path;

pub const MAX_DATABASE_CHOICES: usize = 10_000;
pub const SQLITE_DATABASE_EXTENSIONS: [&str; 3] = ["db", "sqlite", "sqlite3"];

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct DatabaseChoice {
    pub label: String,
    pub target: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct DatabaseList {
    pub choices: Vec<DatabaseChoice>,
    pub current: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DatabaseTargetField {
    Path,
    Database,
}

pub fn database_target_field(driver: &str) -> Option<DatabaseTargetField> {
    match driver {
        "sqlite" => Some(DatabaseTargetField::Path),
        "postgres" | "mysql" | "mariadb" => Some(DatabaseTargetField::Database),
        _ => None,
    }
}

fn too_many() -> DriverError {
    DriverError::new(
        ErrorKind::ResourceLimit,
        format!("The connection has more than {MAX_DATABASE_CHOICES} databases."),
    )
}

pub fn named_database_list(names: Vec<String>, current: String) -> Result<DatabaseList> {
    if names.len() > MAX_DATABASE_CHOICES {
        return Err(too_many());
    }
    Ok(DatabaseList {
        choices: names
            .into_iter()
            .map(|name| DatabaseChoice {
                label: name.clone(),
                target: name,
            })
            .collect(),
        current,
    })
}

pub fn sqlite_sibling_databases(current: &Path) -> Result<DatabaseList> {
    let path = current.to_str().ok_or_else(|| {
        DriverError::new(
            ErrorKind::InvalidInput,
            "The SQLite database path is not valid Unicode.",
        )
    })?;
    if path.is_empty() || path == ":memory:" || path.starts_with("file:") {
        return Err(DriverError::new(
            ErrorKind::Unsupported,
            "Only a SQLite database file has sibling database files.",
        ));
    }
    let folder = match current.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => parent,
        _ => Path::new("."),
    };
    let io = |error: std::io::Error| {
        DriverError::new(
            ErrorKind::Io,
            format!("Cannot list the database files in the folder: {error}"),
        )
    };
    let mut choices = Vec::new();
    for entry in std::fs::read_dir(folder).map_err(io)? {
        let entry = entry.map_err(io)?;
        let file = entry.path();
        let listed = file
            .extension()
            .and_then(|extension| extension.to_str())
            .is_some_and(|extension| SQLITE_DATABASE_EXTENSIONS.contains(&extension));
        if !listed || !entry.file_type().map_err(io)?.is_file() {
            continue;
        }
        let (Some(label), Some(target)) = (
            entry.file_name().to_str().map(str::to_owned),
            file.to_str().map(str::to_owned),
        ) else {
            continue;
        };
        if choices.len() == MAX_DATABASE_CHOICES {
            return Err(too_many());
        }
        choices.push(DatabaseChoice { label, target });
    }
    let current_target = folder
        .join(current.file_name().unwrap_or_default())
        .to_string_lossy()
        .into_owned();
    if !choices.iter().any(|choice| choice.target == current_target) {
        choices.push(DatabaseChoice {
            label: current
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned(),
            target: current_target.clone(),
        });
    }
    choices.sort_by(|left, right| left.label.cmp(&right.label));
    Ok(DatabaseList {
        choices,
        current: current_target,
    })
}
