use choscordb_driver_api::{
    DatabaseDriver, StructureChange, StructureChangeKind, StructureColumn, StructureTable,
    plan_structure,
};
use choscordb_driver_sqlite::SqliteDriver;

#[test]
fn sqlite_gives_its_types_and_the_changes_that_need_a_table_rebuild() {
    let dialect = SqliteDriver.structure_dialect().unwrap();
    assert_eq!(
        dialect.column_types(),
        ["INTEGER", "REAL", "TEXT", "BLOB", "NUMERIC"]
    );
    for kind in [
        StructureChangeKind::PrimaryKey,
        StructureChangeKind::AddForeignKey,
        StructureChangeKind::DropKey,
    ] {
        assert_eq!(
            dialect.rebuild_reason(kind),
            Some("SQLite needs a table rebuild for this change.")
        );
    }
    for kind in [
        StructureChangeKind::AddColumn,
        StructureChangeKind::ChangeColumn,
        StructureChangeKind::DropColumn,
        StructureChangeKind::AddIndex,
        StructureChangeKind::DropIndex,
    ] {
        assert_eq!(dialect.rebuild_reason(kind), None);
    }
    assert_eq!(
        dialect.column_alter_reason(),
        Some(
            "SQLite needs a table rebuild to change a column type, NULL rule, default or comment."
        )
    );
    assert_eq!(
        dialect.column_comment_reason(),
        Some("SQLite does not store column comments.")
    );
    assert_eq!(
        plan_structure(
            &people("\"main\".\"people\""),
            dialect,
            &[change(
                StructureChangeKind::ChangeColumn,
                &[("column", "name"), ("name", "label")]
            )]
        )
        .unwrap(),
        ["ALTER TABLE \"main\".\"people\" RENAME COLUMN \"name\" TO \"label\""]
    );
}

fn people(qualified_name: &str) -> StructureTable {
    StructureTable {
        qualified_name: qualified_name.into(),
        new_table: false,
        columns: vec![StructureColumn {
            name: "name".into(),
            data_type: "varchar(50)".into(),
            nullable: true,
        }],
        indexes: vec![],
        keys: vec![],
    }
}

fn change(kind: StructureChangeKind, values: &[(&str, &str)]) -> StructureChange {
    StructureChange {
        kind,
        values: values
            .iter()
            .map(|(key, value)| ((*key).to_owned(), vec![(*value).to_owned()]))
            .collect(),
    }
}
