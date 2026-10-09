use choscordb_driver_api::{Column, Value};
use choscordb_result_store::{
    JsonViewError, JsonViewReadiness, JsonViewRow, MAX_JSON_VIEW_BYTES, json_cell_readiness,
    json_page_readiness, json_row_readiness, json_view_error_message, render_json_cell,
    render_json_page, render_json_row,
};
use std::collections::BTreeMap;

fn column(name: &str, database_type: &str) -> Column {
    Column {
        name: name.into(),
        database_type: database_type.into(),
        precision: None,
        scale: None,
        timezone: None,
        nullable: None,
    }
}

#[test]
fn row_json_preserves_typed_values_and_staged_state() {
    let columns = [
        column("null", "text"),
        column("empty", "text"),
        column("large", "bigint"),
        column("fraction", "double precision"),
        column("flag", "boolean"),
        column("json text", "jsonb"),
        column("plain text", "text"),
        column("bytes", "blob"),
    ];
    let row = JsonViewRow {
        cells: vec![
            Value::Null,
            Value::Text(String::new()),
            Value::Integer(i64::MAX),
            Value::Real(1.25),
            Value::Bool(true),
            Value::Text("{\"a\":1}".into()),
            Value::Text("{\"a\":1}".into()),
            Value::Binary(vec![0, 255]),
        ],
        inserted: false,
        touched: vec![false; 8],
    };
    let json = render_json_row(&columns, &row, &BTreeMap::new(), MAX_JSON_VIEW_BYTES).unwrap();
    assert_eq!(
        json,
        "{\n  \"null\": null,\n  \"empty\": \"\",\n  \"large\": 9223372036854775807,\n  \"fraction\": 1.25,\n  \"flag\": true,\n  \"json text\": {\n    \"a\": 1\n  },\n  \"plain text\": \"{\\\"a\\\":1}\",\n  \"bytes\": {\"$binary\": \"AP8=\"}\n}"
    );
}

#[test]
fn row_json_disambiguates_names_and_marks_untouched_insert_values() {
    let columns = [
        column("name", "text"),
        column("name", "text"),
        column("name (2)", "text"),
        column("", "text"),
        column("column 4", "text"),
        column("", "text"),
    ];
    let row = JsonViewRow {
        cells: ["one", "two", "three", "four", "five", "six"]
            .into_iter()
            .map(|value| Value::Text(value.into()))
            .collect(),
        inserted: false,
        touched: vec![false; 6],
    };
    let json = render_json_row(&columns, &row, &BTreeMap::new(), MAX_JSON_VIEW_BYTES).unwrap();
    assert_eq!(
        json,
        "{\n  \"name\": \"one\",\n  \"name (3)\": \"two\",\n  \"name (2)\": \"three\",\n  \"column 4 (2)\": \"four\",\n  \"column 4\": \"five\",\n  \"column 6\": \"six\"\n}"
    );

    let inserted = JsonViewRow {
        cells: vec![Value::Null, Value::Null],
        inserted: true,
        touched: vec![false, true],
    };
    assert_eq!(
        render_json_row(
            &columns[..2],
            &inserted,
            &BTreeMap::new(),
            MAX_JSON_VIEW_BYTES
        )
        .unwrap(),
        "{\n  \"name\": {\"$omitted\": true},\n  \"name (2)\": null\n}"
    );
}

