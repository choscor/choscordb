use choscordb_core::{ObjectAction, object_action_availability, prepare_object_action};

#[test]
fn drop_uses_trusted_identity_and_quotes_each_dialect() {
    let sqlite = prepare_object_action(
        ObjectAction::Drop,
        "sqlite",
        "table",
        r#"["main","a.\"b"]"#,
        "untrusted display text",
        "",
        "",
    )
    .unwrap();
    assert_eq!(sqlite.sql, "DROP TABLE \"main\".\"a.\"\"b\";");

    let postgres = prepare_object_action(
        ObjectAction::Drop,
        "postgres",
        "view",
        "pg:relation:42",
        "\"public\".\"order\"",
        "",
        "",
    )
    .unwrap();
    assert_eq!(postgres.sql, "DROP VIEW \"public\".\"order\";");

    let mysql = prepare_object_action(
        ObjectAction::Drop,
        "mysql",
        "table",
        "[\"db\",\"a`b\"]",
        "untrusted display text",
        "",
        "",
    )
    .unwrap();
    assert_eq!(mysql.sql, "DROP TABLE `db`.`a``b`;");
}

#[test]
fn rename_preserves_identity_and_escapes_the_target_name() {
    let sqlite = prepare_object_action(
        ObjectAction::Rename,
        "sqlite",
        "table",
        "[\"aux\",\"old.name\"]",
        "ignored",
        "x\"; DROP TABLE victims;--",
        "",
    )
    .unwrap();
    assert_eq!(
        sqlite.sql,
        "ALTER TABLE \"aux\".\"old.name\" RENAME TO \"x\"\"; DROP TABLE victims;--\";"
    );
    assert_eq!(
        sqlite.new_object_id,
        "[\"aux\",\"x\\\"; DROP TABLE victims;--\"]"
    );
    assert_eq!(
        sqlite.new_qualified_name,
        "\"aux\".\"x\"\"; DROP TABLE victims;--\""
    );

    let postgres = prepare_object_action(
        ObjectAction::Rename,
        "postgres",
        "view",
        "pg:relation:42",
        "\"odd.schema\".\"a\"\"b\"",
        "select.x",
        "",
    )
    .unwrap();
    assert_eq!(
        postgres.sql,
        "ALTER VIEW \"odd.schema\".\"a\"\"b\" RENAME TO \"select.x\";"
    );
    assert_eq!(postgres.new_object_id, "pg:relation:42");

    let mysql = prepare_object_action(
        ObjectAction::Rename,
        "mysql",
        "view",
        "[\"db\",\"order\"]",
        "ignored",
        "x`y",
        "",
    )
    .unwrap();
    assert_eq!(mysql.sql, "RENAME TABLE `db`.`order` TO `db`.`x``y`;");
    assert_eq!(mysql.new_object_id, "[\"db\",\"x`y\"]");
}

#[test]
fn rejects_unsupported_relations_and_invalid_identifiers() {
    let sqlite_id = "[\"main\",\"orders\"]";
    for target in ["", "orders", "  ", "bad\0name", "SQLite_foo"] {
        assert!(
            prepare_object_action(
                ObjectAction::Rename,
                "sqlite",
                "table",
                sqlite_id,
                "ignored",
                target,
                ""
            )
            .is_err(),
            "target {target:?} should be rejected"
        );
    }
    let nearby_valid = prepare_object_action(
        ObjectAction::Rename,
        "sqlite",
        "table",
        sqlite_id,
        "ignored",
        "sqlitefoo",
        "",
    )
    .unwrap();
    assert_eq!(
        nearby_valid.sql,
        "ALTER TABLE \"main\".\"orders\" RENAME TO \"sqlitefoo\";"
    );
    assert_eq!(
        prepare_object_action(
            ObjectAction::Rename,
            "sqlite",
            "view",
            sqlite_id,
            "ignored",
            "other",
            ""
        ),
        Err("SQLite does not support renaming views directly.")
    );
    for (driver, kind, id, qualified, subtype) in [
        ("sqlite", "index", sqlite_id, "ignored", ""),
        ("sqlite", "table", "[\"main\",\"a\",\"b\"]", "ignored", ""),
        ("sqlite", "table", "not-json", "ignored", ""),
        (
            "postgres",
            "table",
            "pg:relation:42",
            "\"main\".\"safe\";DROP TABLE x;",
            "",
        ),
        (
            "postgres",
            "table",
            "pg:relation:0",
            "\"main\".\"safe\"",
            "",
        ),
        (
            "postgres",
            "view",
            "pg:relation:42",
            "\"s\".\"v\"",
            "foreign_table",
        ),
        ("sqlite", "table", sqlite_id, "ignored", "foreign_table"),
        (
            "postgres",
            "table",
            "pg:relation:42",
            "\"s\".\"v\"",
            "materialized_view",
        ),
        (
            "postgres",
            "view",
            "pg:relation:42",
            "\"s\".\"v\"",
            "unknown",
        ),
    ] {
        assert!(
            prepare_object_action(ObjectAction::Drop, driver, kind, id, qualified, "", subtype)
                .is_err(),
            "invalid {driver} {kind} relation should be rejected"
        );
    }
    for (driver, id, qualified, target) in [
        (
            "postgres",
            "pg:relation:42",
            "\"main\".\"safe\"",
            "x".repeat(64),
        ),
        ("mysql", "[\"db\",\"old\"]", "ignored", "x".repeat(65)),
        (
            "mysql",
            "[\"db\",\"old\"]",
            "ignored",
            "trailing ".to_owned(),
        ),
        ("mysql", "[\"db\",\"old\"]", "ignored", "😀".to_owned()),
    ] {
        assert!(
            prepare_object_action(
                ObjectAction::Rename,
                driver,
                "table",
                id,
                qualified,
                &target,
                ""
            )
            .is_err()
        );
    }
}

