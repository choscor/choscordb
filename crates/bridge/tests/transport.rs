use choscordb_bridge::*;
fn await_event(engine: &mut BridgeEngine, kind: &str) -> ffi::BridgeEvent {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(3);
    while std::time::Instant::now() < deadline {
        for event in drain_events(engine) {
            if event.kind == kind {
                return event;
            }
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
    panic!("missing {kind}")
}
#[test]
fn typed_transport_connects_and_streams_sqlite_without_json() {
    let mut engine = new_engine();
    assert!(initialization_error(&engine).is_empty());
    let connection = connect_sqlite(&mut engine, ":memory:", false);
    assert!(connection.accepted, "{}", connection.error);
    assert_eq!(await_event(&mut engine, "connected").id, connection.id);
    let query = execute(
        &mut engine,
        connection.id,
        "SELECT NULL, '', 42, x'00ff'",
        1000,
        0,
        true,
    );
    assert!(query.accepted, "{}", query.error);
    let schema = await_event(&mut engine, "schema");
    assert_eq!(schema.columns.len(), 4);
    assert!(fetch_page(&mut engine, query.id, 1000).accepted);
    let page = await_event(&mut engine, "page");
    assert_eq!(page.row_count, 1);
    assert_eq!(page.column_count, 4);
    assert_eq!(page.cells[0].kind, "null");
    assert_eq!(page.cells[1].kind, "text");
    assert_eq!(page.cells[2].integer, 42);
    assert_eq!(page.cells[3].bytes, vec![0, 255]);
    assert!(shutdown(&mut engine).accepted);
}
#[test]
fn invalid_inputs_return_outcomes_and_utf8_ranges_remain_bytes() {
    let mut engine = new_engine();
    assert!(!execute(&mut engine, u64::MAX, "SELECT 1", 1, 0, true).accepted);
    let result = sql_execution_range("SELECT 'é'; SELECT 2", 15, 0, 0);
    assert!(result.valid);
    assert_eq!(result.start, 13);
    assert!(sql_execution_range("DELETE FROM users", 0, 0, 0).confirmation_required);
}

#[test]
fn event_drain_is_bounded_during_burst() {
    let mut engine = new_engine();
    let c = connect_sqlite(&mut engine, ":memory:", false);
    assert!(c.accepted);
    await_event(&mut engine, "connected");
    for _ in 0..20 {
        assert!(execute(&mut engine, c.id, "SELECT 1", 1000, 0, true).accepted);
    }
    std::thread::sleep(std::time::Duration::from_millis(20));
    let events = drain_events(&mut engine);
    assert!(!events.is_empty());
    assert_eq!(events.len(), 1);
}

#[test]
fn metadata_tokens_survive_success_and_failure() {
    let mut engine = new_engine();
    let connection = connect_sqlite(&mut engine, ":memory:", false);
    assert!(connection.accepted);
    await_event(&mut engine, "connected");
    assert!(metadata_request(&mut engine, connection.id, "", 42).accepted);
    let success = await_event(&mut engine, "metadata");
    assert_eq!(success.request_token, 42);
    assert_eq!(success.objects.len(), 1);
    assert!(metadata_request(&mut engine, connection.id, "invalid object id", 43).accepted);
    let failure = await_event(&mut engine, "metadata_failed");
    assert_eq!(failure.request_token, 43);
    assert_eq!(failure.parent, "invalid object id");
    assert!(!failure.error.is_empty());
}

#[test]
fn transfer_leases_remain_accounted_until_explicit_release() {
    let mut engine = new_engine();
    let c = connect_sqlite(&mut engine, ":memory:", false);
    assert!(c.accepted);
    await_event(&mut engine, "connected");
    let q = execute(&mut engine, c.id, "SELECT 42", 1000, 0, true);
    assert!(q.accepted);
    let schema = await_event(&mut engine, "schema");
    assert!(
        schema.has_lease,
        "schema allocations need a transferred reservation"
    );
    assert!(shrink_page_lease(&mut engine, schema.lease_id, 1024).accepted);
    assert!(fetch_page_at(&mut engine, q.id, 0, 1000).accepted);
    let page = await_event(&mut engine, "stored_page");
    assert!(page.has_lease);
    let before = memory_usage(&engine).used_bytes;
    assert!(before >= page.reserved_bytes);
    let page_lease = page.lease_id;
    drop(page);
    assert_eq!(
        memory_usage(&engine).used_bytes,
        before,
        "dropping CXX DTO does not release a pinned Qt page"
    );
    assert!(shrink_page_lease(&mut engine, page_lease, 4096).accepted);
    assert!(memory_usage(&engine).used_bytes < before);
    assert!(!shrink_page_lease(&mut engine, page_lease, u64::MAX).accepted);
    assert!(release_page_lease(&mut engine, page_lease).accepted);
    assert!(
        !release_page_lease(&mut engine, page_lease).accepted,
        "stale lease must not release a reused slot"
    );
    assert!(release_page_lease(&mut engine, schema.lease_id).accepted);
}

#[test]
fn deferred_chunks_transport_bytes_offsets_errors_and_leases() {
    let mut engine = new_engine();
    let c = connect_sqlite(&mut engine, ":memory:", false);
    assert!(c.accepted);
    await_event(&mut engine, "connected");
    let q = execute(&mut engine, c.id, "SELECT zeroblob(200000)", 1000, 0, true);
    assert!(q.accepted);
    let schema = await_event(&mut engine, "schema");
    assert!(release_page_lease(&mut engine, schema.lease_id).accepted);
    assert!(fetch_page_at(&mut engine, q.id, 0, 1000).accepted);
    let page = await_event(&mut engine, "stored_page");
    let handle = page.cells[0].handle;
    let page_lease = page.lease_id;
    drop(page);
    assert!(release_page_lease(&mut engine, page_lease).accepted);
    assert!(load_value_chunk(&mut engine, q.id, handle, 123, 1024).accepted);
    let chunk = await_event(&mut engine, "value_chunk");
    assert_eq!(chunk.id, q.id);
    assert_eq!(chunk.value_handle, handle);
    assert_eq!(chunk.chunk_offset, 123);
    assert_eq!(chunk.total_bytes, 200000);
    assert_eq!(chunk.chunk_kind, "binary");
    assert_eq!(chunk.chunk_bytes, vec![0; 1024]);
    assert!(chunk.has_lease);
    assert!(memory_usage(&engine).used_bytes >= chunk.reserved_bytes);
    let lease = chunk.lease_id;
    drop(chunk);
    assert!(release_page_lease(&mut engine, lease).accepted);
    assert!(load_value_chunk(&mut engine, q.id, handle, 200001, 1024).accepted);
    let failure = await_event(&mut engine, "value_chunk_failed");
    assert_eq!(failure.id, q.id);
    assert_eq!(failure.value_handle, handle);
    assert_eq!(failure.chunk_offset, 200001);
    assert!(!failure.error.is_empty());
    assert!(!failure.has_lease);
}

#[test]
fn export_transport_publishes_original_rows_and_retires_handle() {
    let directory = tempfile::tempdir().unwrap();
    let destination = directory.path().join("result.csv");
    let mut engine = new_engine();
    let c = connect_sqlite(&mut engine, ":memory:", false);
    assert!(c.accepted);
    await_event(&mut engine, "connected");
    let q = execute(
        &mut engine,
        c.id,
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<1001) SELECT x FROM n",
        1000,
        0,
        true,
    );
    assert!(q.accepted);
    let schema = await_event(&mut engine, "schema");
    assert!(release_page_lease(&mut engine, schema.lease_id).accepted);
    assert!(fetch_page_at(&mut engine, q.id, 0, 1000).accepted);
    let page = await_event(&mut engine, "stored_page");
    assert!(release_page_lease(&mut engine, page.lease_id).accepted);
    let job = start_export(
        &mut engine,
        q.id,
        destination.to_str().unwrap(),
        "csv",
        vec![],
        false,
    );
    assert!(job.accepted, "{}", job.error);
    let done = await_event(&mut engine, "export_finished");
    assert_eq!(done.id, job.id);
    assert_eq!(done.query_id, q.id);
    assert_eq!(done.exported_rows, 1001);
    let contents = std::fs::read_to_string(&destination).unwrap();
    assert_eq!(done.exported_bytes, contents.len() as u64);
    assert_eq!(contents.lines().count(), 1002);
    assert!(contents.starts_with("\"x\"\r\n\"1\"\r\n"));
    assert!(contents.ends_with("\"1001\"\r\n"));
    assert!(!cancel_export(&mut engine, job.id).accepted);
}

#[test]
fn saved_profile_transport_persists_and_tests_without_creating_session() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("metadata.sqlite");
    let path = path.to_str().unwrap();
    {
        let mut engine = new_engine_with_storage(path);
        let dto = ffi::ProfileDto {
            id: "local".into(),
            name: "Local SQLite".into(),
            group_id: "group".into(),
            driver: "sqlite".into(),
            path: ":memory:".into(),
            ..Default::default()
        };
        assert!(profile_save(&mut engine, dto, 42).accepted);
        let saved = await_event(&mut engine, "profile_saved");
        assert_eq!(saved.request_token, 42);
        assert_eq!(saved.profiles[0].id, "local");
        assert_eq!(saved.profiles[0].group_id, "group");
    }
    let mut engine = new_engine_with_storage(path);
    assert!(profile_list(&mut engine, 43).accepted);
    let mut list = await_event(&mut engine, "profiles");
    assert_eq!(list.request_token, 43);
    assert_eq!(list.profiles.len(), 1);
    let profile = list.profiles.pop().unwrap();
    assert!(profile_test(&mut engine, profile, 44).accepted);
    assert_eq!(await_event(&mut engine, "profile_tested").request_token, 44);
    assert!(profile_duplicate(&mut engine, "local", "copy", "Copy", 45).accepted);
    assert_eq!(
        await_event(&mut engine, "profile_saved").profiles[0].id,
        "copy"
    );
    assert!(profile_delete(&mut engine, "copy", 46).accepted);
    assert_eq!(
        await_event(&mut engine, "profile_deleted").profile_id,
        "copy"
    );
}

