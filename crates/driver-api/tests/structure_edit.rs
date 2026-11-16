use choscordb_driver_api::{
    GridEditDriver, StructureChange, StructureChangeKind, StructureColumn, StructureDialect,
    StructureKey, StructurePane, StructureTable, plan_structure, structure_change_summary,
};
use std::collections::BTreeMap;

fn change(kind: StructureChangeKind, values: &[(&str, &str)]) -> StructureChange {
    let mut map = BTreeMap::new();
    for (key, value) in values {
        map.entry((*key).to_owned())
            .or_insert_with(Vec::new)
            .push((*value).to_owned());
    }
    StructureChange { kind, values: map }
}

fn table(driver: GridEditDriver, qualified_name: &str) -> Case {
    Case {
        rules: Fake(driver),
        table: StructureTable {
            qualified_name: qualified_name.into(),
            new_table: false,
            columns: vec![
                StructureColumn {
                    name: "id".into(),
                    data_type: "integer".into(),
                    nullable: false,
                },
                StructureColumn {
                    name: "name".into(),
                    data_type: "varchar(50)".into(),
                    nullable: true,
                },
            ],
            indexes: vec!["people_name".into()],
            keys: vec![
                StructureKey {
                    name: "people_pkey".into(),
                    kind: "primarykey".into(),
                },
                StructureKey {
                    name: "people_team".into(),
                    kind: "foreignkey".into(),
                },
            ],
        },
    }
}

#[derive(Clone)]
struct Case {
    table: StructureTable,
    rules: Fake,
}

impl Case {
    fn with(&self, table: StructureTable) -> Case {
        Case {
            table,
            rules: self.rules,
        }
    }
}

#[derive(Clone, Copy)]
struct Fake(GridEditDriver);
impl StructureDialect for Fake {
    fn sql(&self) -> GridEditDriver {
        self.0
    }
    fn column_types(&self) -> &'static [&'static str] {
        &[]
    }
    fn rebuild_reason(&self, kind: StructureChangeKind) -> Option<&'static str> {
        (self.limited() && kind == StructureChangeKind::DropKey)
            .then_some("Limited rebuild reason.")
    }
    fn column_alter_reason(&self) -> Option<&'static str> {
        self.limited().then_some("Limited alter reason.")
    }
    fn column_comment_reason(&self) -> Option<&'static str> {
        self.limited().then_some("Limited comment reason.")
    }
}

impl Fake {
    fn limited(&self) -> bool {
        self.0 == GridEditDriver::Sqlite
    }
}

fn plan(case: &Case, changes: &[StructureChange]) -> Vec<String> {
    plan_structure(&case.table, &case.rules, changes).unwrap()
}

fn error(case: &Case, changes: &[StructureChange]) -> String {
    plan_structure(&case.table, &case.rules, changes)
        .unwrap_err()
        .to_string()
}

use StructureChangeKind as K;

#[test]
fn add_column_builds_the_definition_and_the_comment_for_each_driver() {
    let add = change(
        K::AddColumn,
        &[
            ("name", "email"),
            ("type", "varchar(255)"),
            ("not_null", "true"),
            ("default", "''"),
            ("comment", "It's mail"),
        ],
    );
    assert_eq!(
        plan(
            &table(GridEditDriver::Postgres, "\"public\".\"people\""),
            std::slice::from_ref(&add)
        ),
        [
            "ALTER TABLE \"public\".\"people\" ADD COLUMN \"email\" varchar(255) NOT NULL DEFAULT ''",
            "COMMENT ON COLUMN \"public\".\"people\".\"email\" IS 'It''s mail'",
        ]
    );
    assert_eq!(
        plan(
            &table(GridEditDriver::Mysql, "`app`.`people`"),
            std::slice::from_ref(&add)
        ),
        [
            "ALTER TABLE `app`.`people` ADD COLUMN `email` varchar(255) NOT NULL DEFAULT '' COMMENT 'It''s mail'"
        ]
    );
    assert_eq!(
        error(
            &table(GridEditDriver::Sqlite, "\"main\".\"people\""),
            &[add]
        ),
        "Change 1: Limited comment reason."
    );
}

