use choscordb_storage::*;
fn document() -> EditorDocument {
    EditorDocument {
        id: "tab".into(),
        title: "Query".into(),
        sql: "é".into(),
        profile_id: None,
        file_path: None,
        cursor_offset: 2,
        selection_anchor: 0,
        modified: true,
    }
}
#[test]
fn mixed_workspace_round_trips_order_active_pane_and_legacy_sql() {
    let mut store = Storage::in_memory().unwrap();
    let sql = document();
    store.save_workspace(std::slice::from_ref(&sql)).unwrap();
    let old = store.restore_workspace_tabs().unwrap();
    assert_eq!(old.tabs, vec![WorkspaceTab::Sql(sql.clone())]);
    let object = WorkspaceTab::Object(ObjectTab {
        profile_id: "profile-1".into(),
        object_type: "table".into(),
        object_id: "public.same".into(),
        label: "same".into(),
        pane: 3,
    });
    let snapshot = WorkspaceSnapshot {
        tabs: vec![object.clone(), WorkspaceTab::Sql(sql), object],
        active_index: 2,
    };
    assert!(store.save_workspace_tabs(&snapshot).is_err());
    let snapshot = WorkspaceSnapshot {
        tabs: snapshot.tabs[..2].to_vec(),
        active_index: 1,
    };
    store.save_workspace_tabs(&snapshot).unwrap();
    assert_eq!(store.restore_workspace_tabs().unwrap(), snapshot);
    let invalid_active = WorkspaceSnapshot {
        active_index: 2,
        ..snapshot.clone()
    };
    assert!(store.save_workspace_tabs(&invalid_active).is_err());
    assert_eq!(store.restore_workspace_tabs().unwrap(), snapshot);
    let second_object = WorkspaceTab::Object(ObjectTab {
        profile_id: "profile-2".into(),
        object_type: "table".into(),
        object_id: "public.same".into(),
        label: "same".into(),
        pane: 0,
    });
    let distinct = WorkspaceSnapshot {
        tabs: vec![snapshot.tabs[0].clone(), second_object],
        active_index: 0,
    };
    store.save_workspace_tabs(&distinct).unwrap();
    assert_eq!(store.restore_workspace_tabs().unwrap(), distinct);
    let mut invalid_pane = distinct.clone();
    if let WorkspaceTab::Object(object) = &mut invalid_pane.tabs[1] {
        object.pane = 5;
    }
    assert!(store.save_workspace_tabs(&invalid_pane).is_err());
    assert_eq!(store.restore_workspace_tabs().unwrap(), distinct);
}
#[test]
fn mixed_workspace_distinguishes_colons_and_sql_ids_from_object_ids() {
    let mut store = Storage::in_memory().unwrap();
    let first = WorkspaceTab::Object(ObjectTab {
        profile_id: "a:b".into(),
        object_type: "c".into(),
        object_id: "d".into(),
        label: "first".into(),
        pane: 0,
    });
    let second = WorkspaceTab::Object(ObjectTab {
        profile_id: "a".into(),
        object_type: "b:c".into(),
        object_id: "d".into(),
        label: "second".into(),
        pane: 1,
    });
    let mut sql = document();
    sql.id = "object:a:b:c:d".into();
    let snapshot = WorkspaceSnapshot {
        tabs: vec![first, WorkspaceTab::Sql(sql), second],
        active_index: 2,
    };
    store.save_workspace_tabs(&snapshot).unwrap();
    assert_eq!(store.restore_workspace_tabs().unwrap(), snapshot);
}
#[test]
fn object_recovery_accepts_namespaced_maximum_profile_id() {
    let mut store = Storage::in_memory().unwrap();
    let snapshot = WorkspaceSnapshot {
        tabs: vec![WorkspaceTab::Object(ObjectTab {
            profile_id: format!("profile:{}", "p".repeat(256)),
            object_type: "table".into(),
            object_id: "public.orders".into(),
            label: "orders".into(),
            pane: 4,
        })],
        active_index: 0,
    };
    store.save_workspace_tabs(&snapshot).unwrap();
    assert_eq!(store.restore_workspace_tabs().unwrap(), snapshot);
}
#[test]
fn reads_previous_tagged_mixed_row_ids() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let mut store = Storage::open(&path).unwrap();
    let sql = WorkspaceTab::Sql(document());
    let object = WorkspaceTab::Object(ObjectTab {
        profile_id: "old-profile".into(),
        object_type: "table".into(),
        object_id: "public.orders".into(),
        label: "orders".into(),
        pane: 2,
    });
    let snapshot = WorkspaceSnapshot {
        tabs: vec![sql, object],
        active_index: 1,
    };
    store.save_workspace_tabs(&snapshot).unwrap();
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute("UPDATE editor_documents SET id='tab' WHERE position=0", [])
        .unwrap();
    db.execute(
        "UPDATE editor_documents SET id='object:old-profile:table:public.orders' WHERE position=1",
        [],
    )
    .unwrap();
    assert_eq!(store.restore_workspace_tabs().unwrap(), snapshot);
}
#[test]
fn invalid_recovery_positions_preserve_previous_workspace() {
    let mut store = Storage::in_memory().unwrap();
    let original = document();
    store
        .save_workspace(std::slice::from_ref(&original))
        .unwrap();
    for offset in [1, 3, u64::MAX] {
        let mut invalid = original.clone();
        invalid.cursor_offset = offset;
        assert!(store.save_workspace(&[invalid]).is_err());
        assert_eq!(store.restore_workspace().unwrap(), vec![original.clone()]);
    }
}
#[test]
fn excessive_history_page_is_rejected() {
    assert!(Storage::in_memory().unwrap().history(u32::MAX, 0).is_err());
}
#[test]
fn corrupt_workspace_identity_and_position_are_rejected() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let mut store = Storage::open(&path).unwrap();
    store.save_workspace(&[document()]).unwrap();
    let db = rusqlite::Connection::open(path).unwrap();
    db.execute("UPDATE editor_documents SET position=4", [])
        .unwrap();
    assert!(store.restore_workspace().is_err());
    db.execute("UPDATE editor_documents SET position=0,id='other'", [])
        .unwrap();
    assert!(store.restore_workspace().is_err());
}
#[test]
fn limits_apply_before_replacing_tabs_and_to_corrupt_settings() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let mut store = Storage::open(&path).unwrap();
    store.save_workspace(&[document()]).unwrap();
    let mut large = document();
    large.sql = "x".repeat(MAX_SQL_BYTES + 1);
    assert!(matches!(
        store.save_workspace(&[large]),
        Err(StorageError::ResourceLimit)
    ));
    let tabs: Vec<_> = (0..129)
        .map(|i| {
            let mut d = document();
            d.id = i.to_string();
            d
        })
        .collect();
    assert!(matches!(
        store.save_workspace(&tabs),
        Err(StorageError::ResourceLimit)
    ));
    assert_eq!(store.restore_workspace().unwrap(), vec![document()]);
    let db = rusqlite::Connection::open(path).unwrap();
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('bad',?1)",
        [format!("\"{}\"", "x".repeat(65536))],
    )
    .unwrap();
    assert!(matches!(
        store.setting::<String>("bad"),
        Err(StorageError::ResourceLimit)
    ));
}
#[test]
fn history_payload_identity_and_zero_retention_are_rejected_on_read() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let mut store = Storage::open(&path).unwrap();
    let entry = HistoryEntry {
        id: "one".into(),
        profile_id: None,
        sql: "SELECT 1".into(),
        timestamp: 100,
        duration_ms: 2,
        status: HistoryStatus::Completed,
        row_count: Some(1),
    };
    store.record_history(&entry, 100).unwrap();
    let db = rusqlite::Connection::open(path).unwrap();
    db.execute("UPDATE query_history SET id='different'", [])
        .unwrap();
    assert!(store.history(10, 0).is_err());
    db.execute(
        "INSERT INTO settings(key,value) VALUES ('history_policy',?1)",
        [r#"{"enabled":true,"max_age_days":0,"max_records":1}"#],
    )
    .unwrap();
    assert!(matches!(
        store.history_policy(),
        Err(StorageError::InvalidRetention)
    ));
}

