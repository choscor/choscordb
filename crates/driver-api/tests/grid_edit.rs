use choscordb_driver_api::{
    GridCellKind, GridEditColumn, GridEditDriver, GridEditError, GridEditRequest, GridEditRow,
    Handle, MAX_PAGE_SIZE, Value, grid_editability, grid_row_insert_error, plan_grid_edits,
};

fn column(name: &str, database_type: &str, key: bool) -> GridEditColumn {
    GridEditColumn {
        name: name.into(),
        result_name: name.into(),
        database_type: database_type.into(),
        key,
        generated: false,
        enum_source_column: String::new(),
        enum_choices: vec![],
    }
}

fn request(driver: GridEditDriver) -> GridEditRequest {
    GridEditRequest {
        driver,
        qualified_name: match driver {
            GridEditDriver::Mysql => "`db`.`items`",
            _ => "\"main\".\"items\"",
        }
        .into(),
        parameter_style: if driver == GridEditDriver::Postgres {
            "$"
        } else {
            "?"
        }
        .into(),
        reason: String::new(),
        object_read_only: true,
        columns: vec![
            column("id", "integer", true),
            column("title", "text", false),
        ],
        rows: vec![GridEditRow {
            current: vec![Value::Integer(7), Value::Text("new".into())],
            original: vec![Value::Integer(7), Value::Text("old".into())],
            touched: vec![false, true],
            inserted: false,
            deleted: false,
        }],
    }
}

#[test]
fn planned_values_keep_database_types_through_review_and_apply() {
    let mut input = request(GridEditDriver::Sqlite);
    let kinds = [
        "date",
        "time with time zone",
        "timestamp",
        "uuid",
        "jsonb",
        "numeric(8,2)",
        "text",
    ];
    input.columns = kinds
        .iter()
        .enumerate()
        .map(|(index, kind)| column(&format!("value_{index}"), kind, false))
        .collect();
    input.rows = vec![GridEditRow {
        current: [
            "2026-09-29",
            "12:34:56",
            "2026-09-29 12:34:56",
            "5bc74327-3f72-4341-a77e-7eb95fe61128",
            "{\"a\":1}",
            "12.50",
            "plain",
        ]
        .into_iter()
        .map(|value| Value::Text(value.into()))
        .collect(),
        original: vec![],
        touched: vec![true; kinds.len()],
        inserted: true,
        deleted: false,
    }];
    assert_eq!(
        plan_grid_edits(&input).unwrap().statements[0]
            .statement
            .params,
        [
            Value::Date("2026-09-29".into()),
            Value::Time("12:34:56".into()),
            Value::Timestamp("2026-09-29 12:34:56".into()),
            Value::Uuid("5bc74327-3f72-4341-a77e-7eb95fe61128".into()),
            Value::Json("{\"a\":1}".into()),
            Value::Decimal("12.50".into()),
            Value::Text("plain".into()),
        ]
    );
}

#[test]
fn keyed_plain_columns_allow_updates_in_all_dialects() {
    for driver in [
        GridEditDriver::Sqlite,
        GridEditDriver::Postgres,
        GridEditDriver::Mysql,
    ] {
        let eligibility = grid_editability(&request(driver));
        assert_eq!(eligibility.editable, vec![false, true]);
        assert_eq!(eligibility.insert_editable, vec![true, true]);
        assert!(eligibility.can_insert);
        assert!(eligibility.can_delete);
    }
}

#[test]
fn editability_blocks_unsafe_comparisons_but_keeps_safe_inserts() {
    let mut input = request(GridEditDriver::Postgres);
    input.columns[1].database_type = "custom_enum".into();
    let unsupported = grid_editability(&input);
    assert_eq!(unsupported.editable, [false, false]);
    assert!(!unsupported.can_delete);
    assert!(unsupported.can_insert);

    input.columns[1].enum_source_column = "title".into();
    input.columns[1].enum_choices = vec!["new".into(), "old".into()];
    assert!(grid_editability(&input).can_delete);
    input.rows[0].current[1] = Value::Binary(vec![1]);
    let binary = grid_editability(&input);
    assert!(!binary.can_delete);
    assert!(binary.can_insert);
    assert!(binary.reason.unwrap().contains("deferred original values"));

    input.rows[0].current[1] = Value::FallbackText {
        text: "raw".into(),
        database_type: "custom_enum".into(),
    };
    let opaque = grid_editability(&input);
    assert_eq!(opaque.insert_editable, [true, false]);
    assert_eq!(opaque.editable, [false, false]);

    input.rows[0].current[1] = Value::Text("new".into());
    input.columns[1].result_name = "alias".into();
    assert_eq!(
        grid_editability(&input).reason.as_deref(),
        Some("Result columns do not match table metadata.")
    );
}

