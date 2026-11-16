use choscordb_driver_api::{StructureChangeKind, StructureDialect};

pub struct PostgresStructure;
impl StructureDialect for PostgresStructure {
    fn column_types(&self) -> &'static [&'static str] {
        &[
            "integer",
            "bigint",
            "smallint",
            "numeric(10,2)",
            "real",
            "double precision",
            "boolean",
            "text",
            "varchar(255)",
            "date",
            "time",
            "timestamp",
            "timestamptz",
            "uuid",
            "json",
            "jsonb",
            "bytea",
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