#[test]
fn cell_json_preserves_duplicate_keys_and_classifies_invalid_text() {
    let json_column = column("document", "JSONB");
    let plain_column = column("text", "varchar(80)");
    let source = Value::Text("{\"n\":9223372036854775807,\"n\":2}".into());
    assert_eq!(
        json_cell_readiness(&json_column, &source),
        JsonViewReadiness::Ready
    );
    assert_eq!(
        render_json_cell(&json_column, &source, None, MAX_JSON_VIEW_BYTES).unwrap(),
        "{\n  \"n\": 9223372036854775807,\n  \"n\": 2\n}"
    );
    assert_eq!(
        json_cell_readiness(&plain_column, &Value::Text("true".into())),
        JsonViewReadiness::Ready
    );
    assert_eq!(
        render_json_cell(
            &plain_column,
            &Value::Text("true".into()),
            None,
            MAX_JSON_VIEW_BYTES
        )
        .unwrap(),
        "true"
    );
    assert_eq!(
        json_cell_readiness(&json_column, &Value::Text("{broken".into())),
        JsonViewReadiness::Invalid
    );
    assert_eq!(
        json_cell_readiness(&plain_column, &Value::Text("{broken".into())),
        JsonViewReadiness::Unavailable
    );
    assert_eq!(
        render_json_cell(
            &json_column,
            &Value::Text("{broken".into()),
            None,
            MAX_JSON_VIEW_BYTES
        ),
        Err(JsonViewError::InvalidJson)
    );
}

#[test]
fn deferred_values_require_exact_type_and_byte_length() {
    let columns = [column("text", "text"), column("bytes", "blob")];
    let row = JsonViewRow {
        cells: vec![
            Value::Deferred {
                handle: choscordb_driver_api::Handle {
                    slot: 1,
                    generation: 1,
                },
                byte_length: 4,
                database_type: "text".into(),
            },
            Value::Deferred {
                handle: choscordb_driver_api::Handle {
                    slot: 2,
                    generation: 1,
                },
                byte_length: 0,
                database_type: "blob".into(),
            },
        ],
        inserted: false,
        touched: vec![false; 2],
    };
    assert_eq!(
        json_row_readiness(&columns, &row, MAX_JSON_VIEW_BYTES),
        JsonViewReadiness::NeedsDeferred
    );
    assert_eq!(
        render_json_row(&columns, &row, &BTreeMap::new(), MAX_JSON_VIEW_BYTES),
        Err(JsonViewError::Deferred)
    );
    let mut resolved = BTreeMap::from([(0, Value::Text("abc".into())), (1, Value::Binary(vec![]))]);
    assert_eq!(
        render_json_row(&columns, &row, &resolved, MAX_JSON_VIEW_BYTES),
        Err(JsonViewError::IncompleteResolution)
    );
    resolved.insert(0, Value::Text("full".into()));
    assert_eq!(
        render_json_row(&columns, &row, &resolved, MAX_JSON_VIEW_BYTES).unwrap(),
        "{\n  \"text\": \"full\",\n  \"bytes\": {\"$binary\": \"\"}\n}"
    );
    resolved.insert(0, Value::Binary(b"full".to_vec()));
    assert_eq!(
        render_json_row(&columns, &row, &resolved, MAX_JSON_VIEW_BYTES),
        Err(JsonViewError::IncompleteResolution)
    );
}

#[test]
fn page_json_keeps_grid_order_and_rejects_aggregate_output_over_budget() {
    let columns = [column("document", "jsonb"), column("plain", "text")];
    let rows = vec![
        JsonViewRow {
            cells: vec![
                Value::Text("{\"row\":1}".into()),
                Value::Text("first".into()),
            ],
            inserted: false,
            touched: vec![false; 2],
        },
        JsonViewRow {
            cells: vec![
                Value::Text("[false,null]".into()),
                Value::Text("second".into()),
            ],
            inserted: false,
            touched: vec![false; 2],
        },
    ];
    assert_eq!(
        json_page_readiness(&columns, &rows, MAX_JSON_VIEW_BYTES),
        JsonViewReadiness::Ready
    );
    assert_eq!(
        render_json_page(&columns, &rows, &BTreeMap::new(), MAX_JSON_VIEW_BYTES).unwrap(),
        "[\n  {\n    \"document\": {\n      \"row\": 1\n    },\n    \"plain\": \"first\"\n  },\n  {\n    \"document\": [\n      false,\n      null\n    ],\n    \"plain\": \"second\"\n  }\n]"
    );
    assert!(render_json_row(&columns, &rows[0], &BTreeMap::new(), 128).is_ok());
    assert_eq!(
        render_json_page(&columns, &rows, &BTreeMap::new(), 128),
        Err(JsonViewError::DisplayLimit)
    );
}

