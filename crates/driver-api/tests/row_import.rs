use choscordb_driver_api::{
    GridEditDriver, ImportColumn, ImportConflict, RowImportError, Value, create_import_table_sql,
    drop_import_table_sql, quote_import_identifier, row_import_sql, row_import_statement,
};

fn column(name: &str, database_type: &str, key: bool) -> ImportColumn {
    ImportColumn {
        name: name.into(),
        database_type: database_type.into(),
        key,
    }
}

fn columns() -> Vec<ImportColumn> {
    vec![column("id", "integer", true), column("name", "text", false)]
}

fn keys() -> Vec<String> {
    vec!["id".into()]
}

#[test]
fn stop_policy_writes_a_plain_insert_with_driver_placeholders() {
    assert_eq!(
        row_import_sql(
            GridEditDriver::Postgres,
            "\"public\".\"people\"",
            &columns(),
            &keys(),
            ImportConflict::Stop
        ),
        Ok("INSERT INTO \"public\".\"people\" (\"id\", \"name\") VALUES ($1, $2)".into())
    );
    assert_eq!(
        row_import_sql(
            GridEditDriver::Mysql,
            "`app`.`people`",
            &columns(),
            &keys(),
            ImportConflict::Stop
        ),
        Ok("INSERT INTO `app`.`people` (`id`, `name`) VALUES (?, ?)".into())
    );
}

#[test]
fn skip_policy_ignores_only_key_conflicts_for_each_driver() {
    let sql = |driver, name: &str| {
        row_import_sql(driver, name, &columns(), &keys(), ImportConflict::Skip).unwrap()
    };
    assert_eq!(
        sql(GridEditDriver::Sqlite, "\"main\".\"people\""),
        "INSERT INTO \"main\".\"people\" (\"id\", \"name\") VALUES (?, ?) ON CONFLICT DO NOTHING"
    );
    assert_eq!(
        sql(GridEditDriver::Mysql, "`people`"),
        "INSERT INTO `people` (`id`, `name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `id` = `id`"
    );
}

#[test]
fn update_policy_updates_non_key_columns_and_needs_every_key_column() {
    assert_eq!(
        row_import_sql(
            GridEditDriver::Postgres,
            "\"people\"",
            &columns(),
            &keys(),
            ImportConflict::Update
        ),
        Ok("INSERT INTO \"people\" (\"id\", \"name\") VALUES ($1, $2) ON CONFLICT (\"id\") DO UPDATE SET \"name\" = excluded.\"name\"".into())
    );
    assert_eq!(
        row_import_sql(
            GridEditDriver::Mysql,
            "`people`",
            &columns(),
            &keys(),
            ImportConflict::Update
        ),
        Ok("INSERT INTO `people` (`id`, `name`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `name` = VALUES(`name`)".into())
    );
    assert_eq!(
        row_import_sql(
            GridEditDriver::Sqlite,
            "\"people\"",
            &[column("name", "text", false)],
            &keys(),
            ImportConflict::Update
        ),
        Err(RowImportError::KeyNotMapped)
    );
    assert_eq!(
        row_import_sql(
            GridEditDriver::Sqlite,
            "\"people\"",
            &columns(),
            &[],
            ImportConflict::Update
        ),
        Err(RowImportError::KeyNotMapped)
    );
}

#[test]
fn replace_policy_is_not_available_for_postgres() {
    assert!(!ImportConflict::Replace.supported(GridEditDriver::Postgres));
    assert!(ImportConflict::Replace.supported(GridEditDriver::Sqlite));
    assert_eq!(
        row_import_sql(
            GridEditDriver::Postgres,
            "\"people\"",
            &columns(),
            &keys(),
            ImportConflict::Replace
        ),
        Err(RowImportError::UnsupportedConflict)
    );
    assert_eq!(
        row_import_sql(
            GridEditDriver::Sqlite,
            "\"people\"",
            &columns(),
            &keys(),
            ImportConflict::Replace
        ),
        Ok("REPLACE INTO \"people\" (\"id\", \"name\") VALUES (?, ?)".into())
    );
}

#[test]
fn import_sql_rejects_unquoted_targets_and_empty_columns() {
    assert_eq!(
        row_import_sql(
            GridEditDriver::Sqlite,
            "people; DROP TABLE x",
            &columns(),
            &[],
            ImportConflict::Stop
        ),
        Err(RowImportError::InvalidIdentifier)
    );
    assert_eq!(
        row_import_sql(
            GridEditDriver::Sqlite,
            "\"people\"",
            &[],
            &[],
            ImportConflict::Stop
        ),
        Err(RowImportError::NoColumns)
    );
}

#[test]
fn import_rows_bind_null_and_typed_text() {
    let statement = row_import_statement(
        "INSERT",
        &[
            column("amount", "numeric(10,2)", false),
            column("note", "text", false),
        ],
        vec![Some("1.50".into()), None],
    )
    .unwrap();
    assert!(matches!(&statement.params[0], Value::Decimal(text) if text == "1.50"));
    assert!(matches!(statement.params[1], Value::Null));
    assert_eq!(statement.expected_rows, None);
    assert_eq!(
        row_import_statement("INSERT", &columns(), vec![None]).err(),
        Some(RowImportError::RowShape)
    );
}

#[test]
fn new_table_sql_quotes_each_column_with_its_type() {
    assert_eq!(
        create_import_table_sql(
            GridEditDriver::Mysql,
            "`people`",
            &[column("first `name`", "TEXT", false)]
        ),
        Ok("CREATE TABLE `people` (`first ``name``` TEXT)".into())
    );
    assert_eq!(
        drop_import_table_sql(GridEditDriver::Postgres, "\"people\""),
        Ok("DROP TABLE \"people\"".into())
    );
    assert_eq!(
        quote_import_identifier(GridEditDriver::Postgres, ""),
        Err(RowImportError::InvalidIdentifier)
    );
}