#[test]
fn postgres_special_relations_use_their_own_ddl() {
    for (kind, subtype, verb) in [
        ("view", "materialized_view", "MATERIALIZED VIEW"),
        ("table", "foreign_table", "FOREIGN TABLE"),
    ] {
        let drop = prepare_object_action(
            ObjectAction::Drop,
            "postgres",
            kind,
            "pg:relation:84",
            "\"s\".\"old\"",
            "",
            subtype,
        )
        .unwrap();
        assert_eq!(drop.sql, format!("DROP {verb} \"s\".\"old\";"));
        let rename = prepare_object_action(
            ObjectAction::Rename,
            "postgres",
            kind,
            "pg:relation:84",
            "\"s\".\"old\"",
            "new.name",
            subtype,
        )
        .unwrap();
        assert_eq!(
            rename.sql,
            format!("ALTER {verb} \"s\".\"old\" RENAME TO \"new.name\";")
        );
    }
}

#[test]
fn display_identity_preserves_quoted_dots_and_escaped_delimiters() {
    use choscordb_core::object_display_identity;
    for (id, label, schema, name) in [
        (r#"["main","a.\"b"]"#, "ignored", Some("main"), "a.\"b"),
        (
            "pg:relation:42",
            r#""odd.schema"."a""b""#,
            Some("odd.schema"),
            "a\"b",
        ),
        ("opaque", "`odd.db`.`a``b`", Some("odd.db"), "a`b"),
        ("opaque", "public.table", Some("public"), "table"),
        ("opaque", "table", None, "table"),
        ("opaque", "\"unterminated", None, "\"unterminated"),
    ] {
        let actual = object_display_identity(id, label);
        assert_eq!(
            actual,
            (schema.map(str::to_owned), name.to_owned()),
            "{label}"
        );
    }
}

#[test]
fn availability_needs_no_identity_and_matches_preparation() {
    for driver in ["sqlite", "postgres", "mysql"] {
        for action in [ObjectAction::Drop, ObjectAction::Rename] {
            assert_eq!(
                object_action_availability(action, driver, "table", ""),
                Ok(())
            );
        }
    }
    assert_eq!(
        object_action_availability(ObjectAction::Rename, "sqlite", "view", ""),
        Err("SQLite does not support renaming views directly.")
    );
    assert_eq!(
        object_action_availability(ObjectAction::Drop, "sqlite", "view", ""),
        Ok(())
    );
    assert!(object_action_availability(ObjectAction::Drop, "postgres", "index", "").is_err());
    assert!(object_action_availability(ObjectAction::Drop, "oracle", "table", "").is_err());
    assert_eq!(
        object_action_availability(ObjectAction::Drop, "postgres", "view", "materialized_view"),
        Ok(())
    );
    assert!(
        object_action_availability(ObjectAction::Drop, "mysql", "view", "materialized_view")
            .is_err()
    );
}
