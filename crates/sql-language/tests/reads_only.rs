use choscordb_sql_language::{MysqlSqlMode, reads_only};

#[test]
fn queries_and_transaction_control_read_only() {
    for sql in [
        "SELECT * FROM t",
        "with x as (select 1) select * from x;",
        "SHOW TABLES",
        "EXPLAIN SELECT 1",
        "DESCRIBE t",
        "VALUES (1), (2)",
        "BEGIN; SELECT 1; COMMIT;",
        "-- note\nSELECT 'DELETE FROM t'",
        "SELECT \"update\" FROM t",
    ] {
        assert!(reads_only(sql, None), "{sql}");
    }
}

#[test]
fn writes_and_unknown_statements_are_not_read_only() {
    for sql in [
        "INSERT INTO t VALUES (1)",
        "update t set a = 1",
        "DELETE FROM t",
        "WITH x AS (DELETE FROM t RETURNING *) SELECT * FROM x",
        "SELECT * INTO copy FROM t",
        "SELECT * FROM t FOR UPDATE",
        "CREATE TABLE t (a int)",
        "DROP TABLE t",
        "SELECT 1; DELETE FROM t",
        "PRAGMA journal_mode = WAL",
        "SET search_path = x",
        "CALL refresh()",
        "EXPLAIN ANALYZE DELETE FROM t",
        "SELECT 'unterminated",
        "",
    ] {
        assert!(!reads_only(sql, None), "{sql}");
    }
}

#[test]
fn mysql_comments_and_quotes_follow_the_mode() {
    let mode = MysqlSqlMode::default();
    assert!(reads_only("# note\nSELECT `delete` FROM t", Some(mode)));
    assert!(!reads_only("# note\nDELETE FROM t", Some(mode)));
}
