use choscordb_bridge::{
    ffi, structure_change_kinds, structure_plan, structure_types, structure_unavailable_reason,
};

fn change(kind: &str, fields: &[(&str, &str)]) -> ffi::StructureChangeDto {
    ffi::StructureChangeDto {
        kind: kind.into(),
        keys: fields.iter().map(|(key, _)| (*key).into()).collect(),
        values: fields.iter().map(|(_, value)| (*value).into()).collect(),
    }
}

fn people() -> ffi::StructureTableDto {
    ffi::StructureTableDto {
        driver: "sqlite".into(),
        existing: true,
        qualified_name: "\"main\".\"people\"".into(),
        columns: vec![ffi::StructureColumnDto {
            name: "id".into(),
            data_type: "INTEGER".into(),
            nullable: false,
        }],
        ..Default::default()
    }
}

#[test]
fn change_kinds_describe_their_fields_and_the_driver_limits() {
    let kinds = structure_change_kinds("sqlite", false);
    let codes: Vec<_> = kinds.iter().map(|kind| kind.code.as_str()).collect();
    assert_eq!(
        codes,
        [
            "add_column",
            "change_column",
            "drop_column",
            "add_index",
            "drop_index",
            "primary_key",
            "add_foreign_key",
            "drop_key"
        ]
    );
    let primary = kinds
        .iter()
        .find(|kind| kind.code == "primary_key")
        .unwrap();
    assert_eq!(
        primary.unavailable,
        "SQLite needs a table rebuild for this change."
    );
    let change = kinds
        .iter()
        .find(|kind| kind.code == "change_column")
        .unwrap();
    let nullability = change
        .fields
        .iter()
        .find(|field| field.key == "nullability")
        .unwrap();
    assert_eq!(nullability.input, "choice");
    assert_eq!(nullability.names, "");
    assert_eq!(nullability.choices.len(), 3);
    assert!(
        structure_change_kinds("unknown", false)
            .iter()
            .all(|kind| kind.unavailable == "This connection cannot edit table structures.")
    );
    let postgres = structure_change_kinds("postgres", true);
    let drop = postgres
        .iter()
        .find(|kind| kind.code == "drop_column")
        .unwrap();
    assert!(!drop.unavailable.is_empty());
    assert!(
        postgres
            .iter()
            .find(|kind| kind.code == "primary_key")
            .unwrap()
            .unavailable
            .is_empty()
    );
    let panes: Vec<_> = kinds.iter().map(|kind| kind.pane).collect();
    assert_eq!(
        panes,
        [
            ffi::StructurePaneDto::Columns,
            ffi::StructurePaneDto::Columns,
            ffi::StructurePaneDto::Columns,
            ffi::StructurePaneDto::Indexes,
            ffi::StructurePaneDto::Indexes,
            ffi::StructurePaneDto::Keys,
            ffi::StructurePaneDto::Keys,
            ffi::StructurePaneDto::Keys
        ]
    );
    assert!(structure_types("mariadb").contains(&"VARCHAR(255)".to_string()));
    assert!(structure_types("oracle").is_empty());
    assert_eq!(
        structure_unavailable_reason("view", ""),
        "Only tables have a structure to edit."
    );
}

#[test]
fn plan_returns_statements_and_summaries_or_the_failed_change() {
    let plan = structure_plan(
        &people(),
        vec![
            change("add_column", &[("name", "email"), ("type", "TEXT")]),
            change(
                "add_index",
                &[("name", "people_email"), ("columns", "email")],
            ),
        ],
    );
    assert!(plan.valid, "{}", plan.error);
    assert_eq!(
        plan.summaries,
        ["Add column email", "Add index people_email"]
    );
    assert_eq!(
        plan.statements,
        [
            "ALTER TABLE \"main\".\"people\" ADD COLUMN \"email\" TEXT",
            "CREATE INDEX \"main\".\"people_email\" ON \"people\" (\"email\")",
        ]
    );
    let failed = structure_plan(&people(), vec![change("drop_column", &[("column", "age")])]);
    assert!(!failed.valid);
    assert_eq!(failed.error, "Change 1: The table has no column age.");
    assert_eq!(failed.summaries, ["Drop column age"]);
}

#[test]
fn a_new_table_takes_its_name_from_the_parent() {
    let table = ffi::StructureTableDto {
        driver: "postgres".into(),
        existing: false,
        parent_kind: "schema".into(),
        parent_qualified: "\"public\"".into(),
        table_name: "teams".into(),
        ..Default::default()
    };
    let plan = structure_plan(
        &table,
        vec![change("add_column", &[("name", "id"), ("type", "bigint")])],
    );
    assert_eq!(
        plan.statements,
        ["CREATE TABLE \"public\".\"teams\" (\n  \"id\" bigint\n)"]
    );
    let unnamed = ffi::StructureTableDto {
        table_name: String::new(),
        ..table
    };
    assert_eq!(
        structure_plan(&unnamed, vec![]).error,
        "Enter a name for the new table."
    );
}
