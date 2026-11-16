use choscordb_driver_api::{DatabaseDriver, StructureChangeKind};
use choscordb_driver_mysql::{MariadbDriver, MysqlDriver};

#[test]
fn mysql_and_mariadb_give_their_types_and_alter_each_change_in_place() {
    for driver in [
        &MysqlDriver::new() as &dyn DatabaseDriver,
        &MariadbDriver as &dyn DatabaseDriver,
    ] {
        let dialect = driver.structure_dialect().unwrap();
        assert!(dialect.column_types().contains(&"VARCHAR(255)"));
        assert!(dialect.column_types().contains(&"DATETIME"));
        assert!(!dialect.column_types().contains(&"jsonb"));
        for kind in StructureChangeKind::ALL {
            assert_eq!(dialect.rebuild_reason(kind), None);
        }
        assert_eq!(dialect.column_alter_reason(), None);
        assert_eq!(dialect.column_comment_reason(), None);
    }
}