#[test]
fn fallback_values_keep_type_and_deferred_fallback_requires_matching_resolution() {
    let columns = [column("unfamiliar", "range_type")];
    let fallback = JsonViewRow {
        cells: vec![Value::FallbackText {
            text: "[1,9)".into(),
            database_type: "range_type".into(),
        }],
        inserted: false,
        touched: vec![false],
    };
    assert_eq!(
        render_json_row(&columns, &fallback, &BTreeMap::new(), MAX_JSON_VIEW_BYTES).unwrap(),
        "{\n  \"unfamiliar\": {\"fallback_text\": \"[1,9)\", \"database_type\": \"range_type\"}\n}"
    );
    let deferred = JsonViewRow {
        cells: vec![Value::DeferredFallback {
            handle: choscordb_driver_api::Handle {
                slot: 91,
                generation: 1,
            },
            byte_length: 5,
            database_type: "range_type".into(),
        }],
        inserted: false,
        touched: vec![false],
    };
    assert_eq!(
        json_row_readiness(&columns, &deferred, MAX_JSON_VIEW_BYTES),
        JsonViewReadiness::NeedsDeferred
    );
    assert_eq!(
        render_json_row(
            &columns,
            &deferred,
            &BTreeMap::from([(0, Value::Text("[1,9)".into()))]),
            MAX_JSON_VIEW_BYTES
        ),
        Err(JsonViewError::IncompleteResolution)
    );
    assert_eq!(
        render_json_row(
            &columns,
            &deferred,
            &BTreeMap::from([(
                0,
                Value::FallbackText {
                    text: "[1,9)".into(),
                    database_type: "wrong".into(),
                }
            )]),
            MAX_JSON_VIEW_BYTES
        ),
        Err(JsonViewError::IncompleteResolution)
    );
    assert!(
        render_json_row(
            &columns,
            &deferred,
            &BTreeMap::from([(
                0,
                Value::FallbackText {
                    text: "[1,9)".into(),
                    database_type: "range_type".into(),
                }
            )]),
            MAX_JSON_VIEW_BYTES
        )
        .is_ok()
    );
}

#[test]
fn nonfinite_and_unavailable_values_invalidate_row_and_page() {
    let columns = [column("fraction", "real")];
    let invalid = JsonViewRow {
        cells: vec![Value::Real(f64::INFINITY)],
        inserted: false,
        touched: vec![false],
    };
    assert_eq!(
        json_row_readiness(&columns, &invalid, MAX_JSON_VIEW_BYTES),
        JsonViewReadiness::Invalid
    );
    assert_eq!(
        render_json_row(&columns, &invalid, &BTreeMap::new(), MAX_JSON_VIEW_BYTES),
        Err(JsonViewError::NonFinite)
    );
    let unavailable = JsonViewRow {
        cells: vec![Value::Unavailable {
            database_type: "real".into(),
            reason: "decode failed".into(),
        }],
        inserted: false,
        touched: vec![false],
    };
    assert_eq!(
        json_page_readiness(
            &columns,
            std::slice::from_ref(&unavailable),
            MAX_JSON_VIEW_BYTES
        ),
        JsonViewReadiness::Invalid
    );
    assert_eq!(
        render_json_page(
            &columns,
            &[unavailable],
            &BTreeMap::new(),
            MAX_JSON_VIEW_BYTES
        ),
        Err(JsonViewError::Unavailable)
    );
}