#[test]
fn computed_query_columns_do_not_disable_safe_source_columns() {
    let mut input = request(GridEditDriver::Postgres);
    input.object_read_only = false;
    input.columns[1].name.clear();
    input.columns[1].result_name = "computed".into();
    input.columns[1].generated = true;
    input.rows[0].touched = vec![false, false];
    input.rows[0].deleted = true;
    let eligible = grid_editability(&input);
    assert_eq!(eligible.insert_editable, [true, false]);
    assert!(eligible.can_delete);
    let deletion = plan_grid_edits(&input).unwrap();
    assert_eq!(
        deletion.statements[0].statement.sql,
        "DELETE FROM \"main\".\"items\" WHERE \"id\" IS NOT DISTINCT FROM $1"
    );
}

#[test]
fn updates_bind_new_values_then_null_safe_original_predicates() {
    for (driver, expected_sql) in [
        (
            GridEditDriver::Sqlite,
            "UPDATE \"main\".\"items\" SET \"title\" = ? WHERE \"id\" IS ? AND \"title\" IS ?",
        ),
        (
            GridEditDriver::Postgres,
            "UPDATE \"main\".\"items\" SET \"title\" = $1 WHERE \"id\" IS NOT DISTINCT FROM $2 AND \"title\" IS NOT DISTINCT FROM $3",
        ),
        (
            GridEditDriver::Mysql,
            "UPDATE `db`.`items` SET `title` = ? WHERE `id` <=> ? AND `title` <=> ?",
        ),
    ] {
        let plan = plan_grid_edits(&request(driver)).unwrap();
        assert_eq!(plan.statements.len(), 1);
        let edit = &plan.statements[0];
        assert_eq!(edit.statement.sql, expected_sql);
        assert_eq!(edit.statement.expected_rows, Some(1));
        assert_eq!(
            edit.statement.params,
            vec![
                Value::Text("new".into()),
                Value::Integer(7),
                Value::Text("old".into()),
            ]
        );
        assert_eq!(edit.parameter_types, ["text", "integer", "text"]);
    }
}

#[test]
fn null_original_is_bound_and_an_opaque_non_key_predicate_is_skipped() {
    let mut input = request(GridEditDriver::Postgres);
    input.rows[0].original[1] = Value::Null;
    let null_plan = plan_grid_edits(&input).unwrap();
    assert_eq!(null_plan.statements[0].statement.params[2], Value::Null);
    input.rows[0].original[1] = Value::FallbackText {
        text: "unparsed".into(),
        database_type: "unknown".into(),
    };
    let fallback_plan = plan_grid_edits(&input).unwrap();
    assert_eq!(
        fallback_plan.statements[0].statement.sql,
        "UPDATE \"main\".\"items\" SET \"title\" = $1 WHERE \"id\" IS NOT DISTINCT FROM $2"
    );
    assert_eq!(fallback_plan.statements[0].statement.params.len(), 2);
}

