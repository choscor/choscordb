use choscordb_driver_api::{Handle, Value};
use choscordb_result_store::{CopyCell, CopyError, CopyRequest, render_copy_tsv};

fn cell(original: Value) -> Option<CopyCell> {
    Some(CopyCell {
        original,
        resolved: None,
        inserted_omitted: false,
    })
}

fn request(rows: Vec<Vec<Option<CopyCell>>>) -> CopyRequest {
    CopyRequest {
        rows,
        resolutions: vec![],
        byte_budget: 1024,
    }
}

#[test]
fn copies_sparse_coordinates_with_tsv_escaping_and_typed_values() {
    let rows = vec![
        vec![
            cell(Value::Decimal("12345678901234567890.001".into())),
            cell(Value::Text("a\tb\n\"c\"".into())),
            None,
        ],
        vec![None, None, None],
        vec![
            None,
            cell(Value::Null),
            cell(Value::Binary(vec![0, 255, 127])),
        ],
    ];
    assert_eq!(
        render_copy_tsv(request(rows)).unwrap(),
        "12345678901234567890.001\t\"a\tb\n\"\"c\"\"\"\t\n\t\t\n\tNULL\t0x00ff7f"
    );
}

#[test]
fn limits_exact_utf16_output_including_escape_and_binary_expansion() {
    let mut selection = request(vec![vec![cell(Value::Text("é\"".into()))]]);
    selection.byte_budget = 10; // five UTF-16 units: quoted text plus escaped quote
    assert_eq!(render_copy_tsv(selection.clone()).unwrap(), "\"é\"\"\"");
    selection.byte_budget = 8;
    assert_eq!(render_copy_tsv(selection), Err(CopyError::Limit));

    let mut binary = request(vec![vec![cell(Value::Binary(vec![1, 2]))]]);
    binary.byte_budget = 12;
    assert_eq!(render_copy_tsv(binary.clone()).unwrap(), "0x0102");
    binary.byte_budget = 10;
    assert_eq!(render_copy_tsv(binary), Err(CopyError::Limit));
}

#[test]
fn deferred_resolution_must_match_type_and_complete_utf8_length() {
    let original = Value::DeferredFallback {
        handle: Handle {
            slot: 1,
            generation: 2,
        },
        byte_length: 5,
        database_type: "range_type".into(),
    };
    let mut selection = request(vec![vec![cell(original.clone())]]);
    assert_eq!(render_copy_tsv(selection.clone()), Err(CopyError::Deferred));
    selection.rows[0][0].as_mut().unwrap().resolved = Some(Value::Text("[1,9)".into()));
    assert_eq!(
        render_copy_tsv(selection.clone()),
        Err(CopyError::WrongResolutionType)
    );
    selection.rows[0][0].as_mut().unwrap().resolved = Some(Value::FallbackText {
        text: "[1,9)".into(),
        database_type: "wrong".into(),
    });
    assert_eq!(
        render_copy_tsv(selection.clone()),
        Err(CopyError::WrongResolutionType)
    );
    selection.rows[0][0].as_mut().unwrap().resolved = Some(Value::FallbackText {
        text: "éé".into(),
        database_type: "range_type".into(),
    });
    assert_eq!(
        render_copy_tsv(selection.clone()),
        Err(CopyError::IncompleteResolution)
    );
    selection.rows[0][0].as_mut().unwrap().resolved = Some(Value::FallbackText {
        text: "[1,9)".into(),
        database_type: "range_type".into(),
    });
    assert_eq!(render_copy_tsv(selection).unwrap(), "[1,9)");
}

#[test]
fn rejects_invalid_global_resolution_even_if_cell_is_not_selected() {
    let mut selection = request(vec![vec![cell(Value::Text("safe".into()))]]);
    selection
        .resolutions
        .push((None, Value::Text("wrong".into())));
    assert_eq!(
        render_copy_tsv(selection),
        Err(CopyError::InvalidResolution)
    );
}

#[test]
fn unavailable_cells_fail_and_omitted_insert_fields_copy_empty() {
    let mut selection = request(vec![vec![cell(Value::Unavailable {
        database_type: "odd_type".into(),
        reason: "text output failed".into(),
    })]]);
    assert_eq!(
        render_copy_tsv(selection.clone()),
        Err(CopyError::Unavailable {
            database_type: "odd_type".into(),
            reason: "text output failed".into(),
        })
    );
    selection.rows[0][0] = cell(Value::Null);
    selection.rows[0][0].as_mut().unwrap().inserted_omitted = true;
    assert_eq!(render_copy_tsv(selection).unwrap(), "");
}

#[test]
fn real_values_use_grid_precision_and_normalize_negative_zero() {
    let selection = request(vec![vec![cell(Value::Real(-0.0)), cell(Value::Real(0.1))]]);
    assert_eq!(
        render_copy_tsv(selection).unwrap(),
        "0\t0.10000000000000001"
    );
}
