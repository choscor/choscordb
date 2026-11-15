use choscordb_core::{
    ObjectAction, ObjectScript, ObjectScriptText, object_action_availability, object_script,
    object_script_availability, prepare_object_action, truncate_traits,
};

fn truncate(cascade: bool, restart_identity: bool) -> ObjectAction {
    ObjectAction::Truncate {
        cascade,
        restart_identity,
    }
}

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

#[test]
fn mariadb_relations_use_the_mysql_dialect() {
    let drop = prepare_object_action(
        ObjectAction::Drop,
        "mariadb",
        "table",
        "[\"db\",\"a`b\"]",
        "ignored",
        "",
        "",
    )
    .unwrap();
    assert_eq!(drop.sql, "DROP TABLE `db`.`a``b`;");
    let rename = prepare_object_action(
        ObjectAction::Rename,
        "mariadb",
        "view",
        "[\"db\",\"order\"]",
        "ignored",
        "x`y",
        "",
    )
    .unwrap();
    assert_eq!(rename.sql, "RENAME TABLE `db`.`order` TO `db`.`x``y`;");
    assert_eq!(rename.new_object_id, "[\"db\",\"x`y\"]");
    assert!(
        prepare_object_action(
            ObjectAction::Rename,
            "mariadb",
            "table",
            "[\"db\",\"old\"]",
            "ignored",
            &"x".repeat(65),
            "",
        )
        .is_err()
    );
}

#[test]
fn mariadb_sequences_drop_and_rename_like_tables() {
    let drop = prepare_object_action(
        ObjectAction::Drop,
        "mariadb",
        "sequence",
        "[\"db\",\"order_ids\"]",
        "ignored",
        "",
        "",
    )
    .unwrap();
    assert_eq!(drop.sql, "DROP SEQUENCE `db`.`order_ids`;");
    let rename = prepare_object_action(
        ObjectAction::Rename,
        "mariadb",
        "sequence",
        "[\"db\",\"order_ids\"]",
        "ignored",
        "invoice_ids",
        "",
    )
    .unwrap();
    assert_eq!(
        rename.sql,
        "RENAME TABLE `db`.`order_ids` TO `db`.`invoice_ids`;"
    );
    assert_eq!(rename.new_object_id, "[\"db\",\"invoice_ids\"]");
    for driver in ["mysql", "postgres", "sqlite"] {
        assert_eq!(
            object_action_availability(ObjectAction::Drop, driver, "sequence", ""),
            Err("This connection does not support sequence actions."),
            "{driver}"
        );
    }
}

#[test]
fn mariadb_system_versioned_tables_drop_and_rename() {
    for action in [ObjectAction::Drop, ObjectAction::Rename] {
        assert_eq!(
            object_action_availability(action, "mariadb", "table", "system_versioned"),
            Ok(())
        );
    }
    assert_eq!(
        prepare_object_action(
            ObjectAction::Drop,
            "mariadb",
            "table",
            "[\"db\",\"audit\"]",
            "ignored",
            "",
            "system_versioned",
        )
        .unwrap()
        .sql,
        "DROP TABLE `db`.`audit`;"
    );
    assert!(
        object_action_availability(ObjectAction::Drop, "mysql", "table", "system_versioned")
            .is_err()
    );
    assert!(
        object_action_availability(ObjectAction::Drop, "mariadb", "view", "system_versioned")
            .is_err()
    );
}

#[test]
fn truncate_uses_each_dialect_and_postgres_options() {
    let postgres = prepare_object_action(
        truncate(true, true),
        "postgres",
        "table",
        "pg:relation:7",
        "\"public\".\"orders\"",
        "",
        "",
    )
    .unwrap();
    assert_eq!(
        postgres.statements,
        ["TRUNCATE TABLE \"public\".\"orders\" RESTART IDENTITY CASCADE;"]
    );
    let plain = prepare_object_action(
        truncate(false, false),
        "postgres",
        "table",
        "pg:relation:7",
        "\"public\".\"orders\"",
        "",
        "",
    )
    .unwrap();
    assert_eq!(plain.sql, "TRUNCATE TABLE \"public\".\"orders\";");
    let mysql = prepare_object_action(
        truncate(false, false),
        "mariadb",
        "table",
        "[\"shop\",\"orders\"]",
        "",
        "",
        "",
    )
    .unwrap();
    assert_eq!(mysql.sql, "TRUNCATE TABLE `shop`.`orders`;");
    let sqlite = prepare_object_action(
        truncate(false, false),
        "sqlite",
        "table",
        "[\"main\",\"orders\"]",
        "",
        "",
        "",
    )
    .unwrap();
    assert_eq!(sqlite.sql, "DELETE FROM \"main\".\"orders\";");
}

