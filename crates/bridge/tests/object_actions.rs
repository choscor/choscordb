use choscordb_bridge::{
    ffi, object_action_unavailable_reason, object_script_policy, object_script_unavailable_reason,
    object_scripts, prepare_object_action, truncate_traits_policy,
};

fn request(action: &str, driver: &str, object_id: &str) -> ffi::ObjectActionRequestDto {
    ffi::ObjectActionRequestDto {
        action: action.into(),
        driver: driver.into(),
        kind: "table".into(),
        object_id: object_id.into(),
        qualified_name: "\"public\".\"orders\"".into(),
        ..Default::default()
    }
}

#[test]
fn clone_returns_each_statement_and_the_joined_script() {
    let statement = prepare_object_action(ffi::ObjectActionRequestDto {
        new_name: "orders_copy".into(),
        with_data: true,
        ..request("clone", "mariadb", "[\"shop\",\"orders\"]")
    });
    assert!(statement.valid, "{}", statement.error);
    assert_eq!(
        statement.statements,
        [
            "CREATE TABLE `shop`.`orders_copy` LIKE `shop`.`orders`;",
            "INSERT INTO `shop`.`orders_copy` SELECT * FROM `shop`.`orders`;"
        ]
    );
    assert_eq!(statement.sql, statement.statements.join("\n"));
    assert_eq!(statement.new_qualified_name, "`shop`.`orders_copy`");
}

#[test]
fn truncate_options_follow_the_driver_traits() {
    let postgres = truncate_traits_policy("postgres");
    assert!(postgres.cascade && postgres.restart_identity);
    let sqlite = truncate_traits_policy("sqlite");
    assert!(!sqlite.cascade && !sqlite.restart_identity);
    let statement = prepare_object_action(ffi::ObjectActionRequestDto {
        cascade: true,
        ..request("truncate", "postgres", "pg:relation:9")
    });
    assert_eq!(
        statement.statements,
        ["TRUNCATE TABLE \"public\".\"orders\" CASCADE;"]
    );
    let rejected = prepare_object_action(ffi::ObjectActionRequestDto {
        cascade: true,
        ..request("truncate", "sqlite", "[\"main\",\"orders\"]")
    });
    assert!(!rejected.valid && rejected.statements.is_empty());
    assert_eq!(
        rejected.error,
        "This connection does not support CASCADE for truncate."
    );
}

#[test]
fn unknown_codes_and_unavailable_actions_return_reasons() {
    assert_eq!(
        prepare_object_action(request("purge", "sqlite", "[\"main\",\"orders\"]")).error,
        "Unknown object action purge."
    );
    assert_eq!(
        object_action_unavailable_reason("clone", "sqlite", "table", ""),
        "SQLite cannot clone a table with its keys and indexes."
    );
    assert_eq!(
        object_action_unavailable_reason("truncate", "mysql", "table", ""),
        ""
    );
    assert_eq!(
        object_script_unavailable_reason("truncate", "postgres", "view", ""),
        "Only tables support this action."
    );
    assert_eq!(
        object_script_unavailable_reason("explain", "postgres", "table", ""),
        "Unknown object script explain."
    );
}

#[test]
fn scripts_list_codes_and_build_sql_or_request_ddl() {
    let scripts = object_scripts();
    let codes: Vec<_> = scripts.iter().map(|script| script.code.as_str()).collect();
    assert_eq!(codes, ["create", "drop", "truncate", "select"]);
    assert_eq!(scripts[3].label, "SELECT 100 rows");
    let select = object_script_policy("select", "mysql", "view", "[\"shop\",\"recent\"]", "", "");
    assert!(select.valid && !select.from_ddl);
    assert_eq!(select.sql, "SELECT * FROM `shop`.`recent` LIMIT 100;");
    let create = object_script_policy(
        "create",
        "postgres",
        "table",
        "pg:relation:9",
        "\"public\".\"orders\"",
        "",
    );
    assert!(create.valid && create.from_ddl && create.sql.is_empty());
}
