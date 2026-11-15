use choscordb_driver_api::{
    GridCellExpression, GridEditColumn, GridEditDriver, GridEditError, GridEditRequest,
    GridEditRow, GridTemporalKind, SetValueError, SetValueKind, SetValueOutcome, Value,
    grid_binary_column, grid_set_value, plan_grid_edits,
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

fn request(
    driver: GridEditDriver,
    expression: GridCellExpression,
    inserted: bool,
) -> GridEditRequest {
    GridEditRequest {
        driver,
        qualified_name: match driver {
            GridEditDriver::Mysql => "`items`",
            _ => "\"items\"",
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
            column("stamp", "timestamp", false),
        ],
        rows: vec![GridEditRow {
            current: vec![Value::Integer(7), Value::Null],
            original: if inserted {
                vec![]
            } else {
                vec![Value::Integer(7), Value::Null]
            },
            touched: vec![inserted, true],
            expressions: vec![None, Some(expression)],
            inserted,
            deleted: false,
        }],
    }
}

#[test]
fn expressions_are_written_into_updates_without_parameters() {
    let plan = plan_grid_edits(&request(
        GridEditDriver::Postgres,
        GridCellExpression::CurrentTimestamp,
        false,
    ))
    .unwrap();
    assert_eq!(
        plan.statements[0].statement.sql,
        "UPDATE \"items\" SET \"stamp\" = CURRENT_TIMESTAMP WHERE \"id\" IS NOT DISTINCT FROM $1 AND \"stamp\" IS NOT DISTINCT FROM $2"
    );
    let plan = plan_grid_edits(&request(
        GridEditDriver::Mysql,
        GridCellExpression::Default,
        false,
    ))
    .unwrap();
    assert!(
        plan.statements[0]
            .statement
            .sql
            .starts_with("UPDATE `items` SET `stamp` = DEFAULT WHERE")
    );
    assert_eq!(
        plan_grid_edits(&request(
            GridEditDriver::Sqlite,
            GridCellExpression::Default,
            false
        ))
        .unwrap_err(),
        GridEditError::DefaultUnsupported
    );
}

#[test]
fn inserted_rows_omit_default_and_write_other_expressions() {
    let plan = plan_grid_edits(&request(
        GridEditDriver::Sqlite,
        GridCellExpression::CurrentDate,
        true,
    ))
    .unwrap();
    assert_eq!(
        plan.statements[0].statement.sql,
        "INSERT INTO \"items\" (\"id\", \"stamp\") VALUES (?, CURRENT_DATE)"
    );
    assert_eq!(plan.statements[0].statement.params, [Value::Integer(7)]);
    let plan = plan_grid_edits(&request(
        GridEditDriver::Sqlite,
        GridCellExpression::Default,
        true,
    ))
    .unwrap();
    assert_eq!(
        plan.statements[0].statement.sql,
        "INSERT INTO \"items\" (\"id\") VALUES (?)"
    );
}

#[test]
fn set_value_choices_follow_the_column_type_and_driver() {
    use SetValueKind::*;
    let sqlite = GridEditDriver::Sqlite;
    let postgres = GridEditDriver::Postgres;
    assert_eq!(
        grid_set_value(Null, postgres, "text", false, Some(true)),
        Ok(SetValueOutcome::Null)
    );
    assert_eq!(
        grid_set_value(Null, postgres, "text", false, Some(false)),
        Err(SetValueError::NotNullable)
    );
    assert_eq!(
        grid_set_value(Empty, postgres, "character varying(20)", false, None),
        Ok(SetValueOutcome::Text(String::new()))
    );
    assert_eq!(
        grid_set_value(Empty, postgres, "integer", false, None),
        Err(SetValueError::NotText)
    );
    assert_eq!(
        grid_set_value(Now, postgres, "date", false, None),
        Ok(SetValueOutcome::Expression(GridCellExpression::CurrentDate))
    );
    assert_eq!(
        grid_set_value(Now, postgres, "time without time zone", false, None),
        Ok(SetValueOutcome::Expression(GridCellExpression::CurrentTime))
    );
    assert_eq!(
        grid_set_value(Now, GridEditDriver::Mysql, "datetime", false, None),
        Ok(SetValueOutcome::Expression(
            GridCellExpression::CurrentTimestamp
        ))
    );
    assert_eq!(
        grid_set_value(Now, sqlite, "text", false, None),
        Err(SetValueError::NotTemporal)
    );
    assert_eq!(
        grid_set_value(Default, sqlite, "text", false, None),
        Err(SetValueError::DefaultUnsupported)
    );
    assert_eq!(
        grid_set_value(Default, sqlite, "text", true, None),
        Ok(SetValueOutcome::Omit)
    );
    assert_eq!(
        grid_set_value(Default, postgres, "text", false, None),
        Ok(SetValueOutcome::Expression(GridCellExpression::Default))
    );
    assert_eq!(
        SetValueKind::ALL.map(SetValueKind::label),
        ["NULL", "DEFAULT", "Empty", "Now()"]
    );
    assert_eq!(GridCellExpression::CurrentTime.sql(), "CURRENT_TIME");
}

#[test]
fn binary_columns_accept_files() {
    for kind in ["bytea", "BLOB", "longblob", "varbinary(16)", "binary(4)"] {
        assert!(grid_binary_column(kind), "{kind}");
    }
    for kind in ["text", "integer", "json", ""] {
        assert!(!grid_binary_column(kind), "{kind}");
    }
}

#[test]
fn temporal_columns_get_iso_text_from_picked_parts() {
    assert_eq!(GridTemporalKind::of("DATE"), Some(GridTemporalKind::Date));
    assert_eq!(
        GridTemporalKind::of("timestamptz"),
        Some(GridTemporalKind::Timestamp)
    );
    assert_eq!(
        GridTemporalKind::of("time(3)"),
        Some(GridTemporalKind::Time)
    );
    assert_eq!(GridTemporalKind::of("text"), None);
    let date = (2026, 11, 5);
    let time = (7, 8, 9);
    assert_eq!(
        GridTemporalKind::Date.text(date, time).as_deref(),
        Some("2026-11-05")
    );
    assert_eq!(
        GridTemporalKind::Time.text(date, time).as_deref(),
        Some("07:08:09")
    );
    assert_eq!(
        GridTemporalKind::Timestamp.text(date, time).as_deref(),
        Some("2026-11-05 07:08:09")
    );
    assert_eq!(GridTemporalKind::Date.text((2026, 13, 1), time), None);
    assert_eq!(GridTemporalKind::Time.text(date, (24, 0, 0)), None);
}
