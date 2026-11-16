use choscordb_core::{TriggerScript, TriggerScriptRequest, trigger_script};
use choscordb_driver_api::ErrorKind;

fn request(driver: &str, table_id: &str, table_name: &str) -> TriggerScriptRequest {
    TriggerScriptRequest {
        driver: driver.into(),
        table_id: table_id.into(),
        table_name: table_name.into(),
        trigger: "audit\"x".into(),
        definition:
            "CREATE TRIGGER \"main\".\"audit\" AFTER INSERT ON orders BEGIN SELECT 1; END;\n".into(),
    }
}

#[test]
fn drop_statements_name_the_trigger_for_each_dialect() {
    let postgres = request("postgres", "pg:relation:42", "\"sales\".\"orders\"");
    assert_eq!(
        trigger_script(TriggerScript::Drop, &postgres).unwrap(),
        "DROP TRIGGER \"audit\"\"x\" ON \"sales\".\"orders\";"
    );
    let mysql = TriggerScriptRequest {
        trigger: "audit`x".into(),
        ..request("mariadb", r#"["shop","orders"]"#, "`shop`.`orders`")
    };
    assert_eq!(
        trigger_script(TriggerScript::Drop, &mysql).unwrap(),
        "DROP TRIGGER `shop`.`audit``x`;"
    );
    let sqlite = request("sqlite", r#"["main","orders"]"#, "main.orders");
    assert_eq!(
        trigger_script(TriggerScript::Drop, &sqlite).unwrap(),
        "DROP TRIGGER \"main\".\"audit\"\"x\";"
    );
}

#[test]
fn edit_drops_the_trigger_and_recreates_it_from_its_definition() {
    let sqlite = request("sqlite", r#"["main","orders"]"#, "main.orders");
    assert_eq!(
        trigger_script(TriggerScript::Edit, &sqlite).unwrap(),
        "DROP TRIGGER \"main\".\"audit\"\"x\";\n\nCREATE TRIGGER \"main\".\"audit\" AFTER INSERT ON orders BEGIN SELECT 1; END;"
    );
    let attached = request("sqlite", r#"["aux","orders"]"#, "aux.orders");
    let unqualified = trigger_script(TriggerScript::Edit, &attached).unwrap_err();
    assert_eq!(unqualified.kind, ErrorKind::InvalidInput);
    assert_eq!(
        unqualified.message,
        "The trigger definition does not name the schema of its table."
    );
    let missing = TriggerScriptRequest {
        definition: " ".into(),
        ..sqlite.clone()
    };
    assert_eq!(
        trigger_script(TriggerScript::Edit, &missing)
            .unwrap_err()
            .message,
        "The trigger definition is unavailable."
    );
    let unnamed = TriggerScriptRequest {
        trigger: String::new(),
        ..sqlite
    };
    assert_eq!(
        trigger_script(TriggerScript::Drop, &unnamed)
            .unwrap_err()
            .message,
        "Choose a trigger."
    );
}

#[test]
fn create_opens_a_template_for_the_table() {
    let sqlite = request("sqlite", r#"["main","orders"]"#, "main.orders");
    assert_eq!(
        trigger_script(TriggerScript::Create, &sqlite).unwrap(),
        "CREATE TRIGGER \"main\".\"orders_trigger\" AFTER INSERT ON \"orders\"\nFOR EACH ROW\nBEGIN\n  SELECT NEW.rowid;\nEND;"
    );
    let postgres = request("postgres", "pg:relation:42", "\"sales\".\"orders\"");
    let template = trigger_script(TriggerScript::Create, &postgres).unwrap();
    assert!(
        template.starts_with(
            "CREATE OR REPLACE FUNCTION \"sales\".\"orders_trigger\"() RETURNS trigger"
        )
    );
    assert!(template.ends_with(
        "CREATE TRIGGER \"orders_trigger\" BEFORE INSERT OR UPDATE ON \"sales\".\"orders\"\nFOR EACH ROW EXECUTE FUNCTION \"sales\".\"orders_trigger\"();"
    ));
    let mysql = request("mysql", r#"["shop","orders"]"#, "`shop`.`orders`");
    assert!(
        trigger_script(TriggerScript::Create, &mysql)
            .unwrap()
            .starts_with("CREATE TRIGGER `shop`.`orders_trigger` BEFORE INSERT ON `shop`.`orders`")
    );
}

#[test]
fn invalid_tables_and_drivers_return_a_typed_error() {
    assert_eq!(
        trigger_script(TriggerScript::Create, &request("oracle", "x", "y"))
            .unwrap_err()
            .kind,
        ErrorKind::Unsupported
    );
    assert_eq!(
        trigger_script(
            TriggerScript::Create,
            &request("postgres", r#"["main","orders"]"#, "orders")
        )
        .unwrap_err()
        .kind,
        ErrorKind::InvalidInput
    );
    assert_eq!(
        trigger_script(
            TriggerScript::Create,
            &request("postgres", r#"["main","orders"]"#, "orders")
        )
        .unwrap_err()
        .message,
        "The selected table has an invalid identity."
    );
    assert_eq!(
        trigger_script(TriggerScript::Create, &request("oracle", "x", "y"))
            .unwrap_err()
            .message,
        "This connection cannot manage triggers."
    );
    assert_eq!(TriggerScript::from_code("edit"), Some(TriggerScript::Edit));
    assert_eq!(TriggerScript::Drop.label(), "Drop trigger");
    assert!(!TriggerScript::Create.needs_trigger());
}
