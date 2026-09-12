use choscordb_storage::{QueryPreferences, Storage, StorageError};
#[test]
fn defaults_restart_and_atomic_validation() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("prefs.sqlite");
    let mut storage = Storage::open(&path).unwrap();
    assert_eq!(
        storage.query_preferences().unwrap(),
        QueryPreferences::default()
    );
    let mut prefs = QueryPreferences {
        timeout_seconds: 15,
        ..Default::default()
    };
    storage.set_query_preferences(&prefs).unwrap();
    prefs.timeout_seconds = 86401;
    assert!(storage.set_query_preferences(&prefs).is_err());
    drop(storage);
    let mut storage = Storage::open(path).unwrap();
    assert_eq!(storage.query_preferences().unwrap().timeout_seconds, 15);
    assert!(matches!(
        storage.set_setting("query_preferences", &prefs),
        Err(StorageError::ReservedSetting)
    ));
}
#[test]
fn corrupt_settings_do_not_default() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("prefs.sqlite");
    drop(Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('query_preferences',?1)",
        [r#"{"version":1,"page_size":99,"timeout_seconds":0}"#],
    )
    .unwrap();
    let mut storage = Storage::open(path).unwrap();
    assert!(storage.query_preferences().is_err());
    storage
        .set_query_preferences(&QueryPreferences::default())
        .unwrap();
    assert!(storage.query_preferences().is_ok());
}
#[test]
fn every_supported_page_size_and_timeout_boundaries_validate() {
    use choscordb_driver_api::{DEFAULT_PAGE_SIZE, MAX_PAGE_SIZE, MIN_PAGE_SIZE, PageSize};
    assert_eq!(
        QueryPreferences::default().page_size.get(),
        DEFAULT_PAGE_SIZE
    );
    for size in MIN_PAGE_SIZE..=MAX_PAGE_SIZE {
        let value = QueryPreferences {
            page_size: PageSize::new(size).unwrap(),
            timeout_seconds: 86400,
            ..Default::default()
        };
        assert!(value.validate().is_ok());
    }
    for size in [MIN_PAGE_SIZE - 1, MAX_PAGE_SIZE + 1, u32::MAX] {
        assert!(PageSize::new(size).is_err());
    }
    for data in [
        r#"{"version":2,"page_size":1000,"timeout_seconds":0}"#,
        r#"{"version":1,"page_size":1000,"timeout_seconds":86401}"#,
    ] {
        assert!(
            serde_json::from_str::<QueryPreferences>(data)
                .unwrap()
                .validate()
                .is_err()
        );
    }
}