#[test]
fn insert_delete_and_default_values_respect_dialect() {
    for driver in [GridEditDriver::Sqlite, GridEditDriver::Mysql] {
        let mut input = request(driver);
        input.rows[0].deleted = true;
        input.rows[0].touched = vec![false, false];
        input.rows.push(GridEditRow {
            current: vec![Value::Null, Value::Text("new".into())],
            original: vec![],
            touched: vec![false, true],
            inserted: true,
            deleted: false,
        });
        let plan = plan_grid_edits(&input).unwrap();
        assert_eq!(plan.statements.len(), 2);
        assert_eq!(
            plan.statements[0].statement.sql,
            match driver {
                GridEditDriver::Sqlite => {
                    "DELETE FROM \"main\".\"items\" WHERE \"id\" IS ? AND \"title\" IS ?"
                }
                GridEditDriver::Mysql => {
                    "DELETE FROM `db`.`items` WHERE `id` <=> ? AND `title` <=> ?"
                }
                GridEditDriver::Postgres => unreachable!(),
            }
        );
        assert_eq!(plan.statements[0].statement.expected_rows, Some(1));
        assert_eq!(plan.statements[1].statement.expected_rows, None);
        assert_eq!(
            plan.statements[1].statement.sql,
            match driver {
                GridEditDriver::Sqlite => {
                    "INSERT INTO \"main\".\"items\" (\"title\") VALUES (?)"
                }
                GridEditDriver::Mysql => "INSERT INTO `db`.`items` (`title`) VALUES (?)",
                GridEditDriver::Postgres => unreachable!(),
            }
        );
        assert_eq!(
            plan.statements[1].statement.params,
            [Value::Text("new".into())]
        );
        input.rows[0].inserted = true;
        input.rows[0].deleted = true;
        input.rows[1].touched = vec![false, false];
        let defaults = plan_grid_edits(&input).unwrap();
        assert_eq!(defaults.statements.len(), 1);
        assert_eq!(
            defaults.statements[0].statement.sql,
            match driver {
                GridEditDriver::Sqlite => "INSERT INTO \"main\".\"items\" DEFAULT VALUES",
                GridEditDriver::Mysql => "INSERT INTO `db`.`items` () VALUES ()",
                GridEditDriver::Postgres => unreachable!(),
            }
        );
    }
}

#[test]
fn unsafe_originals_and_oversized_binary_parameters_are_rejected() {
    let mut fallback_key = request(GridEditDriver::Sqlite);
    fallback_key.rows[0].deleted = true;
    fallback_key.rows[0].original[0] = Value::Unavailable {
        database_type: "integer".into(),
        reason: "failed".into(),
    };
    assert_eq!(
        plan_grid_edits(&fallback_key).unwrap_err(),
        GridEditError::OpaqueMatch
    );

    let mut deferred = request(GridEditDriver::Postgres);
    deferred.rows[0].original[1] = Value::Deferred {
        handle: Handle {
            slot: 3,
            generation: 1,
        },
        byte_length: 1,
        database_type: "text".into(),
    };
    assert_eq!(
        plan_grid_edits(&deferred).unwrap_err(),
        GridEditError::UnsafeOriginal
    );

    let mut binary = request(GridEditDriver::Mysql);
    binary.rows[0].inserted = true;
    binary.rows[0].original.clear();
    binary.rows[0].current[1] = Value::Binary(vec![0x51; 65537]);
    assert_eq!(
        plan_grid_edits(&binary).unwrap_err(),
        GridEditError::BinaryReviewLimit
    );

    binary.rows[0].current[1] = Value::Binary(vec![0x51; 65536]);
    assert_eq!(
        plan_grid_edits(&binary).unwrap().statements[0]
            .statement
            .params[0],
        Value::Binary(vec![0x51; 65536])
    );

    binary.rows[0].current[1] = Value::DeferredFallback {
        handle: Handle {
            slot: 4,
            generation: 1,
        },
        byte_length: 8,
        database_type: "custom".into(),
    };
    assert_eq!(
        plan_grid_edits(&binary).unwrap_err(),
        GridEditError::DeferredParameter
    );
}

#[test]
fn inserts_only_target_allows_new_rows_but_rejects_updates() {
    let mut input = request(GridEditDriver::Mysql);
    input.columns[0].key = false;
    input.reason = "No stable primary key; inserts only".into();
    let eligibility = grid_editability(&input);
    assert!(eligibility.can_insert);
    assert!(!eligibility.can_delete);
    assert_eq!(
        plan_grid_edits(&input).unwrap_err(),
        GridEditError::UnsupportedTarget
    );
    input.rows[0].inserted = true;
    input.rows[0].original.clear();
    assert!(plan_grid_edits(&input).is_ok());
}

#[test]
fn mismatched_shapes_and_untrusted_table_names_never_make_sql() {
    let mut invalid = request(GridEditDriver::Sqlite);
    invalid.rows[0].touched.pop();
    assert_eq!(
        plan_grid_edits(&invalid).unwrap_err(),
        GridEditError::InvalidShape
    );
    invalid = request(GridEditDriver::Sqlite);
    invalid.qualified_name = "\"main\".\"items\"; DROP TABLE victims".into();
    assert_eq!(
        plan_grid_edits(&invalid).unwrap_err(),
        GridEditError::InvalidIdentifier
    );
    assert!(!grid_editability(&invalid).can_insert);
}

