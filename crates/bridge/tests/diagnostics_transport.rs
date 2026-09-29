use choscordb_bridge::{
    diagnostics_new, diagnostics_preview, diagnostics_record, ffi::DiagnosticRecordDto,
};

#[test]
fn diagnostics_bridge_starts_and_keeps_typed_record() {
    let root = tempfile::tempdir().unwrap();
    let service = diagnostics_new(root.path().to_str().unwrap(), "1.2.3", "test-build");
    assert!(choscordb_bridge::diagnostics_start(&service));
    diagnostics_record(
        &service,
        DiagnosticRecordDto {
            event: 5,
            driver: 2,
            error_class: 3,
            duration_bucket: 2,
            open_tabs: 3,
            ..DiagnosticRecordDto::default()
        },
    );
    let summary = diagnostics_preview(&service);
    assert_eq!(
        summary
            .category_counts
            .iter()
            .find(|item| item.name == "query_failed")
            .map(|item| item.count),
        Some(1)
    );
    choscordb_bridge::diagnostics_stop(&service);
}

#[test]
fn attached_engine_reports_connection_before_event_drain() {
    let root = tempfile::tempdir().unwrap();
    let service = diagnostics_new(root.path().to_str().unwrap(), "1.2.3", "test-build");
    assert!(choscordb_bridge::diagnostics_start(&service));
    let mut engine = choscordb_bridge::new_engine();
    choscordb_bridge::diagnostics_attach_engine(&mut engine, &service);
    assert!(choscordb_bridge::connect_sqlite(&mut engine, ":memory:", false).accepted);
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(3);
    loop {
        let summary = diagnostics_preview(&service);
        if summary
            .category_counts
            .iter()
            .any(|item| item.name == "connection_succeeded" && item.count == 1)
        {
            break;
        }
        assert!(
            std::time::Instant::now() < deadline,
            "diagnostics depended on bridge event draining"
        );
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
    choscordb_bridge::diagnostics_stop(&service);
}