#[test]
fn oversized_corrupt_payload_is_rejected_before_json_decoding() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let mut store = Storage::open(&path).unwrap();
    store.save_workspace(&[document()]).unwrap();
    let db = rusqlite::Connection::open(path).unwrap();
    db.execute(
        "UPDATE editor_documents SET data=CAST(zeroblob(?1) AS TEXT)",
        [MAX_COLLECTION_BYTES + 1],
    )
    .unwrap();
    assert!(matches!(
        store.restore_workspace(),
        Err(StorageError::ResourceLimit)
    ));
}

fn history_entry(sql: String) -> HistoryEntry {
    HistoryEntry {
        id: "query".into(),
        profile_id: None,
        sql,
        timestamp: 100,
        duration_ms: 1,
        status: HistoryStatus::Completed,
        row_count: None,
    }
}
#[test]
fn escaped_history_size_is_rejected_by_prequeue_validation() {
    let entry = history_entry("\u{0001}".repeat(MAX_SQL_BYTES));
    assert!(matches!(entry.validate(), Err(StorageError::ResourceLimit)));
}
#[test]
fn failed_insert_after_workspace_delete_rolls_back_snapshot() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let mut store = Storage::open(&path).unwrap();
    store.save_workspace(&[document()]).unwrap();
    let db = rusqlite::Connection::open(path).unwrap();
    db.execute_batch("CREATE TRIGGER reject_snapshot BEFORE INSERT ON editor_documents BEGIN SELECT RAISE(ABORT,'injected write failure'); END;").unwrap();
    let mut replacement = document();
    replacement.id = "new".into();
    assert!(matches!(
        store.save_workspace(&[replacement]),
        Err(StorageError::Database(_))
    ));
    assert_eq!(store.restore_workspace().unwrap(), vec![document()]);
}
#[test]
fn corrupt_history_policy_does_not_block_workspace_and_can_be_repaired() {
    for corrupt in ["{", r#"{"enabled":true,"max_age_days":0,"max_records":1}"#] {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("db");
        let mut store = Storage::open(&path).unwrap();
        store.save_workspace(&[document()]).unwrap();
        drop(store);
        let db = rusqlite::Connection::open(&path).unwrap();
        db.execute(
            "INSERT INTO settings(key,value) VALUES ('history_policy',?1)",
            [corrupt],
        )
        .unwrap();
        drop(db);
        let mut reopened =
            Storage::open(&path).expect("policy corruption must not disable recovery");
        assert_eq!(reopened.restore_workspace().unwrap(), vec![document()]);
        assert!(reopened.history_policy().is_err());
        assert!(reopened.history(10, 0).is_err());
        assert!(
            reopened
                .record_history(&history_entry("SELECT 1".into()), 100)
                .is_err()
        );
        reopened
            .set_history_policy(HistoryPolicy::default())
            .unwrap();
        assert_eq!(reopened.history_policy().unwrap(), HistoryPolicy::default());
        assert!(
            reopened
                .record_history(&history_entry("SELECT 1".into()), 100)
                .unwrap()
        );
        assert_eq!(reopened.history(10, 0).unwrap().len(), 1);
    }
}

