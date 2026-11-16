use choscordb_driver_api::{GridEditDriver, StructureChangeKind, StructureDialect};

pub struct SqliteStructure;
impl StructureDialect for SqliteStructure {
    fn sql(&self) -> GridEditDriver {
        GridEditDriver::Sqlite
    }
    fn column_types(&self) -> &'static [&'static str] {
        &["INTEGER", "REAL", "TEXT", "BLOB", "NUMERIC"]
    }
    fn rebuild_reason(&self, kind: StructureChangeKind) -> Option<&'static str> {
        matches!(
            kind,
            StructureChangeKind::PrimaryKey
                | StructureChangeKind::AddForeignKey
                | StructureChangeKind::DropKey
        )
        .then_some("SQLite needs a table rebuild for this change.")
    }
    fn column_alter_reason(&self) -> Option<&'static str> {
        Some("SQLite needs a table rebuild to change a column type, NULL rule, default or comment.")
    }
    fn column_comment_reason(&self) -> Option<&'static str> {
        Some("SQLite does not store column comments.")
    }
}
