use choscordb_driver_api::{Value, parse_grid_edit_value, parse_grid_edit_value_checked};

#[test]
fn explicit_drafts_report_actionable_type_errors_without_inferring_null() {
    assert_eq!(
        parse_grid_edit_value_checked("INTEGER", "3.0"),
        Err("Enter a whole number from -9223372036854775808 to 9223372036854775807.".into())
    );
    assert_eq!(
        parse_grid_edit_value_checked("BOOL", "yes"),
        Err("Enter true, false, 1, or 0 without surrounding whitespace.".into())
    );
    assert_eq!(
        parse_grid_edit_value_checked("float8", "1e309"),
        Err("Enter a representable real number, NaN, inf, or -inf.".into())
    );
    assert_eq!(
        parse_grid_edit_value_checked("text", "NULL"),
        Ok(Value::Text("NULL".into()))
    );
    assert_eq!(
        parse_grid_edit_value_checked("text", ""),
        Ok(Value::Text("".into()))
    );
}

#[test]
fn integer_aliases_parse_signed_64_bit_values() {
    for database_type in [
        "integer", "INT", "bigint", "smallint", "int2", "int4", "int8",
    ] {
        assert_eq!(
            parse_grid_edit_value(database_type, " -9223372036854775808 "),
            Some(Value::Integer(i64::MIN))
        );
        assert_eq!(
            parse_grid_edit_value(database_type, "+9223372036854775807"),
            Some(Value::Integer(i64::MAX))
        );
    }
    assert!(parse_grid_edit_value("integer", "9223372036854775808").is_none());
    assert!(parse_grid_edit_value("integer", "3.0").is_none());
}

#[test]
fn boolean_numeric_real_and_unknown_types_keep_their_current_meaning() {
    for database_type in ["boolean", "BOOL"] {
        assert_eq!(
            parse_grid_edit_value(database_type, "TrUe"),
            Some(Value::Bool(true))
        );
        assert_eq!(
            parse_grid_edit_value(database_type, "0"),
            Some(Value::Bool(false))
        );
        assert!(parse_grid_edit_value(database_type, " true ").is_none());
        assert!(parse_grid_edit_value(database_type, "yes").is_none());
    }
    assert_eq!(
        parse_grid_edit_value("NUMERIC(30,2)", "not-a-number"),
        Some(Value::Decimal("not-a-number".into()))
    );
    assert_eq!(
        parse_grid_edit_value("decimal", "2.500"),
        Some(Value::Decimal("2.500".into()))
    );
    assert_eq!(
        parse_grid_edit_value("custom_type", "02.5"),
        Some(Value::Text("02.5".into()))
    );
    for database_type in ["real", "double precision", "float4", "FLOAT8"] {
        assert_eq!(
            parse_grid_edit_value(database_type, " 1.25 "),
            Some(Value::Real(1.25))
        );
        assert!(parse_grid_edit_value(database_type, "1e309").is_none());
        assert!(parse_grid_edit_value(database_type, "1e-999").is_none());
        assert!(parse_grid_edit_value(database_type, "1,25").is_none());
    }
    assert_eq!(
        parse_grid_edit_value("real", "0e999"),
        Some(Value::Real(0.0))
    );
    assert_eq!(parse_grid_edit_value("real", ".5"), Some(Value::Real(0.5)));
    assert!(matches!(parse_grid_edit_value("real", "1e-323"), Some(Value::Real(v)) if v > 0.0));
    assert!(parse_grid_edit_value("real", "1e-324").is_none());
    assert!(matches!(parse_grid_edit_value("real", "NaN"), Some(Value::Real(n)) if n.is_nan()));
    assert_eq!(
        parse_grid_edit_value("real", "-inf"),
        Some(Value::Real(f64::NEG_INFINITY))
    );
    assert!(parse_grid_edit_value("real", "+nan").is_none());
}
