use choscordb_sql_language::*;

#[test]
fn session_modes_change_quote_boundaries() {
    let sql = r"SELECT 'a\'; SELECT 2;";
    let mode = MysqlSqlMode::from_sql_mode("STRICT_TRANS_TABLES,NO_BACKSLASH_ESCAPES");
    assert_eq!(statement_ranges_mysql(sql).len(), 1);
    let ranges = statement_ranges_mysql_with_mode(sql, mode);
    assert_eq!(ranges.len(), 2);
    assert_eq!(&sql[ranges[0].clone()], r"SELECT 'a\';");
    assert_eq!(
        &sql[execution_range_mysql_with_mode(sql, sql.len(), None, mode).unwrap()],
        "SELECT 2;"
    );
    let quoted = "SELECT \"a\\\"; SELECT 3;";
    assert_eq!(statement_ranges_mysql(quoted).len(), 1);
    assert_eq!(
        statement_ranges_mysql_with_mode(quoted, MysqlSqlMode::from_sql_mode("ANSI_QUOTES")).len(),
        2
    );
}

#[test]
fn mysql_metadata_commands_do_not_need_confirmation() {
    for sql in [
        "SHOW FULL TABLES FROM `inventory` LIKE 'item%'",
        "SHOW SESSION VARIABLES LIKE 'sql_mode'",
        "SHOW CREATE TABLE `inventory`.`items`",
        "DESCRIBE `inventory`.`items`",
        "SHOW INDEXES FROM `items` FROM `inventory`",
    ] {
        assert_eq!(classify_mysql(sql), Safety::Ordinary, "{sql}");
    }
    for sql in [
        "SHOW UNKNOWN THING",
        "SHOW TABLES; DELETE FROM items",
        "SHOW TABLES INTO OUTFILE '/tmp/x'",
        "SHOW TABLES /*!50000 DROP TABLE items */",
        "DESCRIBE items garbage ???",
    ] {
        assert_eq!(classify_mysql(sql), Safety::ConfirmationRequired, "{sql}");
    }
}

#[test]
fn mysql_does_not_treat_brackets_or_nested_comments_as_quoting() {
    assert_eq!(statement_ranges_mysql("SELECT [1; SELECT 2;").len(), 2);
    assert_eq!(
        statement_ranges_mysql("/* outer /* inner */ SELECT 1; SELECT 2;").len(),
        2
    );
}

#[test]
fn punctuation_only_sql_still_requires_confirmation() {
    assert_eq!(classify_mysql("???"), Safety::ConfirmationRequired);
    assert_eq!(classify("???"), Safety::ConfirmationRequired);
    assert_eq!(
        classify_mysql("SELECT 1; ???"),
        Safety::ConfirmationRequired
    );
}
