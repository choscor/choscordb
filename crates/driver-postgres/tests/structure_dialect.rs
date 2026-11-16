use choscordb_driver_api::{DatabaseDriver, StructureChangeKind};
use choscordb_driver_postgres::PostgresDriver;

#[test]
fn postgres_gives_its_types_and_alters_each_change_in_place() {
    let dialect = PostgresDriver.structure_dialect().unwrap();
    assert!(dialect.column_types().contains(&"jsonb"));
    assert!(dialect.column_types().contains(&"timestamptz"));
    assert!(!dialect.column_types().contains(&"DATETIME"));
    for kind in StructureChangeKind::ALL {
        assert_eq!(dialect.rebuild_reason(kind), None);
    }
    assert_eq!(dialect.column_alter_reason(), None);
    assert_eq!(dialect.column_comment_reason(), None);
}
