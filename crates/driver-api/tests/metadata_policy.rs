use choscordb_driver_api::{
    is_postgres_system_schema, is_system_schema_node, navigator_object_visible,
    postgres_schema_from_qualified, system_schemas_hidden,
};

#[test]
fn only_drivers_with_system_schemas_hide_objects_until_shown() {
    assert!(system_schemas_hidden("postgres", false));
    assert!(system_schemas_hidden("Postgres", false));
    assert!(!system_schemas_hidden("postgres", true));
    for driver in ["sqlite", "mysql", ""] {
        assert!(!system_schemas_hidden(driver, false));
    }
}

#[test]
fn postgres_system_schemas_are_hidden_until_enabled() {
    for name in ["pg_catalog", "pg_temp_3", "pg_toast", "information_schema"] {
        assert!(is_postgres_system_schema(name));
        assert!(!navigator_object_visible(
            "postgres",
            false,
            &format!("{name}.objects")
        ));
        assert!(navigator_object_visible(
            "postgres",
            true,
            &format!("{name}.objects")
        ));
    }
    assert!(!is_postgres_system_schema("public"));
    assert!(navigator_object_visible(
        "postgres",
        false,
        "public.objects"
    ));
}

#[test]
fn quoted_schema_names_and_escaped_quotes_are_parsed_without_losing_case() {
    for (qualified, schema) in [
        ("public.orders", "public"),
        ("Public.orders", "public"),
        ("\"public\".\"odd.name\"", "public"),
        ("\"odd\"\"schema\".items", "odd\"schema"),
        ("\"PG_catalog\".items", "PG_catalog"),
    ] {
        assert_eq!(
            postgres_schema_from_qualified(qualified).as_deref(),
            Some(schema)
        );
        assert!(navigator_object_visible("postgres", false, qualified));
    }
    assert_eq!(
        postgres_schema_from_qualified("PG_catalog.items"),
        Some("pg_catalog".into())
    );
    assert!(!navigator_object_visible(
        "postgres",
        false,
        "PG_catalog.items"
    ));
    assert!(!navigator_object_visible(
        "POSTGRES",
        false,
        "\"pg_temp_3\".items"
    ));
    assert!(navigator_object_visible(
        "postgres",
        false,
        "\"Pg_catalog\".items"
    ));
}

#[test]
fn malformed_postgres_names_fail_closed_but_other_drivers_are_unchanged() {
    for malformed in [
        "unqualified",
        "",
        ".items",
        "public.",
        "public..items",
        "\"\".items",
        "\"unterminated.items",
        "public.\"unterminated",
        "public.\"bad\"tail",
        "public.items; DROP TABLE victims",
        "public.\"\"",
    ] {
        assert!(
            postgres_schema_from_qualified(malformed).is_none(),
            "{malformed}"
        );
        assert!(!navigator_object_visible("postgres", false, malformed));
        assert!(navigator_object_visible("postgres", true, malformed));
        assert!(navigator_object_visible("mysql", false, malformed));
        assert!(navigator_object_visible("sqlite", false, malformed));
    }
}

#[test]
fn primary_key_columns_have_an_available_positive_position() {
    use choscordb_driver_api::{MetadataProperty, PRIMARY_KEY_POSITION, primary_key_column};
    assert!(primary_key_column(&[MetadataProperty::available(
        PRIMARY_KEY_POSITION,
        2
    )]));
    assert!(!primary_key_column(&[MetadataProperty::available(
        PRIMARY_KEY_POSITION,
        0
    )]));
    assert!(!primary_key_column(&[MetadataProperty::available(
        "Ordinal", 1
    )]));
    assert!(!primary_key_column(&[]));
}

#[test]
fn result_cell_metadata_labels_are_bounded() {
    use choscordb_driver_api::{
        ErrorKind, MAX_RESULT_CELL_METADATA_BYTES, ResultCellMetadata, check_result_cell_metadata,
    };
    let column = |label: String| ResultCellMetadata {
        source_column: label,
        source_object: String::new(),
        source_qualified_name: String::new(),
        nullable: None,
        boolean: false,
        enum_choices: vec!["a".into()],
        fk_target_object: String::new(),
        fk_target_qualified_name: String::new(),
        fk_target_column: String::new(),
    };
    let half = "x".repeat(MAX_RESULT_CELL_METADATA_BYTES / 2 - 1);
    assert!(check_result_cell_metadata(&[column(half.clone()), column(half.clone())]).is_ok());
    let error = check_result_cell_metadata(&[column(half.clone()), column(half + "xy")])
        .expect_err("labels beyond the budget");
    assert_eq!(error.kind, ErrorKind::ResourceLimit);
    assert_eq!(error.message, "Result column metadata exceeds 1 MiB.");
}

#[test]
fn only_schema_nodes_hide_as_system_schemas() {
    assert!(is_system_schema_node("schema", "pg_catalog"));
    assert!(!is_system_schema_node("schema", "public"));
    assert!(!is_system_schema_node("table", "pg_catalog"));
}

#[test]
fn only_column_rows_carry_column_metadata() {
    use choscordb_driver_api::{Column, ObjectId, ObjectKind, SchemaObject};
    let object = |kind| SchemaObject {
        id: ObjectId("id".into()),
        parent: None,
        name: "total".into(),
        qualified_name: "public.orders.total".into(),
        kind,
        has_children: false,
        column: Some(Column {
            name: "total".into(),
            database_type: "numeric".into(),
            precision: Some(10),
            scale: Some(2),
            timezone: None,
            nullable: Some(false),
        }),
        properties: Vec::new(),
    };
    let column = object(ObjectKind::Column);
    assert_eq!(
        column.column_metadata().map(|c| c.database_type.as_str()),
        Some("numeric")
    );
    assert!(object(ObjectKind::Table).column_metadata().is_none());
    assert!(object(ObjectKind::Index).column_metadata().is_none());
}
