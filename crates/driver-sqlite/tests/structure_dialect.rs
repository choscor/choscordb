use choscordb_driver_api::{DatabaseDriver, StructureChangeKind};
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
}
