use choscordb_bridge::*;

fn next(engine: &mut BridgeEngine, kind: &str) -> ffi::BridgeEvent {
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
    loop {
        if let Some(event) = drain_events(engine)
            .into_iter()
            .find(|event| event.kind == kind || event.kind.ends_with("failed"))
        {
            return event;
        }
        assert!(std::time::Instant::now() < deadline, "missing {kind}");
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}

fn sqlite(id: &str, path: &std::path::Path) -> ffi::ProfileDto {
    ffi::ProfileDto {
        id: id.into(),
        name: "Local".into(),
        driver: "sqlite".into(),
        path: path.to_string_lossy().into_owned(),
        ..Default::default()
    }
}

fn switch(engine: &mut BridgeEngine, connection: u64, profile: ffi::ProfileDto) -> ffi::Submit {
    let credentials = profile_draft_credentials(&profile, &Default::default(), false);
    switch_database(engine, connection, profile, credentials, 3)
}

#[test]
fn a_sqlite_file_connection_switches_in_place() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = new_engine();
    let opened = connect_sqlite(
        &mut engine,
        &dir.path().join("a.sqlite").to_string_lossy(),
        false,
    );
    assert!(opened.accepted, "{}", opened.error);
    assert_eq!(next(&mut engine, "connected").kind, "connected");
    let submitted = switch(
        &mut engine,
        opened.id,
        sqlite("", &dir.path().join("b.sqlite")),
    );
    assert!(submitted.accepted, "{}", submitted.error);
    let event = next(&mut engine, "database_switched");
    assert_eq!(
        (event.kind.as_str(), event.id, event.request_token),
        ("database_switched", opened.id, 3)
    );
}

#[test]
fn a_saved_connection_reports_a_failed_switch() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = new_engine();
    let opened = profile_connect(&mut engine, sqlite("saved", &dir.path().join("a.sqlite")));
    assert!(opened.accepted, "{}", opened.error);
    assert_eq!(next(&mut engine, "connected").kind, "connected");
    let missing = dir.path().join("missing/b.sqlite");
    let submitted = switch(&mut engine, opened.id, sqlite("saved", &missing));
    assert!(submitted.accepted, "{}", submitted.error);
    let event = next(&mut engine, "database_switched");
    assert_eq!(
        (event.kind.as_str(), event.id, event.request_token),
        ("database_switch_failed", opened.id, 3)
    );
    assert!(!event.error.is_empty());
}

#[test]
fn an_unsaved_server_connection_cannot_switch() {
    let dir = tempfile::tempdir().unwrap();
    let mut engine = new_engine();
    let opened = connect_sqlite(
        &mut engine,
        &dir.path().join("a.sqlite").to_string_lossy(),
        false,
    );
    assert!(opened.accepted, "{}", opened.error);
    let server = ffi::ProfileDto {
        driver: "postgres".into(),
        database: "shop".into(),
        ..Default::default()
    };
    let submitted = switch(&mut engine, opened.id, server);
    assert!(!submitted.accepted);
    assert_eq!(submitted.error, "This connection cannot switch databases.");
}
