use choscordb_driver_api::ErrorKind;
use choscordb_sql_language::{
    CreateTemplate, create_template, create_template_parent, create_template_unavailable,
};

#[test]
fn schema_parents_qualify_new_objects() {
    assert_eq!(
        create_template(CreateTemplate::View, "postgres", "schema", "\"sales\"").unwrap(),
        "CREATE VIEW \"sales\".new_view AS\nSELECT 1 AS value;"
    );
    assert_eq!(
        create_template(CreateTemplate::Table, "mysql", "schema", "`shop`").unwrap(),
        "CREATE TABLE `shop`.new_table (\n  id BIGINT AUTO_INCREMENT PRIMARY KEY\n);"
    );
    assert_eq!(
        create_template(CreateTemplate::Table, "sqlite", "connection", "").unwrap(),
        "CREATE TABLE new_table (\n  id INTEGER PRIMARY KEY\n);"
    );
}

#[test]
fn routines_and_schemas_follow_each_dialect() {
    assert_eq!(
        create_template(CreateTemplate::Procedure, "mariadb", "schema", "`shop`").unwrap(),
        "CREATE PROCEDURE `shop`.new_procedure()\nSELECT 1;"
    );
    assert!(
        create_template(CreateTemplate::Function, "postgres", "database", "")
            .unwrap()
            .starts_with("CREATE FUNCTION new_function()\nRETURNS integer\nLANGUAGE sql")
    );
    assert_eq!(
        create_template(CreateTemplate::Schema, "mysql", "connection", "").unwrap(),
        "CREATE DATABASE new_database;"
    );
    assert_eq!(
        create_template(CreateTemplate::Schema, "postgres", "schema", "\"sales\"").unwrap(),
        "CREATE SCHEMA new_schema;"
    );
}

#[test]
fn unsupported_objects_and_parents_return_reasons() {
    assert_eq!(
        create_template_unavailable(CreateTemplate::Function, "sqlite"),
        Some("SQLite does not support this object type.")
    );
    assert_eq!(
        create_template_unavailable(CreateTemplate::View, "sqlite"),
        None
    );
    let parent = create_template(CreateTemplate::View, "postgres", "table", "\"t\"").unwrap_err();
    assert_eq!(parent.kind, ErrorKind::InvalidInput);
    assert_eq!(
        parent.message,
        "Select a connection, database or schema to create an object."
    );
    let sqlite = create_template(CreateTemplate::Function, "sqlite", "connection", "").unwrap_err();
    assert_eq!(sqlite.kind, ErrorKind::Unsupported);
    assert_eq!(sqlite.message, "SQLite does not support this object type.");
    assert_eq!(
        create_template(CreateTemplate::Table, "oracle", "connection", "")
            .unwrap_err()
            .kind,
        ErrorKind::Unsupported
    );
    assert_eq!(create_template_parent("schema"), Some(true));
    assert_eq!(create_template_parent("column"), None);
    assert_eq!(
        CreateTemplate::from_code("procedure"),
        Some(CreateTemplate::Procedure)
    );
}