#[test]
fn review_text_lists_each_statement_with_its_bound_values() {
    use choscordb_driver_api::{EditStatement, GridEditPlan, PlannedGridEdit, review_text};
    let planned = |sql: &str, params: Vec<Value>| PlannedGridEdit {
        statement: EditStatement {
            sql: sql.into(),
            params,
            expected_rows: Some(1),
        },
        parameter_types: vec![],
    };
    let plan = GridEditPlan {
        statements: vec![
            planned(
                "UPDATE t SET a = ?, b = ?, c = ? WHERE id = ?",
                vec![
                    Value::Text("say \"hi\"\\\nbye".into()),
                    Value::Null,
                    Value::Binary(vec![0xab, 0x01]),
                    Value::Integer(7),
                ],
            ),
            planned(
                "DELETE FROM t WHERE d = ? AND r = ? AND f = ?",
                vec![
                    Value::Date("2024-02-29".into()),
                    Value::Real(0.1234567891),
                    Value::Bool(false),
                ],
            ),
        ],
    };
    assert_eq!(
        review_text(&plan),
        "UPDATE t SET a = ?, b = ?, c = ? WHERE id = ?\n\
         \x20 Parameter 1: text \"say \\\"hi\\\"\\\\\\nbye\"\n\
         \x20 Parameter 2: NULL\n\
         \x20 Parameter 3: binary 0xab01 (2 bytes)\n\
         \x20 Parameter 4: 7\n\
         \n\
         DELETE FROM t WHERE d = ? AND r = ? AND f = ?\n\
         \x20 Parameter 1: date \"2024-02-29\"\n\
         \x20 Parameter 2: 0.1234567891\n\
         \x20 Parameter 3: false\n\
         \n"
    );
}

#[test]
fn only_complete_values_edit_inline_and_partial_rows_cannot_be_copied_or_deleted() {
    let value = GridCellKind::Value.policy();
    assert!(value.inline_editable && !value.blocks_row_delete && !value.blocks_row_duplicate);
    for kind in [GridCellKind::Binary, GridCellKind::Deferred] {
        let policy = kind.policy();
        assert!(!policy.inline_editable, "{kind:?}");
        assert!(
            !policy.blocks_row_delete && !policy.blocks_row_duplicate,
            "{kind:?}"
        );
    }
    assert!(GridCellKind::Deferred.policy().duplicate_requires_load);
    assert!(!GridCellKind::Binary.policy().duplicate_requires_load);
    for kind in [GridCellKind::FallbackText, GridCellKind::Unavailable] {
        let policy = kind.policy();
        assert!(!policy.inline_editable && policy.blocks_row_delete && policy.blocks_row_duplicate);
    }
}

#[test]
fn staged_rows_stop_at_the_page_size_limit() {
    let full = MAX_PAGE_SIZE as usize;
    assert_eq!(grid_row_insert_error(full - 1), None);
    assert!(grid_row_insert_error(full).is_some());
}

#[test]
fn edit_query_columns_follow_the_result_order() {
    use choscordb_driver_api::{EditColumn, EditQueryTarget, EditTarget};
    let column = |name: &str, key: bool| EditColumn {
        name: name.into(),
        database_type: "INTEGER".into(),
        nullable: false,
        generated: false,
        key,
    };
    let query = EditQueryTarget {
        target: EditTarget {
            columns: vec![column("id", true), column("name", false)],
            ..Default::default()
        },
        source_columns: vec!["name".into(), "total".into(), "id".into()],
        reason: String::new(),
    };
    let aligned = query.aligned_columns();
    let summary: Vec<_> = aligned
        .iter()
        .map(|column| (column.name.as_str(), column.key, column.generated))
        .collect();
    assert_eq!(
        summary,
        vec![
            ("name", false, false),
            ("total", false, true),
            ("id", true, false)
        ]
    );
}

#[test]
fn duplicated_rows_copy_only_plain_named_columns() {
    use choscordb_driver_api::EditColumn;
    let column = |name: &str, key: bool, generated: bool| EditColumn {
        name: name.into(),
        database_type: "INTEGER".into(),
        nullable: true,
        generated,
        key,
    };
    assert!(column("name", false, false).duplicable());
    assert!(!column("id", true, false).duplicable());
    assert!(!column("total", false, true).duplicable());
    assert!(!column("", false, false).duplicable());
}