#[test]
fn unavailable_credential_store_keeps_saved_profile_and_redacts_secret() {
    fn draft(name: &str) -> ffi::ProfileDto {
        ffi::ProfileDto {
            id: "pg".into(),
            name: name.into(),
            driver: "postgres".into(),
            host: "localhost".into(),
            port: 5432,
            database: "app".into(),
            user: "user".into(),
            tls: "verify_full".into(),
            ..Default::default()
        }
    }
    let mut engine = new_engine();
    assert!(profile_save(&mut engine, draft("Original"), 10).accepted);
    await_event(&mut engine, "profile_saved");
    assert!(
        profile_save_secret(
            &mut engine,
            draft("Changed"),
            "replace",
            "private-test-secret",
            11
        )
        .accepted
    );
    let failure = await_event(&mut engine, "profile_failed");
    assert_eq!(failure.request_token, 11);
    assert!(failure.error.contains("unavailable"));
    assert!(!failure.error.contains("private-test-secret"));
    assert!(profile_list(&mut engine, 12).accepted);
    let profiles = await_event(&mut engine, "profiles");
    assert_eq!(profiles.profiles.len(), 1);
    assert_eq!(profiles.profiles[0].name, "Original");
    assert!(profiles.profiles[0].credential_ref.is_empty());
}

