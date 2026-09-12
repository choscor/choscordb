use crate::{BridgeEngine, ffi, submit};
use choscordb_core::{
    Accent, AccentPreset, AppearanceLayout, Density, ThemeMode, WindowGeometry, WorkspaceLayout,
};

fn theme(value: &str) -> Result<ThemeMode, String> {
    match value {
        "system" => Ok(ThemeMode::System),
        "light" => Ok(ThemeMode::Light),
        "dark" => Ok(ThemeMode::Dark),
        _ => Err("Unknown appearance theme".into()),
    }
}

fn density(value: &str) -> Result<Density, String> {
    match value {
        "compact" => Ok(Density::Compact),
        "comfortable" => Ok(Density::Comfortable),
        _ => Err("Unknown appearance density".into()),
    }
}

fn preset(value: &str) -> Result<AccentPreset, String> {
    match value {
        "cobalt" => Ok(AccentPreset::Cobalt),
        "azure" => Ok(AccentPreset::Azure),
        "violet" => Ok(AccentPreset::Violet),
        "teal" => Ok(AccentPreset::Teal),
        "green" => Ok(AccentPreset::Green),
        "orange" => Ok(AccentPreset::Orange),
        "rose" => Ok(AccentPreset::Rose),
        _ => Err("Unknown appearance accent preset".into()),
    }
}

fn accent(kind: &str, value: String) -> Result<Accent, String> {
    match kind {
        "preset" => Ok(Accent::Preset(preset(&value)?)),
        "custom" => Ok(Accent::Custom(value)),
        _ => Err("Unknown appearance accent kind".into()),
    }
}

pub(crate) fn dto(value: AppearanceLayout) -> ffi::AppearanceLayoutDto {
    let (accent_kind, accent): (&str, String) = match value.accent {
        Accent::Preset(value) => (
            "preset",
            match value {
                AccentPreset::Cobalt => "cobalt",
                AccentPreset::Azure => "azure",
                AccentPreset::Violet => "violet",
                AccentPreset::Teal => "teal",
                AccentPreset::Green => "green",
                AccentPreset::Orange => "orange",
                AccentPreset::Rose => "rose",
            }
            .into(),
        ),
        Accent::Custom(value) => ("custom", value),
    };
    ffi::AppearanceLayoutDto {
        version: value.version,
        theme: match value.theme {
            ThemeMode::System => "system",
            ThemeMode::Light => "light",
            ThemeMode::Dark => "dark",
        }
        .into(),
        density: match value.density {
            Density::Compact => "compact",
            Density::Comfortable => "comfortable",
        }
        .into(),
        accent_kind: accent_kind.into(),
        accent,
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
        density: density(&value.density)?,
        accent: accent(&value.accent_kind, value.accent)?,
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