#[test]
fn change_column_uses_separate_alters_on_postgres_and_modify_on_mysql() {
    let changed = change(
        K::ChangeColumn,
        &[
            ("column", "name"),
            ("name", "full_name"),
            ("nullability", "not_null"),
            ("drop_default", "true"),
        ],
    );
    assert_eq!(
        plan(
            &table(GridEditDriver::Postgres, "\"people\""),
            std::slice::from_ref(&changed)
        ),
        [
            "ALTER TABLE \"people\" ALTER COLUMN \"name\" SET NOT NULL",
            "ALTER TABLE \"people\" ALTER COLUMN \"name\" DROP DEFAULT",
            "ALTER TABLE \"people\" RENAME COLUMN \"name\" TO \"full_name\"",
        ]
    );
    assert_eq!(
        plan(&table(GridEditDriver::Mysql, "`people`"), &[changed]),
        [
            "ALTER TABLE `people` MODIFY COLUMN `name` varchar(50) NOT NULL",
            "ALTER TABLE `people` ALTER COLUMN `name` DROP DEFAULT",
            "ALTER TABLE `people` RENAME COLUMN `name` TO `full_name`",
        ]
    );
}

#[test]
fn a_limited_dialect_renames_and_drops_columns_and_names_its_reason_for_other_changes() {
    let sqlite = table(GridEditDriver::Sqlite, "\"main\".\"people\"");
    assert_eq!(
        plan(
            &sqlite,
            &[
                change(K::ChangeColumn, &[("column", "name"), ("name", "label")]),
                change(K::DropColumn, &[("column", "label")]),
            ]
        ),
        [
            "ALTER TABLE \"main\".\"people\" RENAME COLUMN \"name\" TO \"label\"",
            "ALTER TABLE \"main\".\"people\" DROP COLUMN \"label\"",
        ]
    );
    assert_eq!(
        error(
            &sqlite,
            &[change(
                K::ChangeColumn,
                &[("column", "name"), ("type", "TEXT")]
            )]
        ),
        "Change 1: Limited alter reason."
    );
    assert_eq!(
        error(&sqlite, &[change(K::DropKey, &[("key", "people_team")])]),
        "Change 1: Limited rebuild reason."
    );
    assert_eq!(
        error(
            &sqlite.with(StructureTable {
                new_table: true,
                ..sqlite.table.clone()
            }),
            &[change(K::DropKey, &[])]
        ),
        "Change 1: A new table has no columns, indexes or keys to change yet."
    );
}

#[test]
fn indexes_use_the_schema_of_the_table_on_sqlite_and_postgres() {
    let add = change(
        K::AddIndex,
        &[
            ("name", "people_id"),
            ("columns", "id"),
            ("columns", "name"),
            ("unique", "true"),
        ],
    );
    let drop = change(K::DropIndex, &[("index", "people_name")]);
    assert_eq!(
        plan(
            &table(GridEditDriver::Sqlite, "\"main\".\"people\""),
            &[add.clone(), drop.clone()]
        ),
        [
            "CREATE UNIQUE INDEX \"main\".\"people_id\" ON \"people\" (\"id\", \"name\")",
            "DROP INDEX \"main\".\"people_name\"",
        ]
    );
    assert_eq!(
        plan(
            &table(GridEditDriver::Postgres, "\"public\".\"people\""),
            std::slice::from_ref(&drop)
        ),
        ["DROP INDEX \"public\".\"people_name\""]
    );
    assert_eq!(
        plan(&table(GridEditDriver::Mysql, "`app`.`people`"), &[drop]),
        ["DROP INDEX `people_name` ON `app`.`people`"]
    );
}

#[test]
fn keys_add_and_drop_with_the_syntax_of_each_driver() {
    let foreign = change(
        K::AddForeignKey,
        &[
            ("name", "people_team"),
            ("columns", "id"),
            ("ref_table", "public.teams"),
            ("ref_columns", "id"),
            ("on_delete", "cascade"),
        ],
    );
    assert_eq!(
        plan(&table(GridEditDriver::Postgres, "\"people\""), &[foreign]),
        [
            "ALTER TABLE \"people\" ADD CONSTRAINT \"people_team\" FOREIGN KEY (\"id\") REFERENCES \"public\".\"teams\" (\"id\") ON DELETE CASCADE"
        ]
    );
    let drops = [
        change(K::DropKey, &[("key", "people_team")]),
        change(K::DropKey, &[("key", "people_pkey")]),
    ];
    assert_eq!(
        plan(&table(GridEditDriver::Mysql, "`people`"), &drops),
        [
            "ALTER TABLE `people` DROP FOREIGN KEY `people_team`",
            "ALTER TABLE `people` DROP PRIMARY KEY",
        ]
    );
    assert_eq!(
        plan(&table(GridEditDriver::Postgres, "\"people\""), &drops[..1]),
        ["ALTER TABLE \"people\" DROP CONSTRAINT \"people_team\""]
    );
}

