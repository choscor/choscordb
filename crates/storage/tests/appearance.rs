use choscordb_storage::{
    APPEARANCE_LAYOUT_VERSION, Accent, AccentPreset, AppearanceLayout, Density, Storage,
    StorageError, ThemeMode, WindowGeometry, WorkspaceLayout,
};

fn customized() -> AppearanceLayout {
    AppearanceLayout {
        version: APPEARANCE_LAYOUT_VERSION,
        theme: ThemeMode::Dark,
        density: Density::Comfortable,
        accent: Accent::Custom("#1267A8".into()),
        layout: WorkspaceLayout {
            navigator_width: 312,
            editor_results_split: 575,
            history_height: 244,
            navigator_visible: true,
            history_visible: true,
        },
        geometry: WindowGeometry {
            x: -720,
            y: 48,
            width: 1440,
            height: 960,
            maximized: false,
            screen_name: Some("Left display".into()),
        },
    }
}

#[test]
fn missing_save_restart_and_reset_are_distinct() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("appearance.sqlite");
    let mut storage = Storage::open(&path).unwrap();
    assert_eq!(storage.appearance_layout().unwrap(), None);

    let expected = customized();
    storage.set_appearance_layout(&expected).unwrap();
    drop(storage);

    let mut storage = Storage::open(&path).unwrap();
    assert_eq!(storage.appearance_layout().unwrap(), Some(expected));
    storage.reset_appearance_layout().unwrap();
    assert_eq!(storage.appearance_layout().unwrap(), None);
}

#[test]
fn invalid_save_is_atomic_and_cannot_replace_the_last_valid_record() {
    let mut storage = Storage::in_memory().unwrap();
    let expected = customized();
    storage.set_appearance_layout(&expected).unwrap();

    let mut invalid = expected.clone();
    invalid.layout.editor_results_split = 1001;
    assert!(matches!(
        storage.set_appearance_layout(&invalid),
        Err(StorageError::InvalidAppearance)
    ));
    assert_eq!(storage.appearance_layout().unwrap(), Some(expected));
}

#[test]
fn corrupt_and_unsupported_records_are_reported_and_never_overwritten_on_load() {
    for (stored, expected_error) in [
        ("{not json", "corrupt"),
        (
            r##"{"version":2,"theme":"system","density":"compact","accent":{"kind":"preset","value":"cobalt"},"layout":{"navigator_width":280,"editor_results_split":600,"history_height":220,"navigator_visible":true,"history_visible":false},"geometry":{"x":0,"y":0,"width":1280,"height":900,"maximized":false}}"##,
            "unsupported",
        ),
    ] {
        let directory = tempfile::tempdir().unwrap();
        let path = directory.path().join("appearance.sqlite");
        drop(Storage::open(&path).unwrap());
        let db = rusqlite::Connection::open(&path).unwrap();
        db.execute(
            "INSERT INTO appearance_layout(singleton,value) VALUES (1,?1)",
            [stored],
        )
        .unwrap();
        drop(db);

        let storage = Storage::open(&path).unwrap();
        let error = storage.appearance_layout().unwrap_err();
        assert!(match expected_error {
            "corrupt" => matches!(error, StorageError::CorruptAppearance),
            "unsupported" => matches!(error, StorageError::UnsupportedAppearanceVersion(2)),
            _ => false,
        });
        drop(storage);
        let db = rusqlite::Connection::open(path).unwrap();
        let unchanged: String = db
            .query_row(
                "SELECT value FROM appearance_layout WHERE singleton=1",
                [],
                |row| row.get(0),
            )
            .unwrap();
        assert_eq!(unchanged, stored);
    }
}

#[test]
fn non_text_payload_is_classified_as_corruption() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("wrong-type.sqlite");
    drop(Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO appearance_layout(singleton,value) VALUES (1,?1)",
        [rusqlite::types::Value::Blob(vec![0, 1, 2])],
    )
    .unwrap();
    drop(db);

    let storage = Storage::open(path).unwrap();
    assert!(matches!(
        storage.appearance_layout(),
        Err(StorageError::CorruptAppearance)
    ));
}