#[test]
fn source_and_rendered_json_obey_sixteen_mib_ceiling() {
    let document = column("document", "jsonb");
    let display_units = MAX_JSON_VIEW_BYTES / 2;
    let source = Value::Text(format!("\"{}\"", "x".repeat(display_units - 2)));
    assert_eq!(
        json_cell_readiness(&document, &source),
        JsonViewReadiness::Ready
    );
    assert_eq!(
        render_json_cell(&document, &source, None, MAX_JSON_VIEW_BYTES)
            .unwrap()
            .len(),
        display_units
    );
    let over = Value::Text(format!("\"{}\"", "x".repeat(display_units - 1)));
    assert_eq!(
        json_cell_readiness(&document, &over),
        JsonViewReadiness::Invalid
    );
    assert_eq!(
        render_json_cell(&document, &over, None, MAX_JSON_VIEW_BYTES),
        Err(JsonViewError::InvalidJson)
    );
    let row = JsonViewRow {
        cells: vec![Value::Text("x".repeat(display_units - 10))],
        inserted: false,
        touched: vec![false],
    };
    assert_eq!(
        render_json_row(
            &[column("plain", "text")],
            &row,
            &BTreeMap::new(),
            MAX_JSON_VIEW_BYTES
        ),
        Err(JsonViewError::DisplayLimit)
    );
}

#[test]
fn row_json_escapes_control_text_and_keeps_unicode() {
    let row = JsonViewRow {
        cells: vec![Value::Text("\"\\\u{001f}🙂\n".into())],
        inserted: false,
        touched: vec![false],
    };
    assert_eq!(
        render_json_row(
            &[column("payload", "text")],
            &row,
            &BTreeMap::new(),
            MAX_JSON_VIEW_BYTES
        )
        .unwrap(),
        "{\n  \"payload\": \"\\\"\\\\\\u001f🙂\\u000a\"\n}"
    );
}

#[test]
fn json_document_depth_matches_qt_parser_limit() {
    let column = column("document", "jsonb");
    let accepted = Value::Text(format!("{}0{}", "[".repeat(1023), "]".repeat(1023)));
    assert_eq!(
        json_cell_readiness(&column, &accepted),
        JsonViewReadiness::Ready
    );
    let too_deep = Value::Text(format!("{}0{}", "[".repeat(1024), "]".repeat(1024)));
    assert_eq!(
        json_cell_readiness(&column, &too_deep),
        JsonViewReadiness::Invalid
    );
}

#[test]
fn page_json_preserves_exact_decimal_values() {
    let columns = [column("total", "numeric")];
    let rows: Vec<_> = ["377.00", "12345678901234567890.123456789", "-0.01"]
        .into_iter()
        .map(|text| JsonViewRow {
            cells: vec![Value::Decimal(text.into())],
            inserted: false,
            touched: vec![false],
        })
        .collect();
    assert_eq!(
        render_json_page(&columns, &rows, &BTreeMap::new(), MAX_JSON_VIEW_BYTES).unwrap(),
        "[\n  {\n    \"total\": \"377.00\"\n  },\n  {\n    \"total\": \"12345678901234567890.123456789\"\n  },\n  {\n    \"total\": \"-0.01\"\n  }\n]"
    );
    assert_eq!(
        json_page_readiness(&columns, &rows, MAX_JSON_VIEW_BYTES),
        JsonViewReadiness::Ready
    );
}

#[test]
fn json_view_errors_explain_the_display_budget() {
    assert_eq!(
        json_view_error_message(JsonViewError::DisplayLimit, 101, false),
        "The JSON output exceeds 100 bytes of display storage (16 MiB maximum)."
    );
    assert_eq!(
        json_view_error_message(JsonViewError::DisplayLimit, usize::MAX, true),
        format!(
            "The JSON output exceeds {MAX_JSON_VIEW_BYTES} bytes of display storage (16 MiB maximum)."
        )
    );
    assert_eq!(
        json_view_error_message(JsonViewError::InvalidInput, 0, true),
        "This cell contains invalid JSON view input."
    );
    assert_eq!(
        json_view_error_message(JsonViewError::InvalidInput, 0, false),
        "This row contains invalid JSON view input."
    );
    assert_eq!(
        json_view_error_message(JsonViewError::Deferred, 0, false),
        "Load every deferred value before viewing this row as JSON."
    );
}
