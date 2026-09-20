use choscordb_storage::*;

fn profile() -> ConnectionProfile {
    ConnectionProfile {
        id: "p1".into(),
        name: "Local".into(),
        group_id: None,
        configuration: ProfileConfiguration::Sqlite {
            path: "/tmp/data.sqlite".into(),
            read_only: true,
        },
        credential_ref: None,
    }
}

#[test]
fn migrations_and_profiles_survive_restart() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("app.sqlite");
    {
        let mut store = Storage::open(&path).unwrap();
        store.save_profile(&profile()).unwrap();
        store.duplicate_profile("p1", "p2", "Copy").unwrap();
        assert!(store.duplicate_profile("p1", "p2", "Copy").is_err());
    }
    let mut store = Storage::open(&path).unwrap();
    assert_eq!(store.profiles().unwrap().len(), 2);
    assert_eq!(store.profile("p1").unwrap(), Some(profile()));
    assert!(store.delete_profile("p2").unwrap());
    let db = rusqlite::Connection::open(path).unwrap();
    for table in [
        "schema_migrations",
        "connection_profiles",
        "profile_groups",
        "editor_documents",
        "query_history",
        "metadata_cache",
        "settings",
        "recent_items",
        "pending_credential_cleanup",
        "appearance_layout",
    ] {
        assert!(db.prepare(&format!("SELECT * FROM {table}")).is_ok());
    }
    assert_eq!(
        db.query_row("SELECT count(*) FROM schema_migrations", [], |r| r
            .get::<_, i64>(0))
            .unwrap(),
        3
    );
}

#[test]
fn history_retention_disable_and_clear() {
    let mut store = Storage::in_memory().unwrap();
    assert_eq!(store.history_policy().unwrap(), HistoryPolicy::default());
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs() as i64;
    store
        .set_history_policy(HistoryPolicy {
            enabled: true,
            max_age_days: 90,
            max_records: 2,
        })
        .unwrap();
    for (id, timestamp) in [("old", 0), ("a", now - 2), ("b", now - 1), ("c", now)] {
        store
            .record_history(
                &HistoryEntry {
                    id: id.into(),
                    profile_id: Some("p1".into()),
                    sql: "SELECT 1".into(),
                    timestamp,
                    duration_ms: 12,
                    status: HistoryStatus::Completed,
                    row_count: Some(1),
                },
                now,
            )
            .unwrap();
    }
    assert_eq!(
        store
            .history(20, 0)
            .unwrap()
            .iter()
            .map(|h| h.id.as_str())
            .collect::<Vec<_>>(),
        vec!["c", "b"]
    );
    store
        .set_history_policy(HistoryPolicy {
            enabled: false,
            ..Default::default()
        })
        .unwrap();
    let entry = store.history(1, 0).unwrap().remove(0);
    assert!(!store.record_history(&entry, now).unwrap());
    store.clear_history().unwrap();
    assert!(store.history(20, 0).unwrap().is_empty());
}

#[test]
fn recovery_is_only_editor_data_and_settings_persist() {
    let mut store = Storage::in_memory().unwrap();
    let tabs = vec![EditorDocument {
        id: "e1".into(),
        title: "Query".into(),
        sql: "DELETE FROM x".into(),
        profile_id: Some("p1".into()),
        file_path: None,
        cursor_offset: 3,
        selection_anchor: 0,
        modified: true,
    }];
    store.save_workspace(&tabs).unwrap();
    assert_eq!(store.restore_workspace().unwrap(), tabs);
    store.set_setting("font_size", &14_u32).unwrap();
    assert_eq!(store.setting::<u32>("font_size").unwrap(), Some(14));
    store.save_workspace(&[]).unwrap();
    assert!(store.restore_workspace().unwrap().is_empty());
}

#[test]
fn profile_schema_rejects_secret_fields() {
    let mut value = serde_json::to_value(profile()).unwrap();
    value["password"] = "must-not-persist".into();
    assert!(serde_json::from_value::<ConnectionProfile>(value).is_err());
    let mut value = serde_json::to_value(profile()).unwrap();
    value["configuration"]["private_key"] = "must-not-persist".into();
    assert!(serde_json::from_value::<ConnectionProfile>(value).is_err());
}

