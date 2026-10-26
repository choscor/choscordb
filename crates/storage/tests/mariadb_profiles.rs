use choscordb_storage::{ConnectionProfile, Storage, StorageError};

fn profile(id: &str, driver: &str) -> ConnectionProfile {
    serde_json::from_value(
        serde_json::json!({"id":id, "name":"Server", "group_id":null,
        "credential_ref":null, "configuration":{"driver":driver,
        "host":"db.example", "port":3306, "database":"inventory", "user":"reader",
        "tls":{"mode":"Prefer", "root_certificate_path":null}}}),
    )
    .unwrap()
}

fn schema_version(path: &std::path::Path) -> i64 {
    rusqlite::Connection::open(path)
        .unwrap()
        .query_row("SELECT max(version) FROM schema_migrations", [], |row| {
            row.get(0)
        })
        .unwrap()
}

#[test]
fn mariadb_profile_round_trips_with_its_own_driver_tag() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.db");
    let saved = profile("maria", "mariadb");
    Storage::open(&path).unwrap().save_profile(&saved).unwrap();
    let loaded = Storage::open(&path)
        .unwrap()
        .profile("maria")
        .unwrap()
        .unwrap();
    assert_eq!(loaded, saved);
    assert_eq!(
        serde_json::to_value(&loaded).unwrap()["configuration"]["driver"],
        "mariadb"
    );
    assert_eq!(loaded.configuration.driver_id(), "mariadb");
    assert!(matches!(
        loaded.configuration.connection_options(None, None),
        choscordb_driver_api::ConnectionOptions::Mariadb { port: 3306, .. }
    ));
}

#[test]
fn only_the_first_mariadb_save_raises_the_schema_version() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.db");
    let mut storage = Storage::open(&path).unwrap();
    storage.save_profile(&profile("mysql", "mysql")).unwrap();
    drop(storage);
    assert_eq!(schema_version(&path), 3);

    let mut storage = Storage::open(&path).unwrap();
    storage.save_profile(&profile("maria", "mariadb")).unwrap();
    storage.save_profile(&profile("maria", "mariadb")).unwrap();
    storage.duplicate_profile("maria", "copy", "Copy").unwrap();
    drop(storage);
    assert_eq!(schema_version(&path), 4);
    assert_eq!(Storage::open(&path).unwrap().profiles().unwrap().len(), 3);
}

#[test]
fn a_rejected_mariadb_save_leaves_the_schema_version_unchanged() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.db");
    let mut invalid = profile("maria", "mariadb");
    invalid.name = String::new();
    assert!(
        Storage::open(&path)
            .unwrap()
            .save_profile(&invalid)
            .is_err()
    );
    assert_eq!(schema_version(&path), 3);
}

#[test]
fn schemas_newer_than_mariadb_profiles_are_rejected() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.db");
    drop(Storage::open(&path).unwrap());
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute("INSERT INTO schema_migrations VALUES (4)", [])
        .unwrap();
    assert!(Storage::open(&path).is_ok());
    db.execute("INSERT INTO schema_migrations VALUES (5)", [])
        .unwrap();
    assert!(matches!(
        Storage::open(&path),
        Err(StorageError::NewerSchema)
    ));
}
