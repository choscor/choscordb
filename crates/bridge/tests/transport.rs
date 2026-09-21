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
fn result_view_commands_transport_typed_filters_sort_pages_and_clear() {
    let mut engine = new_engine();
    let connection = connect_sqlite(&mut engine, ":memory:", false);
    assert!(connection.accepted, "{}", connection.error);
    await_event(&mut engine, "connected");
    let query = execute(
        &mut engine,
        connection.id,
        "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<206) SELECT CASE WHEN x=206 THEN NULL ELSE printf('row%03d',206-x) END, x FROM c",
        100,
        0,
        true,
    );
    assert!(query.accepted, "{}", query.error);
    await_event(&mut engine, "schema");
    let applied = apply_result_view(
        &mut engine,
        query.id,
        vec![ffi::ResultFilterDto {
            column: 1,
            operation: "greater_than".into(),
            value_kind: "integer".into(),
            value: "100".into(),
        }],
        0,
        "ascending",
        100,
    );
    assert!(applied.accepted, "{}", applied.error);
    assert_eq!(
        await_event(&mut engine, "result_view_applied").result_view_rows,
        106
    );
    assert!(fetch_page_at(&mut engine, query.id, 1, 100).accepted);
    let page = await_event(&mut engine, "stored_page");
    assert_eq!(page.first_row, 100);
    assert_eq!(page.cells[0].text, "row101");
    assert_eq!(page.cells[10].kind, "null");
    assert!(clear_result_view(&mut engine, query.id).accepted);
    assert_eq!(
        await_event(&mut engine, "result_view_applied").result_view_rows,
        206
    );

    let invalid = apply_result_view(
        &mut engine,
        query.id,
        vec![ffi::ResultFilterDto {
            column: 0,
            operation: "regex".into(),
            value_kind: "text".into(),
            value: ".*".into(),
        }],
        0,
        "",
        100,
    );
    assert!(!invalid.accepted);
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
fn mixed_workspace_transport_preserves_order_active_and_object_pane() {
    let mut engine = new_engine();
    let sql = ffi::WorkspaceTabDto {
        document: ffi::EditorDocumentDto {
            id: "draft".into(),
            title: "Draft".into(),
            sql: "SELECT 7".into(),
            cursor_offset: 8,
            selection_anchor: 8,
            modified: true,
            ..Default::default()
        },
        ..Default::default()
    };
    let object = ffi::WorkspaceTabDto {
        is_object: true,
        profile_id: "profile".into(),
        object_type: "table".into(),
        object_id: "public.orders".into(),
        label: "orders".into(),
        pane: 3,
        ..Default::default()
    };
    assert!(workspace_tabs_save(&mut engine, vec![object, sql], 0, 901).accepted);
    assert_eq!(
        await_event(&mut engine, "workspace_saved").request_token,
        901
    );
    assert!(workspace_tabs_restore(&mut engine, 902).accepted);
    let restored = await_event(&mut engine, "workspace_tabs_restored");
    assert_eq!(restored.active_tab, 0);
    assert_eq!(restored.workspace_tabs.len(), 2);
    assert!(restored.workspace_tabs[0].is_object);
    assert_eq!(restored.workspace_tabs[0].object_id, "public.orders");
    assert_eq!(restored.workspace_tabs[0].pane, 3);
    assert_eq!(restored.workspace_tabs[1].document.sql, "SELECT 7");
}

#[test]
fn appearance_transport_preserves_typed_values_missing_and_reset() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("appearance.sqlite");
    let mut engine = new_engine_with_storage(path.to_str().unwrap());

    assert!(appearance_layout_get(&mut engine, 900).accepted);
    let missing = await_event(&mut engine, "appearance_layout");
    assert_eq!(missing.request_token, 900);
    assert!(!missing.has_appearance);
    assert_eq!(missing.appearance_layout.version, 1);
    assert_eq!(missing.appearance_layout.theme, "system");
    assert_eq!(missing.appearance_layout.density, "compact");
    assert_eq!(missing.appearance_layout.width, 1280);
    assert_eq!(missing.appearance_layout.height, 900);

    let expected = ffi::AppearanceLayoutDto {
        version: 1,
        theme: "dark".into(),
        density: "comfortable".into(),
        accent_kind: "custom".into(),
        accent: "#1267A8".into(),
        navigator_width: 312,
        editor_results_split: 575,
        history_height: 244,
        navigator_visible: true,
        history_visible: true,
        x: -720,
        y: 48,
        width: 1440,
        height: 960,
        maximized: false,
        has_screen_name: true,
        screen_name: "Left display".into(),
    };
    assert!(appearance_layout_set(&mut engine, expected, 901).accepted);
    let saved = await_event(&mut engine, "appearance_layout");
    assert_eq!(saved.request_token, 901);
    assert!(saved.has_appearance);
    assert_eq!(saved.appearance_layout.theme, "dark");
    assert_eq!(saved.appearance_layout.density, "comfortable");
    assert_eq!(saved.appearance_layout.accent_kind, "custom");
    assert_eq!(saved.appearance_layout.accent, "#1267A8");
    assert_eq!(saved.appearance_layout.navigator_width, 312);
    assert_eq!(saved.appearance_layout.editor_results_split, 575);
    assert_eq!(saved.appearance_layout.x, -720);
    assert_eq!(saved.appearance_layout.screen_name, "Left display");

    let invalid = ffi::AppearanceLayoutDto {
        version: 1,
        theme: "sepia".into(),
        ..Default::default()
    };
    assert!(!appearance_layout_set(&mut engine, invalid, 902).accepted);
    assert!(appearance_layout_reset(&mut engine, 903).accepted);
    let reset = await_event(&mut engine, "appearance_layout");
    assert_eq!(reset.request_token, 903);
    assert!(!reset.has_appearance);
}