#[test]
fn workspace_replacement_rolls_back_on_duplicate_ids() {
    let mut store = Storage::in_memory().unwrap();
    let tab = EditorDocument {
        id: "one".into(),
        title: "Original".into(),
        sql: "SELECT 1".into(),
        profile_id: None,
        file_path: None,
        cursor_offset: 0,
        selection_anchor: 0,
        modified: true,
    };
    store.save_workspace(std::slice::from_ref(&tab)).unwrap();
    assert!(store.save_workspace(&[tab.clone(), tab.clone()]).is_err());
    assert_eq!(store.restore_workspace().unwrap(), vec![tab]);
}

#[test]
fn migration_failure_is_atomic_and_future_schemas_are_rejected() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("broken.sqlite");
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute("CREATE TABLE editor_documents(dummy TEXT)", [])
        .unwrap();
    assert!(Storage::open(&path).is_err());
    assert!(db.prepare("SELECT * FROM connection_profiles").is_err());
    assert!(db.prepare("SELECT * FROM schema_migrations").is_err());
    db.execute("DROP TABLE editor_documents", []).unwrap();
    let _store = Storage::open(&path).unwrap();
    db.execute("INSERT INTO schema_migrations VALUES (999)", [])
        .unwrap();
    assert!(matches!(
        Storage::open(&path),
        Err(StorageError::NewerSchema)
    ));
}

#[test]
fn postgres_profile_persists_only_credential_references() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.sqlite");
    let p = ConnectionProfile {
        id: "pg".into(),
        name: "Postgres".into(),
        group_id: None,
        configuration: ProfileConfiguration::Postgres {
            ssh: None,
            host: "localhost".into(),
            port: 5432,
            database: "app".into(),
            user: "alice".into(),
            tls: PostgresTls::default(),
        },
        credential_ref: Some("choscordb/pg/password".into()),
    };
    let mut store = Storage::open(&path).unwrap();
    store.save_profile(&p).unwrap();
    assert_eq!(store.profile("pg").unwrap(), Some(p));
    let db = rusqlite::Connection::open(path).unwrap();
    let json: String = db
        .query_row("SELECT data FROM connection_profiles", [], |r| r.get(0))
        .unwrap();
    let data: serde_json::Value = serde_json::from_str(&json).unwrap();
    assert!(data.get("password").is_none());
    assert!(data["configuration"].get("password").is_none());
    assert!(data["configuration"]["tls"].get("private_key").is_none());
    assert_eq!(data["configuration"]["tls"]["mode"], "VerifyFull");
}

#[test]
fn policy_setting_is_reserved_and_tightening_prunes_immediately() {
    let mut store = Storage::in_memory().unwrap();
    assert!(
        store
            .set_setting("history_policy", &HistoryPolicy::default())
            .is_err()
    );
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs() as i64;
    for id in ["a", "b", "c"] {
        store
            .record_history(
                &HistoryEntry {
                    id: id.into(),
                    profile_id: None,
                    sql: "SELECT 1".into(),
                    timestamp: now,
                    duration_ms: 0,
                    status: HistoryStatus::Completed,
                    row_count: None,
                },
                now,
            )
            .unwrap();
    }
    store
        .set_history_policy(HistoryPolicy {
            max_records: 1,
            ..Default::default()
        })
        .unwrap();
    assert_eq!(store.history(10, 0).unwrap().len(), 1);
}

#[test]
fn startup_prunes_old_history_without_new_queries() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("history.sqlite");
    {
        let mut store = Storage::open(&path).unwrap();
        store
            .record_history(
                &HistoryEntry {
                    id: "old".into(),
                    profile_id: None,
                    sql: "SELECT 1".into(),
                    timestamp: 0,
                    duration_ms: 0,
                    status: HistoryStatus::Completed,
                    row_count: None,
                },
                0,
            )
            .unwrap();
    }
    assert!(
        Storage::open(path)
            .unwrap()
            .history(10, 0)
            .unwrap()
            .is_empty()
    );
}

