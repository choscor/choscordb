use choscordb_sql_language::{
    CreateTemplate, create_template, create_template_parent, create_template_unavailable,
};

#[test]
fn schema_parents_qualify_new_objects() {
    assert_eq!(
        create_template(CreateTemplate::View, "postgres", "schema", "\"sales\""),
        Ok("CREATE VIEW \"sales\".new_view AS\nSELECT 1 AS value;".into())
    );
    assert_eq!(
        create_template(CreateTemplate::Table, "mysql", "schema", "`shop`"),
        Ok("CREATE TABLE `shop`.new_table (\n  id BIGINT AUTO_INCREMENT PRIMARY KEY\n);".into())
    );
    assert_eq!(
        create_template(CreateTemplate::Table, "sqlite", "connection", ""),
        Ok("CREATE TABLE new_table (\n  id INTEGER PRIMARY KEY\n);".into())
    );
}

#[test]
fn routines_and_schemas_follow_each_dialect() {
    assert_eq!(
        create_template(CreateTemplate::Procedure, "mariadb", "schema", "`shop`"),
        Ok("CREATE PROCEDURE `shop`.new_procedure()\nSELECT 1;".into())
    );
    assert!(
        create_template(CreateTemplate::Function, "postgres", "database", "")
            .unwrap()
            .starts_with("CREATE FUNCTION new_function()\nRETURNS integer\nLANGUAGE sql")
    );
    assert_eq!(
        create_template(CreateTemplate::Schema, "mysql", "connection", ""),
        Ok("CREATE DATABASE new_database;".into())
    );
    assert_eq!(
        create_template(CreateTemplate::Schema, "postgres", "schema", "\"sales\""),
        Ok("CREATE SCHEMA new_schema;".into())
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
    assert_eq!(
        create_template(CreateTemplate::View, "postgres", "table", "\"t\""),
        Err("Select a connection, database or schema to create an object.")
    );
    assert_eq!(create_template_parent("schema"), Some(true));
    assert_eq!(create_template_parent("column"), None);
    assert_eq!(
        CreateTemplate::from_code("procedure"),
        Some(CreateTemplate::Procedure)
    );
}