#[test]
fn truncate_rejects_options_and_kinds_that_a_driver_cannot_run() {
    assert!(truncate_traits("postgres").cascade && truncate_traits("postgres").restart_identity);
    for driver in ["sqlite", "mysql", "mariadb"] {
        let traits = truncate_traits(driver);
        assert!(!traits.cascade && !traits.restart_identity, "{driver}");
    }
    assert_eq!(
        prepare_object_action(
            truncate(true, false),
            "mysql",
            "table",
            "[\"shop\",\"orders\"]",
            "",
            "",
            ""
        ),
        Err("This connection does not support CASCADE for truncate.")
    );
    assert_eq!(
        prepare_object_action(
            truncate(false, true),
            "sqlite",
            "table",
            "[\"main\",\"orders\"]",
            "",
            "",
            ""
        ),
        Err("This connection does not support RESTART IDENTITY for truncate.")
    );
    assert_eq!(
        object_action_availability(truncate(false, false), "postgres", "view", ""),
        Err("Only tables support this action.")
    );
    assert_eq!(
        object_action_availability(truncate(false, false), "postgres", "table", "foreign_table"),
        Err("Only tables support this action.")
    );
}

#[test]
fn clone_copies_structure_and_optional_rows() {
    let postgres = prepare_object_action(
        ObjectAction::Clone { with_data: true },
        "postgres",
        "table",
        "pg:relation:7",
        "\"public\".\"orders\"",
        "orders copy",
        "",
    )
    .unwrap();
    assert_eq!(
        postgres.statements,
        [
            "CREATE TABLE \"public\".\"orders copy\" (LIKE \"public\".\"orders\" INCLUDING ALL);",
            "INSERT INTO \"public\".\"orders copy\" OVERRIDING SYSTEM VALUE SELECT * FROM \"public\".\"orders\";"
        ]
    );
    assert_eq!(postgres.new_qualified_name, "\"public\".\"orders copy\"");
    let mysql = prepare_object_action(
        ObjectAction::Clone { with_data: false },
        "mysql",
        "table",
        "[\"shop\",\"orders\"]",
        "",
        "orders_copy",
        "",
    )
    .unwrap();
    assert_eq!(
        mysql.statements,
        ["CREATE TABLE `shop`.`orders_copy` LIKE `shop`.`orders`;"]
    );
    assert_eq!(mysql.new_object_id, "[\"shop\",\"orders_copy\"]");
    assert_eq!(
        prepare_object_action(
            ObjectAction::Clone { with_data: false },
            "mysql",
            "table",
            "[\"shop\",\"orders\"]",
            "",
            "orders",
            ""
        ),
        Err("Enter a different object name.")
    );
    assert_eq!(
        object_action_availability(
            ObjectAction::Clone { with_data: true },
            "sqlite",
            "table",
            ""
        ),
        Err("SQLite cannot clone a table with its keys and indexes.")
    );
}

#[test]
fn action_codes_select_each_action() {
    assert_eq!(
        ObjectAction::from_code("truncate", true, false, true),
        Some(truncate(true, false))
    );
    assert_eq!(
        ObjectAction::from_code("clone", false, false, true),
        Some(ObjectAction::Clone { with_data: true })
    );
    assert_eq!(
        ObjectAction::from_code("drop", true, true, true),
        Some(ObjectAction::Drop)
    );
    assert_eq!(ObjectAction::from_code("purge", false, false, false), None);
}

#[test]
fn copy_scripts_build_sql_or_request_ddl() {
    let id = "[\"main\",\"orders\"]";
    let script = |script| object_script(script, "sqlite", "table", id, "", "");
    assert_eq!(script(ObjectScript::Create), Ok(ObjectScriptText::Ddl));
    assert_eq!(
        script(ObjectScript::Drop),
        Ok(ObjectScriptText::Sql(
            "DROP TABLE \"main\".\"orders\";".into()
        ))
    );
    assert_eq!(
        script(ObjectScript::Truncate),
        Ok(ObjectScriptText::Sql(
            "DELETE FROM \"main\".\"orders\";".into()
        ))
    );
    assert_eq!(
        script(ObjectScript::SelectRows),
        Ok(ObjectScriptText::Sql(
            "SELECT * FROM \"main\".\"orders\" LIMIT 100;".into()
        ))
    );
    assert_eq!(
        object_script(ObjectScript::Create, "sqlite", "table", "bad", "", ""),
        Err("The selected object has an invalid identity.")
    );
    assert_eq!(
        object_script_availability(ObjectScript::Truncate, "mysql", "view", ""),
        Err("Only tables support this action.")
    );
    assert_eq!(
        object_script_availability(ObjectScript::SelectRows, "mariadb", "sequence", ""),
        Err("Only tables and views support this script.")
    );
    assert_eq!(ObjectScript::SelectRows.label(), "SELECT 100 rows");
    assert_eq!(
        ObjectScript::from_code("select"),
        Some(ObjectScript::SelectRows)
    );
}