#[test]
fn a_new_table_collects_columns_keys_and_indexes_into_create_statements() {
    let teams = table(GridEditDriver::Postgres, "\"public\".\"teams\"");
    let new_table = teams.with(StructureTable {
        new_table: true,
        columns: vec![],
        indexes: vec![],
        keys: vec![],
        ..teams.table.clone()
    });
    let changes = [
        change(
            K::AddColumn,
            &[("name", "id"), ("type", "bigint"), ("not_null", "true")],
        ),
        change(
            K::AddColumn,
            &[("name", "title"), ("type", "text"), ("comment", "Shown")],
        ),
        change(K::PrimaryKey, &[("columns", "id")]),
        change(
            K::AddIndex,
            &[("name", "teams_title"), ("columns", "title")],
        ),
    ];
    assert_eq!(
        plan(&new_table, &changes),
        [
            "CREATE TABLE \"public\".\"teams\" (\n  \"id\" bigint NOT NULL,\n  \"title\" text,\n  PRIMARY KEY (\"id\")\n)",
            "COMMENT ON COLUMN \"public\".\"teams\".\"title\" IS 'Shown'",
            "CREATE INDEX \"teams_title\" ON \"public\".\"teams\" (\"title\")",
        ]
    );
    assert_eq!(
        error(&new_table, &[change(K::DropColumn, &[("column", "id")])]),
        "Change 1: A new table has no columns, indexes or keys to change yet."
    );
    assert_eq!(
        error(
            &new_table,
            &[change(K::AddIndex, &[("name", "x"), ("columns", "id")])]
        ),
        "Change 1: The table has no column id."
    );
}

#[test]
fn changes_are_validated_before_any_sql_is_built() {
    let people = table(GridEditDriver::Postgres, "\"people\"");
    let cases = [
        (
            change(K::AddColumn, &[("name", "id"), ("type", "int")]),
            "The table already has a column id.",
        ),
        (
            change(
                K::AddColumn,
                &[("name", "x"), ("type", "int; DROP TABLE people")],
            ),
            "The type \"int; DROP TABLE people\" is not valid.",
        ),
        (
            change(
                K::AddColumn,
                &[
                    ("name", "x"),
                    ("type", "int"),
                    ("default", "1; DROP TABLE people"),
                ],
            ),
            "The default expression cannot contain ;, --, /*, # or a backslash. Its strings and parentheses must close. A $ must be in a string, and a comma in a string or parentheses.",
        ),
        (
            change(
                K::AddColumn,
                &[("name", "x"), ("type", "int, DROP COLUMN name")],
            ),
            "The type \"int, DROP COLUMN name\" is not valid.",
        ),
        (
            change(
                K::AddColumn,
                &[
                    ("name", "x"),
                    ("type", "int"),
                    ("default", "1, DROP COLUMN name"),
                ],
            ),
            "The default expression cannot contain ;, --, /*, # or a backslash. Its strings and parentheses must close. A $ must be in a string, and a comma in a string or parentheses.",
        ),
        (
            change(K::AddColumn, &[("type", "int")]),
            "Enter a value for Name.",
        ),
        (
            change(K::ChangeColumn, &[("column", "name")]),
            "Choose what to change in the column.",
        ),
        (
            change(K::DropColumn, &[("column", "age")]),
            "The table has no column age.",
        ),
        (
            change(
                K::AddForeignKey,
                &[("columns", "id"), ("ref_table", "teams")],
            ),
            "Enter one referenced column for each column.",
        ),
    ];
    for default in [
        "'\\'', DROP COLUMN name, ADD COLUMN z INT DEFAULT '\\''",
        "E'\\'', DROP COLUMN name, ADD COLUMN z INT DEFAULT E'\\''",
        "0 # hidden",
        "\"'\", DROP COLUMN name, ADD COLUMN z INT DEFAULT \"'\"",
        "$$'$$, DROP COLUMN name, ADD COLUMN z text DEFAULT $$'$$",
        "(1",
        "1)",
        "'open",
    ] {
        assert_eq!(
            error(
                &people,
                &[change(
                    K::AddColumn,
                    &[("name", "x"), ("type", "int"), ("default", default)],
                )]
            ),
            "Change 1: The default expression cannot contain ;, --, /*, # or a backslash. Its strings and parentheses must close. A $ must be in a string, and a comma in a string or parentheses.",
            "{default}"
        );
    }
    for (changed, message) in cases {
        assert_eq!(error(&people, &[changed]), format!("Change 1: {message}"));
    }
    assert_eq!(
        plan(
            &people,
            &[change(
                K::AddColumn,
                &[
                    ("name", "price"),
                    ("type", "numeric(10, 2)"),
                    ("default", "coalesce(NULL, 'a, b')"),
                ],
            )]
        ),
        [
            "ALTER TABLE \"people\" ADD COLUMN \"price\" numeric(10, 2) DEFAULT coalesce(NULL, 'a, b')"
        ]
    );
    assert_eq!(
        plan(
            &people,
            &[change(
                K::AddColumn,
                &[
                    ("name", "label"),
                    ("type", "text"),
                    ("default", "'$5, \"a\"'")
                ],
            )]
        ),
        ["ALTER TABLE \"people\" ADD COLUMN \"label\" text DEFAULT '$5, \"a\"'"]
    );
    assert_eq!(error(&people, &[]), "Add one or more changes.");
}