#[test]
fn implausible_geometry_falls_back_without_losing_appearance_or_rewriting_storage() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("geometry.sqlite");
    drop(Storage::open(&path).unwrap());
    let mut raw = serde_json::to_value(customized()).unwrap();
    raw["geometry"]["width"] = 1.into();
    let stored = serde_json::to_string(&raw).unwrap();
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO appearance_layout(singleton,value) VALUES (1,?1)",
        [&stored],
    )
    .unwrap();
    drop(db);

    let storage = Storage::open(&path).unwrap();
    let loaded = storage.appearance_layout().unwrap().unwrap();
    assert_eq!(loaded.theme, ThemeMode::Dark);
    assert_eq!(loaded.density, Density::Comfortable);
    assert_eq!(loaded.geometry, WindowGeometry::default());
    drop(storage);

    let db = rusqlite::Connection::open(path).unwrap();
    let unchanged: String = db
        .query_row(
            "SELECT value FROM appearance_layout WHERE singleton=1",
            [],
            |row| row.get(0),
        )
        .unwrap();
    assert_eq!(unchanged, stored);
}

#[test]
fn every_field_is_bounded_and_enums_reject_unknown_values() {
    let valid = customized();
    assert!(valid.validate().is_ok());

    let mut cases = Vec::new();
    let mut value = valid.clone();
    value.accent = Accent::Custom("red".into());
    cases.push(value);
    let mut value = valid.clone();
    value.geometry.width = 959;
    cases.push(value);
    let mut value = valid.clone();
    value.geometry.height = 16_385;
    cases.push(value);
    let mut value = valid.clone();
    value.geometry.x = 1_000_001;
    cases.push(value);
    let mut value = valid.clone();
    value.layout.navigator_width = 95;
    cases.push(value);
    let mut value = valid;
    value.layout.history_height = 4097;
    cases.push(value);
    for value in cases {
        assert!(matches!(
            value.validate(),
            Err(StorageError::InvalidAppearance)
        ));
    }

    let unknown = r##"{"version":1,"theme":"sepia","density":"compact","accent":{"kind":"preset","value":"cobalt"},"layout":{"navigator_width":280,"editor_results_split":600,"history_height":220,"navigator_visible":true,"history_visible":false},"geometry":{"x":0,"y":0,"width":1280,"height":900,"maximized":false}}"##;
    assert!(serde_json::from_str::<AppearanceLayout>(unknown).is_err());

    assert!(
        AppearanceLayout {
            accent: Accent::Preset(AccentPreset::Cobalt),
            ..AppearanceLayout::default()
        }
        .validate()
        .is_ok()
    );
}

#[test]
fn generic_settings_api_cannot_bypass_the_appearance_contract() {
    let mut storage = Storage::in_memory().unwrap();
    assert!(matches!(
        storage.set_setting("appearance_layout", &customized()),
        Err(StorageError::ReservedSetting)
    ));
}

#[test]
fn migration_from_the_previous_schema_is_additive() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("v2.sqlite");
    let mut storage = Storage::open(&path).unwrap();
    storage
        .set_setting("migration_sentinel", &"preserved")
        .unwrap();
    drop(storage);
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute("DELETE FROM schema_migrations WHERE version=3", [])
        .unwrap();
    db.execute("DROP TABLE appearance_layout", []).unwrap();
    drop(db);

    let storage = Storage::open(&path).unwrap();
    assert_eq!(storage.appearance_layout().unwrap(), None);
    assert_eq!(
        storage.setting::<String>("migration_sentinel").unwrap(),
        Some("preserved".into())
    );
    let version: i64 = rusqlite::Connection::open(path)
        .unwrap()
        .query_row("SELECT max(version) FROM schema_migrations", [], |row| {
            row.get(0)
        })
        .unwrap();
    assert_eq!(version, 3);
}
