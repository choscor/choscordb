use choscordb_driver_api::{
    DatabaseDriver, StructureChange, StructureChangeKind, StructureColumn, StructureTable,
    plan_structure,
};
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
        assert_eq!(
            plan_structure(
                &people("`people`"),
                dialect,
                &[change(
                    StructureChangeKind::ChangeColumn,
                    &[("column", "name"), ("nullability", "not_null")]
                )]
            )
            .unwrap(),
            ["ALTER TABLE `people` MODIFY COLUMN `name` varchar(50) NOT NULL"]
        );
    }
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
