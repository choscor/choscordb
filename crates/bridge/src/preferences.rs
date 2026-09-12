use crate::{BridgeEngine, ffi, submit};
use choscordb_core::{EditorPreferences, ShortcutCommand, ShortcutOverride};
fn command_id(command: ShortcutCommand) -> &'static str {
    match command {
        ShortcutCommand::NewQuery => "new_query",
        ShortcutCommand::ChoscorDBFile => "open_sql_file",
        ShortcutCommand::SaveSqlFile => "save_sql_file",
        ShortcutCommand::Undo => "undo",
        ShortcutCommand::Redo => "redo",
        ShortcutCommand::Cut => "cut",
        ShortcutCommand::Copy => "copy",
        ShortcutCommand::Paste => "paste",
        ShortcutCommand::Find => "find",
        ShortcutCommand::Replace => "replace",
        ShortcutCommand::FindNext => "find_next",
        ShortcutCommand::FindPrevious => "find_previous",
        ShortcutCommand::RunStatement => "run_statement",
        ShortcutCommand::CancelQuery => "cancel_query",
        ShortcutCommand::Commit => "commit",
        ShortcutCommand::Rollback => "rollback",
    }
}
pub(crate) fn dto(value: EditorPreferences) -> ffi::EditorPreferencesDto {
    ffi::EditorPreferencesDto {
        version: value.version,
        font_family: value.font_family.unwrap_or_default(),
        font_size: value.font_size,
        shortcuts: value
            .shortcuts
            .into_iter()
            .map(|s| ffi::ShortcutOverrideDto {
                command: command_id(s.command).into(),
                sequence: s.sequence,
            })
            .collect(),
    }
}
pub fn editor_preferences_get(engine: &mut BridgeEngine, token: u64) -> ffi::Submit {
    submit(engine, |e| {
        e.editor_preferences_get(token)
            .map(|()| token)
            .map_err(|e| e.to_string())
    })
}
pub fn editor_preferences_set(
    engine: &mut BridgeEngine,
    value: ffi::EditorPreferencesDto,
    token: u64,
) -> ffi::Submit {
    submit(engine, |e| {
        if value.shortcuts.len() > ShortcutCommand::ALL.len() {
            return Err("Too many shortcuts".into());
        }
        let mut shortcuts = Vec::with_capacity(value.shortcuts.len());
        for s in value.shortcuts {
            let command = ShortcutCommand::ALL
                .into_iter()
                .find(|c| command_id(*c) == s.command)
                .ok_or_else(|| "Unknown shortcut command".to_string())?;
            shortcuts.push(ShortcutOverride {
                command,
                sequence: s.sequence,
            });
        }
        e.editor_preferences_set(
            EditorPreferences {
                version: value.version,
                font_family: (!value.font_family.is_empty()).then_some(value.font_family),
                font_size: value.font_size,
                shortcuts,
            },
            token,
        )
        .map(|()| token)
        .map_err(|e| e.to_string())
    })
}
pub fn editor_preference_limits() -> ffi::EditorPreferenceLimitsDto {
    ffi::EditorPreferenceLimitsDto {
        version: choscordb_core::EDITOR_PREFERENCES_VERSION,
        default_font_size: choscordb_core::DEFAULT_EDITOR_FONT_SIZE,
        min_font_size: choscordb_core::MIN_EDITOR_FONT_SIZE,
        max_font_size: choscordb_core::MAX_EDITOR_FONT_SIZE,
    }
}
