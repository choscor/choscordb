use choscordb_driver_api::{StructureChangeKind, StructureDialect};

pub struct MysqlStructure;
impl StructureDialect for MysqlStructure {
    fn column_types(&self) -> &'static [&'static str] {
        &[
            "INT",
            "BIGINT",
            "SMALLINT",
            "TINYINT",
            "DECIMAL(10,2)",
            "FLOAT",
            "DOUBLE",
            "BOOLEAN",
            "VARCHAR(255)",
            "TEXT",
            "LONGTEXT",
            "DATE",
            "TIME",
            "DATETIME",
            "TIMESTAMP",
            "JSON",
            "BLOB",
        ]
    }
    fn rebuild_reason(&self, _: StructureChangeKind) -> Option<&'static str> {
        None
    }
    fn column_alter_reason(&self) -> Option<&'static str> {
        None
    }
    fn column_comment_reason(&self) -> Option<&'static str> {
        None
    }
}
