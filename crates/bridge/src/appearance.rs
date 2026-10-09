use crate::{BridgeEngine, ffi, submit};
use choscordb_core::{AppearanceLayout, ThemeMode, WindowGeometry, WorkspaceLayout};

fn theme(value: &str) -> Result<ThemeMode, String> {
    ThemeMode::parse_choice(value).ok_or_else(|| "Unknown appearance theme".into())
}

/// The layout the desktop applies when nothing is saved and on "Reset layout".
pub fn appearance_layout_default() -> ffi::AppearanceLayoutDto {
    dto(AppearanceLayout::default())
}

pub fn appearance_theme_valid(value: &str) -> bool {
    ThemeMode::parse_choice(value).is_some()
}

pub(crate) fn dto(value: AppearanceLayout) -> ffi::AppearanceLayoutDto {
    ffi::AppearanceLayoutDto {
        version: value.version,
        theme: match value.theme {
            ThemeMode::System => "system",
            ThemeMode::Light => "light",
            ThemeMode::Dark => "dark",
        }
        .into(),
        navigator_width: value.layout.navigator_width,
        editor_results_split: value.layout.editor_results_split,
        history_height: value.layout.history_height,
        navigator_visible: value.layout.navigator_visible,
        history_visible: value.layout.history_visible,
        x: value.geometry.x,
        y: value.geometry.y,
        width: value.geometry.width,
        height: value.geometry.height,
        maximized: value.geometry.maximized,
        has_screen_name: value.geometry.screen_name.is_some(),
        screen_name: value.geometry.screen_name.unwrap_or_default(),
    }
}

fn model(value: ffi::AppearanceLayoutDto) -> Result<AppearanceLayout, String> {
    Ok(AppearanceLayout {
        version: value.version,
        theme: theme(&value.theme)?,
        layout: WorkspaceLayout {
            navigator_width: value.navigator_width,
            editor_results_split: value.editor_results_split,
            history_height: value.history_height,
            navigator_visible: value.navigator_visible,
            history_visible: value.history_visible,
        },
        geometry: WindowGeometry {
            x: value.x,
            y: value.y,
            width: value.width,
            height: value.height,
            maximized: value.maximized,
            screen_name: value.has_screen_name.then_some(value.screen_name),
        },
    })
}

pub fn appearance_layout_get(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |core| {
        core.appearance_layout_get(token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}

pub fn appearance_layout_set(
    engine: &mut BridgeEngine,
    value: ffi::AppearanceLayoutDto,
    token: u64,
) -> ffi::Submit {
    submit(engine, |core| {
        core.appearance_layout_set(model(value)?, token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}

pub fn appearance_layout_reset(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |core| {
        core.appearance_layout_reset(token)
            .map(|()| token)
            .map_err(|error| error.to_string())
    })
}