#[test]
fn recovery_and_history_debug_are_redacted() {
    let entry = HistoryEntry {
        id: "secret".into(),
        profile_id: None,
        sql: "secret".into(),
        timestamp: 0,
        duration_ms: 0,
        status: HistoryStatus::Completed,
        row_count: None,
    };
    let doc = EditorDocument {
        id: "secret".into(),
        title: "secret".into(),
        sql: "secret".into(),
        profile_id: None,
        file_path: Some("secret".into()),
        cursor_offset: 0,
        selection_anchor: 0,
        modified: false,
    };
    assert!(!format!("{entry:?} {doc:?}").contains("secret"));
}

#[test]
fn every_supported_profile_option_reaches_driver_unchanged() {
    use choscordb_driver_api::{ConnectionOptions, Secret};
    for mode in [TlsMode::VerifyFull, TlsMode::Disable] {
        let config = ProfileConfiguration::Postgres {
            ssh: None,
            host: "db.example".into(),
            port: 6543,
            database: "app data".into(),
            user: "alice".into(),
            tls: PostgresTls {
                mode: mode.clone(),
                root_certificate_path: Some("/tmp/root.pem".into()),
            },
        };
        let encoded = serde_json::to_string(&config).unwrap();
        let restored: ProfileConfiguration = serde_json::from_str(&encoded).unwrap();
        match restored.connection_options(Some(Secret::new("private-password"))) {
            ConnectionOptions::Postgres {
                ssh: None,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
            } => {
                assert_eq!(
                    (host.as_str(), port, database.as_str(), user.as_str()),
                    ("db.example", 6543, "app data", "alice")
                );
                assert_eq!(tls, mode);
                assert_eq!(
                    root_certificate.unwrap(),
                    std::path::PathBuf::from("/tmp/root.pem")
                );
                assert_eq!(password.unwrap().expose(), "private-password");
            }
            _ => panic!("wrong driver"),
        }
        assert!(!encoded.contains("private-password"));
    }
    match profile().configuration.connection_options(None) {
        ConnectionOptions::Sqlite { path, read_only } => {
            assert_eq!(path, std::path::PathBuf::from("/tmp/data.sqlite"));
            assert!(read_only);
        }
        _ => panic!("wrong driver"),
    }
    assert_eq!(PostgresTls::default().mode, TlsMode::VerifyFull);
}

#[test]
fn unsupported_tls_configuration_is_rejected_instead_of_dropped() {
    assert!(
        serde_json::from_str::<PostgresTls>(r#"{"mode":"Require","root_certificate_path":null}"#)
            .is_err()
    );
    assert!(
        serde_json::from_str::<PostgresTls>(
            r#"{"mode":"VerifyFull","root_certificate_path":null,"client_certificate_path":"cert"}"#
        )
        .is_err()
    );
}

#[test]
fn invalid_and_oversized_profiles_are_rejected() {
    let mut store = Storage::in_memory().unwrap();
    for invalid in ["", "bad\0id"] {
        let mut p = profile();
        p.id = invalid.into();
        assert!(store.save_profile(&p).is_err());
    }
    let mut p = profile();
    p.name = "n".repeat(1025);
    assert!(store.save_profile(&p).is_err());
    p = profile();
    p.configuration = ProfileConfiguration::Sqlite {
        path: String::new(),
        read_only: false,
    };
    assert!(store.save_profile(&p).is_err());
    p.configuration = ProfileConfiguration::Sqlite {
        path: "\u{1}".repeat(16 * 1024),
        read_only: false,
    };
    assert!(
        store.save_profile(&p).is_err(),
        "escaped JSON must fit serialized limit"
    );
    assert!(store.profiles().unwrap().is_empty());
}

#[test]
fn duplicate_does_not_share_credential_ownership() {
    let mut store = Storage::in_memory().unwrap();
    let mut p = profile();
    p.credential_ref = Some("opaque-reference".into());
    store.save_profile(&p).unwrap();
    let copy = store.duplicate_profile(&p.id, "copy", "Copy").unwrap();
    assert_eq!(copy.configuration, p.configuration);
    assert_eq!(copy.credential_ref, None);
    assert_eq!(store.profile(&p.id).unwrap(), Some(p));
}

#[test]
fn profile_capacity_allows_updates_and_rejects_unbounded_existing_rows() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("profiles.sqlite");
    let mut store = Storage::open(&path).unwrap();
    for n in 0..1000 {
        let mut p = profile();
        p.id = format!("p{n}");
        store.save_profile(&p).unwrap();
    }
    store.save_profile(&profile()).unwrap();
    assert_eq!(store.profiles().unwrap().len(), 1000);
    assert!(
        store
            .duplicate_profile("p1", "overflow", "Overflow")
            .is_err()
    );
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO connection_profiles VALUES ('overflow', ?1)",
        [serde_json::to_string(&profile()).unwrap()],
    )
    .unwrap();
    assert!(store.profiles().is_err());
    db.execute("DELETE FROM connection_profiles WHERE id='overflow'", [])
        .unwrap();
    db.execute(
        "UPDATE connection_profiles SET data=?1 WHERE id='p1'",
        ["x".repeat(65537)],
    )
    .unwrap();
    assert!(store.profile("p1").is_err());
    assert!(store.profiles().is_err());
}