#[test]
fn appearance_corruption_is_user_visible_and_reset_releases_the_queue() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("appearance-corrupt.sqlite");
    let mut initialized = new_engine_with_storage(path.to_str().unwrap());
    assert!(appearance_layout_get(&mut initialized, 909).accepted);
    await_event(&mut initialized, "appearance_layout");
    drop(initialized);
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute(
        "INSERT INTO appearance_layout(singleton,value) VALUES (1,?1)",
        ["{private malformed appearance"],
    )
    .unwrap();
    drop(db);

    let mut engine = new_engine_with_storage(path.to_str().unwrap());
    assert!(appearance_layout_get(&mut engine, 910).accepted);
    let failed = await_event(&mut engine, "recovery_failed");
    assert_eq!(failed.request_token, 910);
    assert_eq!(failed.error_kind, "InvalidInput");
    assert!(failed.error.contains("corrupt"));
    assert!(!failed.error.contains("private"));
    assert!(appearance_layout_reset(&mut engine, 911).accepted);
    let reset = await_event(&mut engine, "appearance_layout");
    assert_eq!(reset.request_token, 911);
    assert!(!reset.has_appearance);
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

#[test]
fn ddl_requests_correlate_success_and_failure() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("inspect.sqlite");
    let db = rusqlite::Connection::open(&path).unwrap();
    db.execute_batch("CREATE TABLE t(x TEXT DEFAULT 'one')")
        .unwrap();
    drop(db);
    let mut engine = new_engine();
    let connection = connect_sqlite(&mut engine, path.to_str().unwrap(), false);
    await_event(&mut engine, "connected");
    assert!(object_ddl_request(&mut engine, connection.id, r#"["main","t"]"#, 81).accepted);
    let ddl = await_event(&mut engine, "ddl");
    assert_eq!(ddl.request_token, 81);
    assert_eq!(ddl.object, r#"["main","t"]"#);
    assert!(ddl.ddl.contains("DEFAULT 'one'"));
    assert!(object_ddl_request(&mut engine, connection.id, r#"["main","missing"]"#, 82).accepted);
    let failure = await_event(&mut engine, "ddl_failed");
    assert_eq!(failure.request_token, 82);
    assert_eq!(failure.object, r#"["main","missing"]"#);
    assert!(!failure.error.is_empty());
}

#[test]
fn postgres_ssh_profile_survives_save_reload_and_duplicate() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("ssh.sqlite");
    let path = path.to_str().unwrap();
    {
        let mut engine = new_engine_with_storage(path);
        let profile = ffi::ProfileDto {
            id: "ssh".into(),
            name: "SSH database".into(),
            driver: "postgres".into(),
            host: "db.internal".into(),
            port: 5433,
            database: "app".into(),
            user: "dbuser".into(),
            tls: "verify_full".into(),
            ssh_enabled: true,
            ssh_host: "bastion.example".into(),
            ssh_port: 2222,
            ssh_user: "operator".into(),
            ssh_identity_file: "/keys/database".into(),
            ..Default::default()
        };
        assert!(profile_save(&mut engine, profile, 80).accepted);
        let saved = await_event(&mut engine, "profile_saved");
        assert!(saved.profiles[0].ssh_enabled);
    }
    let mut engine = new_engine_with_storage(path);
    assert!(profile_duplicate(&mut engine, "ssh", "copy", "Copy", 81).accepted);
    let saved = await_event(&mut engine, "profile_saved");
    let profile = &saved.profiles[0];
    assert!(profile.ssh_enabled);
    assert_eq!(profile.ssh_host, "bastion.example");
    assert_eq!(profile.ssh_port, 2222);
    assert_eq!(profile.ssh_user, "operator");
    assert_eq!(profile.ssh_identity_file, "/keys/database");
    assert_eq!(profile.host, "db.internal");
    assert_eq!(profile.port, 5433);
}

#[test]
fn mysql_profile_roundtrips_and_dispatches_to_registered_driver() {
    let mut engine = new_engine();
    let dto = ffi::ProfileDto {
        id: "mysql".into(),
        name: "Inventory".into(),
        driver: "mysql".into(),
        host: "127.0.0.1".into(),
        port: 1,
        database: "inventory".into(),
        user: "reader".into(),
        tls: "verify_full".into(),
        root_certificate: "/ca.pem".into(),
        ..Default::default()
    };
    let saved = profile_save(&mut engine, dto, 901);
    assert!(saved.accepted, "{}", saved.error);
    let event = await_event(&mut engine, "profile_saved");
    let dto = event.profiles.into_iter().next().unwrap();
    assert_eq!(
        (
            &*dto.driver,
            &*dto.database,
            &*dto.user,
            &*dto.tls,
            &*dto.root_certificate
        ),
        ("mysql", "inventory", "reader", "verify_full", "/ca.pem")
    );
    assert_eq!(dto.port, 1);
    let tested = profile_test(&mut engine, dto, 902);
    assert!(tested.accepted, "{}", tested.error);
    assert_eq!(
        await_event(&mut engine, "profile_failed").request_token,
        902
    );
}

#[test]
fn mysql_bridge_rejects_invalid_ssh_and_invalid_tls() {
    for (tls, ssh_enabled) in [("prefer", false), ("verify_full", true)] {
        let mut engine = new_engine();
        let result = profile_save(
            &mut engine,
            ffi::ProfileDto {
                id: "mysql".into(),
                name: "MySQL".into(),
                driver: "mysql".into(),
                host: "db.example".into(),
                port: 3306,
                database: "app".into(),
                user: "reader".into(),
                tls: tls.into(),
                ssh_enabled,
                ..Default::default()
            },
            1,
        );
        assert!(!result.accepted);
    }
}

#[test]
fn mysql_sql_ranges_keep_escaped_quote_and_hash_comment_in_statement() {
    let sql = "SELECT 'it\\'s; intact'; # comment;\nDELETE FROM items";
    let selected = sql_execution_range_mysql(sql, 0, 0, 0);
    assert!(selected.valid);
    assert_eq!(
        &sql[selected.start as usize..selected.end as usize],
        "SELECT 'it\\'s; intact';"
    );
    // The shared grammar conservatively confirms dialect-specific escaped strings.
    assert!(selected.confirmation_required);
    assert!(!sql_execution_range_mysql("SELECT 1", 0, 0, 0).confirmation_required);
    let selected = sql_execution_range_mysql(sql, sql.len() as u64, 0, 0);
    assert!(selected.valid);
    assert!(selected.confirmation_required);
}

#[test]
fn export_dialect_rejects_unknown_names() {
    let mut engine = new_engine();
    let result = start_export_dialect(
        &mut engine,
        0,
        "/unused",
        "sql",
        vec!["items".into()],
        "unknown",
    );
    assert!(!result.accepted);
    assert_eq!(result.error, "Unknown SQL dialect");
}

#[test]
fn mysql_sql_mode_crosses_editor_bridge() {
    let sql = "SELECT 'a\\'; SELECT 2;";
    let default = sql_execution_range_mysql(sql, 16, 0, 0);
    let mode = sql_execution_range_mysql_mode(sql, 16, 0, 0, "NO_BACKSLASH_ESCAPES");
    assert!(mode.valid);
    assert_eq!(&sql[mode.start as usize..mode.end as usize], "SELECT 2;");
    assert!(!default.valid || default.start != mode.start || default.end != mode.end);
}

#[test]
fn mysql_bridge_preserves_ssh_profile() {
    let directory = tempfile::tempdir().unwrap();
    let mut engine =
        new_engine_with_storage(directory.path().join("mysql-ssh.db").to_str().unwrap());
    assert!(
        profile_save(
            &mut engine,
            ffi::ProfileDto {
                id: "mysql-ssh".into(),
                name: "MySQL over SSH".into(),
                driver: "mysql".into(),
                host: "database.internal".into(),
                port: 3306,
                database: "inventory".into(),
                user: "reader".into(),
                tls: "verify_full".into(),
                ssh_enabled: true,
                ssh_host: "bastion.example".into(),
                ssh_port: 2222,
                ssh_user: "tunnel".into(),
                ssh_identity_file: "/keys/mysql".into(),
                ..Default::default()
            },
            701
        )
        .accepted
    );
    let saved = await_event(&mut engine, "profile_saved");
    assert!(saved.profiles[0].ssh_enabled);
    assert_eq!(saved.profiles[0].ssh_host, "bastion.example");
    assert_eq!(saved.profiles[0].host, "database.internal");
    assert_eq!(saved.profiles[0].tls, "verify_full");
}