#[test]
fn recovery_transport_preserves_typed_documents_and_policy_without_connecting() {
    let mut engine = new_engine();
    let document = ffi::EditorDocumentDto {
        id: "tab-1".into(),
        title: "Recovered SQL".into(),
        sql: "SELECT '🦀é';".into(),
        has_profile: true,
        profile_id: "saved-profile".into(),
        has_file: true,
        file_path: "/missing/query.sql".into(),
        cursor_offset: 14,
        selection_anchor: 8,
        modified: true,
    };
    assert!(workspace_save(&mut engine, vec![document], 700).accepted);
    assert_eq!(
        await_event(&mut engine, "workspace_saved").request_token,
        700
    );
    assert!(workspace_restore(&mut engine, 701).accepted);
    let restored = await_event(&mut engine, "workspace_restored");
    assert_eq!(restored.request_token, 701);
    assert_eq!(restored.documents.len(), 1);
    let d = &restored.documents[0];
    assert_eq!(d.sql, "SELECT '🦀é';");
    assert_eq!((d.cursor_offset, d.selection_anchor), (14, 8));
    assert!(d.has_profile && d.has_file && d.modified);
    assert_eq!(d.profile_id, "saved-profile");
    assert_eq!(d.file_path, "/missing/query.sql");
    assert!(history_policy_get(&mut engine, 702).accepted);
    let policy = await_event(&mut engine, "history_policy").history_policy;
    assert!(policy.enabled);
    assert_eq!((policy.max_age_days, policy.max_records), (90, 10_000));
    assert!(
        history_policy_set(
            &mut engine,
            ffi::HistoryPolicyDto {
                enabled: false,
                ..policy
            },
            703
        )
        .accepted
    );
    assert!(
        !await_event(&mut engine, "history_policy")
            .history_policy
            .enabled
    );
    assert!(history_list(&mut engine, 100, 0, 704).accepted);
    assert!(
        await_event(&mut engine, "history_listed")
            .history
            .is_empty()
    );
    assert!(history_clear(&mut engine, 705).accepted);
    assert_eq!(
        await_event(&mut engine, "history_cleared").request_token,
        705
    );
    assert!(drain_events(&mut engine).is_empty());
}

