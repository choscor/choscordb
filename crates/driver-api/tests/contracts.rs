use choscordb_driver_api::*;
#[test]
fn page_sizes_obey_product_bounds() {
    assert!(!validate_page_size(99));
    assert!(validate_page_size(100));
    assert!(validate_page_size(1_000));
    assert!(validate_page_size(10_000));
    assert!(!validate_page_size(10_001));
}

#[test]
fn stale_handles_never_access_reused_slots() {
    let mut arena = Arena::default();
    let old = arena.insert("old");
    assert_eq!(arena.remove(old), Some("old"));
    let new = arena.insert("new");
    assert_eq!(old.slot, new.slot);
    assert_ne!(old.generation, new.generation);
    assert_eq!(arena.get(old), None);
    assert_eq!(arena.get_mut(old), None);
    assert_eq!(arena.remove(old), None);
    assert_eq!(arena.get(new), Some(&"new"));
}

#[test]
fn exact_values_and_null_survive_contract_boundary() {
    let number = "12345678901234567890.12345678901234567890";
    assert_eq!(Value::Decimal(number.into()), Value::Decimal(number.into()));
    assert_ne!(Value::Null, Value::Text(String::new()));
    let large = Value::Deferred {
        handle: Handle {
            slot: 1,
            generation: 9,
        },
        byte_length: 1_000_000_000,
        database_type: "blob".into(),
    };
    assert!(large.estimated_bytes() < 100);
}

#[test]
fn secrets_and_server_error_text_are_omitted_from_logs() {
    let secret = Secret::new("private-password");
    assert!(!format!("{secret:?}").contains("private-password"));
    let error = DriverError::new(ErrorKind::Query, "row data private-password")
        .with_code("malicious-private-password");
    assert!(!format!("{error:?} {error}").contains("private-password"));
    assert_eq!(
        error.vendor_code.as_deref(),
        Some("malicious-private-password")
    );
}

#[test]
fn row_values_are_not_revealed_in_debug_logs() {
    let row = vec![
        Value::Text("private-row-data".into()),
        Value::Binary(b"private-row-data".to_vec()),
    ];
    assert!(!format!("{row:?}").contains("private-row-data"));
    assert!(!format!("{row:?}").contains("112, 114, 105"));
}

#[test]
fn page_accounting_includes_reserved_allocations() {
    let mut text = String::with_capacity(8192);
    text.push('x');
    let mut row = Vec::with_capacity(64);
    row.push(Value::Text(text));
    let mut rows = Vec::with_capacity(32);
    rows.push(row);
    let page = ResultPage {
        index: 0,
        rows,
        has_more: false,
    };
    assert!(
        page.estimated_bytes()
            >= 8192 + 64 * std::mem::size_of::<Value>() + 32 * std::mem::size_of::<Row>()
    );
}

#[test]
fn query_summary_debug_redacts_server_warnings() {
    let summary = QuerySummary {
        affected_rows: Some(3),
        warnings: vec!["private-server-row-value".into()],
        ..Default::default()
    };
    assert!(!format!("{summary:?}").contains("private-server-row-value"));
}

#[test]
fn legacy_query_summary_has_unknown_transaction_state() {
    let summary: QuerySummary =
        serde_json::from_str(r#"{"affected_rows":null,"warnings":[]}"#).unwrap();
    assert_eq!(summary.transaction_active, None);
    let summary = QuerySummary {
        transaction_active: Some(true),
        warnings: vec!["private notice".into()],
        ..Default::default()
    };
    let roundtrip: QuerySummary =
        serde_json::from_str(&serde_json::to_string(&summary).unwrap()).unwrap();
    assert_eq!(roundtrip.transaction_active, Some(true));
    assert!(!format!("{summary:?}").contains("private notice"));
}