#[test]
fn history_pages_return_byte_bounded_prefix_without_skipping_sql() {
    let mut store = Storage::in_memory().unwrap();
    let sql = "\u{0001}".repeat(3 * 1024 * 1024);
    for index in 0..4 {
        let mut entry = history_entry(sql.clone());
        entry.id = index.to_string();
        entry.timestamp = 100 + index;
        store.record_history(&entry, 104).unwrap();
    }
    let first = store
        .history(100, 0)
        .expect("return the prefix that fits the byte budget");
    assert_eq!(
        first
            .iter()
            .map(|entry| entry.id.as_str())
            .collect::<Vec<_>>(),
        vec!["3", "2", "1"]
    );
    let second = store.history(100, first.len() as u32).unwrap();
    assert_eq!(second.len(), 1);
    assert_eq!(second[0].id, "0");
    assert!(first.iter().chain(&second).all(|entry| entry.sql == sql));
    assert!(store.history(100, 4).unwrap().is_empty());
}

#[test]
fn oversized_first_history_record_is_an_error_not_false_eof() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db");
    let store = Storage::open(&path).unwrap();
    let db = rusqlite::Connection::open(path).unwrap();
    db.execute("INSERT INTO query_history(id,timestamp,data) VALUES ('bad',100,CAST(zeroblob(?1) AS TEXT))", [MAX_COLLECTION_BYTES + 1]).unwrap();
    assert!(matches!(
        store.history(100, 0),
        Err(StorageError::ResourceLimit)
    ));
}
