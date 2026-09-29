use choscordb_driver_api::{
    is_postgres_system_schema, navigator_object_visible, postgres_schema_from_qualified,
};

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
