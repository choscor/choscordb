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
        version: 2,
        ..Default::default()
    };
    assert!(preferences.validate().is_err());
    preferences.version = 1;
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