#[test]
fn summaries_name_the_change_and_its_subject() {
    assert_eq!(
        structure_change_summary(&change(K::AddColumn, &[("name", "email")])),
        "Add column email"
    );
    assert_eq!(
        structure_change_summary(&change(
            K::PrimaryKey,
            &[("columns", "id"), ("columns", "name")]
        )),
        "Set primary key id, name"
    );
}

#[test]
fn each_field_names_its_input_and_the_table_names_it_offers() {
    let inputs = |kind: StructureChangeKind| {
        kind.fields()
            .iter()
            .map(|field| (field.key, field.kind.input(), field.kind.names()))
            .collect::<Vec<_>>()
    };
    assert_eq!(
        inputs(K::DropColumn),
        [("column", "choice", "table_columns")]
    );
    assert_eq!(inputs(K::DropIndex), [("index", "choice", "table_indexes")]);
    assert_eq!(inputs(K::DropKey), [("key", "choice", "table_keys")]);
    assert_eq!(
        inputs(K::AddIndex),
        [
            ("name", "text", ""),
            ("columns", "text", ""),
            ("unique", "flag", "")
        ]
    );
    assert_eq!(inputs(K::AddColumn)[1], ("type", "editable_choice", ""));
    assert_eq!(inputs(K::ChangeColumn)[3], ("nullability", "choice", ""));
}

#[test]
fn each_change_kind_belongs_to_one_structure_pane() {
    let panes: Vec<_> = StructureChangeKind::ALL
        .into_iter()
        .map(|kind| (kind.code(), kind.pane()))
        .collect();
    assert_eq!(
        panes,
        [
            ("add_column", StructurePane::Columns),
            ("change_column", StructurePane::Columns),
            ("drop_column", StructurePane::Columns),
            ("add_index", StructurePane::Indexes),
            ("drop_index", StructurePane::Indexes),
            ("primary_key", StructurePane::Keys),
            ("add_foreign_key", StructurePane::Keys),
            ("drop_key", StructurePane::Keys),
        ]
    );
}

#[test]
fn a_mysql_comment_with_a_backslash_is_refused_because_the_sql_mode_changes_its_meaning() {
    let comment = change(
        K::AddColumn,
        &[("name", "path"), ("type", "text"), ("comment", "C:\\temp")],
    );
    assert_eq!(
        error(
            &table(GridEditDriver::Mysql, "`people`"),
            std::slice::from_ref(&comment)
        ),
        "Change 1: A MySQL column comment cannot contain a backslash."
    );
    assert_eq!(
        plan(&table(GridEditDriver::Postgres, "\"people\""), &[comment]),
        [
            "ALTER TABLE \"people\" ADD COLUMN \"path\" text",
            "COMMENT ON COLUMN \"people\".\"path\" IS 'C:\\temp'",
        ]
    );
}