#[test]
fn postgres_required_fields_and_certificate_path_are_validated() {
    let mut store = Storage::in_memory().unwrap();
    for (host, port, database, user, root) in [
        ("", 5432, "app", "user", None),
        ("localhost", 0, "app", "user", None),
        ("localhost", 5432, "", "user", None),
        ("localhost", 5432, "app", "", None),
        ("localhost", 5432, "app", "user", Some("cert\0path")),
    ] {
        let mut p = profile();
        p.configuration = ProfileConfiguration::Postgres {
            ssh: None,
            host: host.into(),
            port,
            database: database.into(),
            user: user.into(),
            tls: PostgresTls {
                root_certificate_path: root.map(Into::into),
                ..Default::default()
            },
        };
        assert!(matches!(
            store.save_profile(&p),
            Err(StorageError::InvalidProfile)
        ));
    }
    let mut p = profile();
    p.configuration = ProfileConfiguration::Sqlite {
        path: ":memory:".into(),
        read_only: false,
    };
    store.save_profile(&p).unwrap();
    assert_eq!(store.profile(&p.id).unwrap(), Some(p));
}

#[test]
fn migration_two_preserves_existing_profiles_and_tracks_atomic_cleanup() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("old.sqlite");
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute_batch(include_str!("../src/schema.sql")).unwrap();
    db.execute_batch("CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY); INSERT INTO schema_migrations VALUES (1);").unwrap();
    let mut p = profile();
    p.credential_ref = Some("old-reference".into());
    db.execute(
        "INSERT INTO connection_profiles VALUES (?1,?2)",
        rusqlite::params![p.id, serde_json::to_string(&p).unwrap()],
    )
    .unwrap();
    let mut store = Storage::open(&path).unwrap();
    assert_eq!(store.profile("p1").unwrap(), Some(p.clone()));
    p.credential_ref = Some("new-reference".into());
    db.execute_batch("CREATE TRIGGER deny_cleanup BEFORE INSERT ON pending_credential_cleanup BEGIN SELECT RAISE(ABORT,'injected'); END;").unwrap();
    assert!(store.save_profile(&p).is_err());
    assert_eq!(
        store
            .profile("p1")
            .unwrap()
            .unwrap()
            .credential_ref
            .as_deref(),
        Some("old-reference")
    );
    assert!(store.delete_profile("p1").is_err());
    assert!(store.profile("p1").unwrap().is_some());
    db.execute_batch("DROP TRIGGER deny_cleanup").unwrap();
    store.save_profile(&p).unwrap();
    assert_eq!(
        store.pending_credential_cleanup().unwrap(),
        vec!["old-reference"]
    );
    store.delete_profile("p1").unwrap();
    assert_eq!(
        store.pending_credential_cleanup().unwrap(),
        vec!["new-reference", "old-reference"]
    );
}
