//! Versioned, bounded appearance and workspace-layout persistence.
//!
//! Screen intersection is deliberately a native-client decision because this
//! crate has no display-system dependency. The persisted coordinates and
//! dimensions are nevertheless bounded before they can reach that client.
use crate::{Result, Storage, StorageError};
use rusqlite::{TransactionBehavior, params};
use serde::{Deserialize, Serialize};

pub const APPEARANCE_LAYOUT_VERSION: u32 = 1;
pub const MAX_APPEARANCE_LAYOUT_BYTES: usize = 4096;
pub const MAX_SCREEN_NAME_BYTES: usize = 256;
pub const MIN_WINDOW_WIDTH: u32 = 960;
pub const MIN_WINDOW_HEIGHT: u32 = 640;
pub const MAX_WINDOW_DIMENSION: u32 = 16_384;
const MAX_COORDINATE_MAGNITUDE: i32 = 1_000_000;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ThemeMode {
    #[default]
    System,
    Light,
    Dark,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Density {
    #[default]
    Compact,
    Comfortable,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AccentPreset {
    #[default]
    Cobalt,
    Azure,
    Violet,
    Teal,
    Green,
    Orange,
    Rose,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(
    tag = "kind",
    content = "value",
    rename_all = "snake_case",
    deny_unknown_fields
)]
pub enum Accent {
    Preset(AccentPreset),
    Custom(String),
}

impl Default for Accent {
    fn default() -> Self {
        Self::Preset(AccentPreset::Cobalt)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WorkspaceLayout {
    pub navigator_width: u32,
    /// Permille of the editor/results area's height assigned to the editor.
    pub editor_results_split: u16,
    pub history_height: u32,
    pub navigator_visible: bool,
    pub history_visible: bool,
}

impl Default for WorkspaceLayout {
    fn default() -> Self {
        Self {
            navigator_width: 280,
            editor_results_split: 600,
            history_height: 220,
            navigator_visible: true,
            history_visible: false,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WindowGeometry {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
    pub maximized: bool,
    /// Last-known native screen identifier, used only as a restoration hint.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub screen_name: Option<String>,
}

impl Default for WindowGeometry {
    fn default() -> Self {
        Self {
            x: 0,
            y: 0,
            width: 1280,
            height: 900,
            maximized: false,
            screen_name: None,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AppearanceLayout {
    pub version: u32,
    pub theme: ThemeMode,
    pub density: Density,
    pub accent: Accent,
    pub layout: WorkspaceLayout,
    pub geometry: WindowGeometry,
}

impl AppearanceLayout {
    pub fn validate(&self) -> Result<()> {
        if self.version != APPEARANCE_LAYOUT_VERSION
            || !(96..=2048).contains(&self.layout.navigator_width)
            || !(100..=900).contains(&self.layout.editor_results_split)
            || !(80..=4096).contains(&self.layout.history_height)
            || matches!(&self.accent, Accent::Custom(value) if !valid_hex_color(value))
            || !self.geometry.is_valid()
        {
            return Err(StorageError::InvalidAppearance);
        }
        Ok(())
    }

    fn validate_without_geometry(&self) -> Result<()> {
        let mut value = self.clone();
        value.geometry = WindowGeometry::default();
        value.validate()
    }
}

impl WindowGeometry {
    fn is_valid(&self) -> bool {
        (MIN_WINDOW_WIDTH..=MAX_WINDOW_DIMENSION).contains(&self.width)
            && (MIN_WINDOW_HEIGHT..=MAX_WINDOW_DIMENSION).contains(&self.height)
            && self.x.unsigned_abs() <= MAX_COORDINATE_MAGNITUDE as u32
            && self.y.unsigned_abs() <= MAX_COORDINATE_MAGNITUDE as u32
            && !self.screen_name.as_ref().is_some_and(|name| {
                name.is_empty() || name.len() > MAX_SCREEN_NAME_BYTES || name.contains('\0')
            })
    }
}

impl Default for AppearanceLayout {
    fn default() -> Self {
        Self {
            version: APPEARANCE_LAYOUT_VERSION,
            theme: ThemeMode::default(),
            density: Density::default(),
            accent: Accent::default(),
            layout: WorkspaceLayout::default(),
            geometry: WindowGeometry::default(),
        }
    }
}

impl Storage {
    /// Missing, corrupt, and unsupported records remain observably distinct.
    pub fn appearance_layout(&self) -> Result<Option<AppearanceLayout>> {
        let mut statement = self
            .db
            .prepare("SELECT value FROM appearance_layout WHERE singleton=1")?;
        let mut rows = statement.query([])?;
        let Some(row) = rows.next()? else {
            return Ok(None);
        };
        let encoded = row
            .get_ref(0)?
            .as_str()
            .map_err(|_| StorageError::CorruptAppearance)?;
        if encoded.len() > MAX_APPEARANCE_LAYOUT_BYTES {
            return Err(StorageError::CorruptAppearance);
        }
        let raw: serde_json::Value =
            serde_json::from_str(encoded).map_err(|_| StorageError::CorruptAppearance)?;
        let version = raw
            .get("version")
            .and_then(serde_json::Value::as_u64)
            .ok_or(StorageError::CorruptAppearance)?;
        if version != u64::from(APPEARANCE_LAYOUT_VERSION) {
            return Err(StorageError::UnsupportedAppearanceVersion(version));
        }
        let mut value: AppearanceLayout =
            serde_json::from_value(raw).map_err(|_| StorageError::CorruptAppearance)?;
        value
            .validate_without_geometry()
            .map_err(|_| StorageError::CorruptAppearance)?;
        if !value.geometry.is_valid() {
            value.geometry = WindowGeometry::default();
        }
        Ok(Some(value))
    }

    /// Validates and replaces the singleton record in one immediate transaction.
    pub fn set_appearance_layout(&mut self, value: &AppearanceLayout) -> Result<()> {
        value.validate()?;
        let encoded = serde_json::to_string(value)?;
        if encoded.len() > MAX_APPEARANCE_LAYOUT_BYTES {
            return Err(StorageError::InvalidAppearance);
        }
        let tx = self
            .db
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        tx.execute(
            "INSERT INTO appearance_layout(singleton,value) VALUES (1,?1) \
             ON CONFLICT(singleton) DO UPDATE SET value=excluded.value",
            params![encoded],
        )?;
        tx.commit()?;
        Ok(())
    }

    /// Explicit reset is the only automatic-default path allowed to delete a
    /// corrupt or unsupported saved record.
    pub fn reset_appearance_layout(&mut self) -> Result<()> {
        let tx = self
            .db
            .transaction_with_behavior(TransactionBehavior::Immediate)?;
        tx.execute("DELETE FROM appearance_layout WHERE singleton=1", [])?;
        tx.commit()?;
        Ok(())
    }
}

fn valid_hex_color(value: &str) -> bool {
    value.len() == 7
        && value.starts_with('#')
        && value.as_bytes()[1..]
            .iter()
            .all(|byte| byte.is_ascii_hexdigit())
}