#[test]
fn recovery_transport_rejects_invalid_offsets_and_retention_without_losing_snapshot() {
    let mut engine = new_engine();
    let bad = ffi::EditorDocumentDto {
        id: "bad".into(),
        title: "SQL".into(),
        sql: "é".into(),
        cursor_offset: 1,
        ..Default::default()
    };
    assert!(!workspace_save(&mut engine, vec![bad], 800).accepted);
    assert!(!history_policy_set(&mut engine, ffi::HistoryPolicyDto::default(), 801).accepted);
    assert!(!history_list(&mut engine, 1001, 0, 802).accepted);
    assert!(workspace_restore(&mut engine, 803).accepted);
    let restored = await_event(&mut engine, "workspace_restored");
    assert_eq!(restored.request_token, 803);
    assert!(restored.documents.is_empty());
}

#[test]
fn keyword_completion_is_bounded_and_uses_shared_catalog() {
    assert_eq!(sql_keyword_completions("sel"), vec!["SELECT"]);
    assert!(sql_keyword_completions(&"a".repeat(257)).is_empty());
    let all = sql_keyword_completions("");
    assert!(all.contains(&"ROLLBACK".to_string()));
    assert!(all.len() <= 100);
}

#[test]
fn search_transport_preserves_byte_ranges_literal_replacement_and_errors() {
    let found = text_find("é cat βcat", "cat", 0, false, false, true);
    assert!(found.valid && found.found && !found.wrapped);
    assert_eq!((found.start, found.end), (3, 6));
    let previous = text_find("é cat βcat", "cat", 0, true, false, true);
    assert!(previous.valid && previous.found && previous.wrapped);
    assert_eq!((previous.start, previous.end), (3, 6));
    let invalid = text_find("é cat", "cat", 1, false, false, false);
    assert!(!invalid.valid && !invalid.error.is_empty());
    let replaced = text_replace_all("é cat cat", "cat", "$1", false, false);
    assert!(replaced.valid);
    assert_eq!(replaced.count, 2);
    assert_eq!(replaced.text, "é $1 $1");
}
