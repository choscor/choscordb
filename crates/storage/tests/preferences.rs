use choscordb_storage::{
    EditorPreferences, ShortcutCommand, ShortcutOverride, Storage, StorageError,
};
#[test]
fn defaults_roundtrip_and_invalid_atomic() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("prefs.sqlite");
    let mut storage = Storage::open(&path).unwrap();
    assert_eq!(
        storage.editor_preferences().unwrap(),
        EditorPreferences::default()
    );
    let expected = EditorPreferences {
        font_family: Some("Example Mono".into()),
        font_size: 17,
        shortcuts: vec![ShortcutOverride {
            command: ShortcutCommand::Find,
            sequence: String::new(),
        }],
        ..Default::default()
    };
    storage.set_editor_preferences(&expected).unwrap();
    let mut invalid = expected.clone();
    invalid.font_size = 49;
    assert!(storage.set_editor_preferences(&invalid).is_err());
    drop(storage);
    let mut storage = Storage::open(path).unwrap();
    assert_eq!(storage.editor_preferences().unwrap(), expected);
    assert!(matches!(
        storage.set_setting("editor_preferences", &expected),
        Err(StorageError::ReservedSetting)
    ));
}
#[test]
fn corruption_is_not_silently_defaulted() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("prefs.sqlite");
    drop(Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('editor_preferences',?1)",
        ["{private invalid json}"],
    )
    .unwrap();
    let mut storage = Storage::open(path).unwrap();
    assert!(storage.editor_preferences().is_err());
    storage
        .set_editor_preferences(&EditorPreferences::default())
        .unwrap();
    assert_eq!(
        storage.editor_preferences().unwrap(),
        EditorPreferences::default()
    );
}
#[test]
fn versions_duplicate_commands_and_utf8_byte_limits_are_enforced() {
    let mut preferences = EditorPreferences {
        version: 3,
        ..Default::default()
    };
    assert!(preferences.validate().is_err());
    preferences.version = 1;
    assert!(preferences.validate().is_err());
    preferences.version = 2;
    preferences.font_size = 7;
    assert!(preferences.validate().is_err());
    preferences.font_size = 8;
    preferences.font_family = Some("é".repeat(128));
    assert!(preferences.validate().is_ok());
    preferences.font_family = Some("é".repeat(129));
    assert!(matches!(
        preferences.validate(),
        Err(StorageError::ResourceLimit)
    ));
    preferences.font_family = None;
    preferences.shortcuts = vec![
        ShortcutOverride {
            command: ShortcutCommand::Find,
            sequence: "Ctrl+F".into()
        };
        2
    ];
    assert!(preferences.validate().is_err());
    preferences.shortcuts.truncate(1);
    preferences.shortcuts[0].sequence = "x".repeat(129);
    assert!(matches!(
        preferences.validate(),
        Err(StorageError::ResourceLimit)
    ));
    let unknown = r#"{"version":1,"font_family":null,"font_size":13,"shortcuts":[{"command":"unknown","sequence":""}]}"#;
    assert!(serde_json::from_str::<EditorPreferences>(unknown).is_err());
    let catalog = ShortcutCommand::ALL
        .into_iter()
        .map(|command| ShortcutOverride {
            command,
            sequence: String::new(),
        })
        .collect();
    assert!(
        EditorPreferences {
            shortcuts: catalog,
            ..Default::default()
        }
        .validate()
        .is_ok()
    );
}

fn stored_preferences(raw: &str) -> (tempfile::TempDir, std::path::PathBuf) {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("prefs.sqlite");
    drop(Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('editor_preferences',?1)",
        [raw],
    )
    .unwrap();
    (dir, path)
}
#[test]
fn version_one_drops_retired_transaction_commands_and_saves_version_two() {
    let v1 = r#"{"version":1,"font_family":null,"font_size":14,"shortcuts":[
        {"command":"commit","sequence":"Ctrl+K"},
        {"command":"run_statement","sequence":"Ctrl+E"},
        {"command":"rollback","sequence":"Ctrl+J"}]}"#;
    let (_dir, path) = stored_preferences(v1);
    let storage = Storage::open(&path).unwrap();
    let expected = EditorPreferences {
        version: 2,
        font_family: None,
        font_size: 14,
        shortcuts: vec![ShortcutOverride {
            command: ShortcutCommand::RunStatement,
            sequence: "Ctrl+E".into(),
        }],
    };
    assert_eq!(storage.editor_preferences().unwrap(), expected);
    drop(storage);
    let db = rusqlite::Connection::open(&path).unwrap();
    let saved: String = db
        .query_row(
            "SELECT value FROM settings WHERE key='editor_preferences'",
            [],
            |row| row.get(0),
        )
        .unwrap();
    assert_eq!(
        serde_json::from_str::<EditorPreferences>(&saved).unwrap(),
        expected
    );
}
#[test]
fn version_one_with_an_unknown_command_still_fails() {
    let v1 = r#"{"version":1,"font_family":null,"font_size":13,"shortcuts":[
        {"command":"commit","sequence":""},{"command":"unknown","sequence":""}]}"#;
    let (_dir, path) = stored_preferences(v1);
    assert!(Storage::open(path).unwrap().editor_preferences().is_err());
}
#[test]
fn run_all_is_a_configurable_command() {
    assert!(ShortcutCommand::ALL.contains(&ShortcutCommand::RunAll));
    assert_eq!(
        serde_json::to_string(&ShortcutCommand::RunAll).unwrap(),
        "\"run_all\""
    );
}
#[test]
fn grid_commands_use_their_snake_case_ids_and_round_trip() {
    let grid = [
        (ShortcutCommand::GridAddRow, "grid_add_row"),
        (ShortcutCommand::GridDuplicateRow, "grid_duplicate_row"),
        (ShortcutCommand::GridDeleteRows, "grid_delete_rows"),
        (ShortcutCommand::GridRestoreRows, "grid_restore_rows"),
        (ShortcutCommand::GridSetNull, "grid_set_null"),
        (ShortcutCommand::GridEditCell, "grid_edit_cell"),
        (ShortcutCommand::GridApplyEdits, "grid_apply_edits"),
        (ShortcutCommand::GridDiscardEdits, "grid_discard_edits"),
    ];
    assert_eq!(ShortcutCommand::ALL.len(), 23);
    for (command, id) in grid {
        assert!(ShortcutCommand::ALL.contains(&command));
        assert_eq!(
            serde_json::to_string(&command).unwrap(),
            format!("\"{id}\"")
        );
    }
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("prefs.sqlite");
    let mut storage = Storage::open(&path).unwrap();
    let expected = EditorPreferences {
        shortcuts: grid
            .iter()
            .map(|(command, _)| ShortcutOverride {
                command: *command,
                sequence: "Ctrl+Alt+K".into(),
            })
            .collect(),
        ..Default::default()
    };
    storage.set_editor_preferences(&expected).unwrap();
    drop(storage);
    assert_eq!(
        Storage::open(path).unwrap().editor_preferences().unwrap(),
        expected
    );
}
