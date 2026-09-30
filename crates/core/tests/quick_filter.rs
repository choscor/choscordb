use choscordb_core::quick_filter::{Operator, options, predicate};
use choscordb_driver_api::Value;
#[test]
fn numeric_cell_templates_and_literals() {
    let items = options("id", &Value::Integer(42));
    assert_eq!(
        items
            .iter()
            .map(|item| item.template.as_str())
            .collect::<Vec<_>>(),
        [
            "id = value",
            "id != value",
            "id < value",
            "id <= value",
            "id > value",
            "id >= value",
            "id IN (value)",
            "id LIKE value"
        ]
    );
    assert_eq!(
        predicate("id", &Value::Integer(42), Operator::Equals).unwrap(),
        "\"id\" = 42"
    );
    assert_eq!(
        predicate("id", &Value::Integer(-7), Operator::In).unwrap(),
        "\"id\" IN (-7)"
    );
    assert_eq!(
        predicate("id", &Value::Real(2.5), Operator::LessEqual).unwrap(),
        "\"id\" <= 2.5"
    );
    assert!(items[..7].iter().all(|item| item.expression.is_some()));
    assert!(items[7].expression.is_none());
    assert!(!items[7].reason.is_empty());
}
#[test]
fn typed_cells_are_literal_safe_and_adapt_operators() {
    for (value, literal, count) in [
        (Value::Null, "IS NULL", 2),
        (Value::Bool(true), "= 1", 8),
        (Value::Bool(false), "= 0", 8),
        (Value::Binary(vec![0, 255]), "= X'00ff'", 8),
        (Value::Binary(vec![]), "= X''", 8),
        (Value::Decimal("2.50".into()), "= 2.5", 8),
        (
            Value::Text("O'Reilly, Việt".into()),
            "= 'O''Reilly, Việt'",
            8,
        ),
        (Value::Text("".into()), "= ''", 8),
        (Value::Date("2026-10-01".into()), "= '2026-10-01'", 8),
        (Value::Json("{\"x\":1}".into()), "= '{\"x\":1}'", 8),
    ] {
        let items = options("a\"b", &value);
        assert_eq!(items.len(), count);
        assert_eq!(
            items[0].expression.as_deref(),
            Some(format!("\"a\"\"b\" {literal}").as_str())
        );
        if matches!(value, Value::Bool(_) | Value::Binary(_)) {
            assert!(
                items[2..6]
                    .iter()
                    .all(|item| item.expression.is_none() && !item.reason.is_empty())
            );
            assert!(items[6].expression.is_some());
        }
    }
    assert_eq!(
        predicate("n", &Value::Null, Operator::IsNotNull).unwrap(),
        "\"n\" IS NOT NULL"
    );
    assert_eq!(
        predicate("n", &Value::Text("a,b".into()), Operator::In).unwrap(),
        "\"n\" IN ('a,b')"
    );
    for value in [
        Value::Real(f64::INFINITY),
        Value::Real(f64::NAN),
        Value::Decimal("0.10000000000000001".into()),
        Value::Text("x\0y".into()),
        Value::Unavailable {
            reason: "missing".into(),
            database_type: "text".into(),
        },
        Value::FallbackText {
            text: "preview".into(),
            database_type: "custom".into(),
        },
        Value::Deferred {
            handle: choscordb_driver_api::Handle {
                slot: 1,
                generation: 0,
            },
            byte_length: 4,
            database_type: "text".into(),
        },
    ] {
        assert!(
            options("n", &value)
                .iter()
                .all(|item| item.expression.is_none() && !item.reason.is_empty())
        );
    }
    assert!(predicate("", &Value::Integer(1), Operator::Equals).is_err());
    assert!(predicate("x\0y", &Value::Integer(1), Operator::Equals).is_err());
    assert!(predicate("n", &Value::Decimal("0.1".into()), Operator::Equals).is_ok());
}
#[test]
fn contains_like_escapes_literal_wildcards_and_quotes() {
    assert_eq!(
        predicate(
            "name",
            &Value::Text("50%_\\O'Reilly".into()),
            Operator::Like
        )
        .unwrap(),
        "\"name\" LIKE '%50\\%\\_\\\\O''Reilly%' ESCAPE '\\'"
    );
    assert_eq!(
        predicate("name", &Value::Text("".into()), Operator::Like).unwrap(),
        "\"name\" LIKE '%%' ESCAPE '\\'"
    );
    assert!(predicate("id", &Value::Integer(42), Operator::Like).is_err());
}

fn columns() -> Vec<choscordb_driver_api::Column> {
    ["id", "name", "active", "AND OR"]
        .into_iter()
        .map(|name| choscordb_driver_api::Column {
            name: name.into(),
            database_type: "".into(),
            precision: None,
            scale: None,
            timezone: None,
            nullable: None,
        })
        .collect()
}
#[test]
fn composition_preserves_precedence_and_flattens_repeated_filters() {
    use choscordb_core::quick_filter::compose;
    let next = "\"name\" = 'Alice'";
    for (draft, expected) in [
        ("  ", "\"name\" = 'Alice'"),
        ("id > 1", "id > 1 AND \"name\" = 'Alice'"),
        (
            "id = 1 OR id = 2",
            "(id = 1 OR id = 2) AND \"name\" = 'Alice'",
        ),
        ("(((id > 1)))", "id > 1 AND \"name\" = 'Alice'"),
        (
            "((id = 1 OR id = 2))",
            "(id = 1 OR id = 2) AND \"name\" = 'Alice'",
        ),
        (
            "id BETWEEN 1 AND 2",
            "id BETWEEN 1 AND 2 AND \"name\" = 'Alice'",
        ),
        (
            "name = 'AND OR (x)'",
            "name = 'AND OR (x)' AND \"name\" = 'Alice'",
        ),
        ("\"AND OR\" = 1", "\"AND OR\" = 1 AND \"name\" = 'Alice'"),
        (
            "CASE WHEN id = 1 OR id = 2 THEN 1 ELSE 0 END",
            "CASE WHEN id = 1 OR id = 2 THEN 1 ELSE 0 END AND \"name\" = 'Alice'",
        ),
        (
            "coalesce((id = 1 OR id = 2), 0)",
            "coalesce((id = 1 OR id = 2), 0) AND \"name\" = 'Alice'",
        ),
        (
            "(SELECT id = 1 OR id = 2)",
            "(SELECT id = 1 OR id = 2) AND \"name\" = 'Alice'",
        ),
    ] {
        assert_eq!(
            compose(&columns(), draft, next).unwrap().expression,
            expected
        );
    }
    let first = compose(&columns(), "id = 1 OR id = 2", next)
        .unwrap()
        .expression;
    let second = compose(&columns(), &first, "\"active\" = 1")
        .unwrap()
        .expression;
    assert_eq!(
        second,
        "(id = 1 OR id = 2) AND \"name\" = 'Alice' AND \"active\" = 1"
    );
    assert_eq!(
        compose(&columns(), &second, "\"active\" = 1")
            .unwrap()
            .expression,
        format!("{second} AND \"active\" = 1")
    );
    let invalid = compose(&columns(), "id = (", next).unwrap();
    assert!(invalid.expression.contains("id = ("));
    assert!(!invalid.validation_error.is_empty());
}
