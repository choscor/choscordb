use choscordb_driver_api::{Handle, Value, foreign_key_predicate, foreign_key_value_filterable};

#[test]
fn quotes_identifier_and_text_literal_without_executing_embedded_sql() {
    let value = Value::Text("O'Reilly'; DROP TABLE victims;--".into());
    assert!(foreign_key_value_filterable(&value));
    assert_eq!(
        foreign_key_predicate("key\"name", &value),
        Some("\"key\"\"name\" = 'O''Reilly''; DROP TABLE victims;--'".into())
    );
    assert_eq!(
        foreign_key_predicate("key\"; DROP TABLE victims;--", &Value::Integer(7)),
        Some("\"key\"\"; DROP TABLE victims;--\" = 7".into())
    );
}

#[test]
fn scalar_literals_match_existing_qt_navigation_output() {
    for (value, expected) in [
        (Value::Bool(true), "\"id\" = 1"),
        (Value::Bool(false), "\"id\" = 0"),
        (Value::Integer(-42), "\"id\" = -42"),
        (Value::Real(0.1), "\"id\" = 0.10000000000000001"),
        (Value::Real(1e-5), "\"id\" = 1.0000000000000001e-05"),
        (Value::Real(-0.0), "\"id\" = 0"),
        (Value::Decimal("2.50".into()), "\"id\" = 2.50"),
    ] {
        assert!(foreign_key_value_filterable(&value));
        assert_eq!(
            foreign_key_predicate("id", &value).as_deref(),
            Some(expected)
        );
    }
}

#[test]
fn only_exact_decimal_representations_can_make_numeric_filters() {
    for decimal in ["0.1", "9007199254740993", "1e-100", "+002.500"] {
        let value = Value::Decimal(decimal.into());
        assert!(foreign_key_value_filterable(&value), "expected {decimal}");
        assert_eq!(
            foreign_key_predicate("amount", &value),
            Some(format!("\"amount\" = {decimal}"))
        );
    }
    for decimal in [
        "0.10000000000000001",
        "12e999",
        "1; DROP TABLE x",
        "-",
        "1e10001",
    ] {
        let value = Value::Decimal(decimal.into());
        assert!(
            !foreign_key_value_filterable(&value),
            "unexpected {decimal}"
        );
        assert!(foreign_key_predicate("amount", &value).is_none());
    }
}

#[test]
fn unsupported_values_and_invalid_identifiers_do_not_make_filters() {
    let handle = Handle {
        slot: 1,
        generation: 2,
    };
    for value in [
        Value::Null,
        Value::Real(f64::NAN),
        Value::Real(f64::INFINITY),
        Value::Binary(vec![0]),
        Value::Deferred {
            handle,
            byte_length: 5,
            database_type: "text".into(),
        },
        Value::DeferredFallback {
            handle,
            byte_length: 5,
            database_type: "unknown".into(),
        },
        Value::FallbackText {
            text: "raw".into(),
            database_type: "unknown".into(),
        },
        Value::Unavailable {
            database_type: "integer".into(),
            reason: "decode failed".into(),
        },
    ] {
        assert!(!foreign_key_value_filterable(&value));
        assert!(foreign_key_predicate("id", &value).is_none());
    }
    for identifier in ["", "bad\0name"] {
        assert!(foreign_key_predicate(identifier, &Value::Integer(1)).is_none());
    }
}
