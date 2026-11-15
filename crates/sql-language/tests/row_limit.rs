use choscordb_sql_language::*;

fn limited(sql: &str, limit: u32) -> (String, Option<u32>) {
    let result = apply_row_limit(sql, limit, None);
    (result.sql, result.applied)
}

#[test]
fn appends_a_limit_to_a_select_without_one() {
    assert_eq!(
        limited("select * from t", 100),
        ("select * from t LIMIT 100".into(), Some(100))
    );
    assert_eq!(
        limited("SELECT 1; -- done", 300),
        ("SELECT 1 LIMIT 300; -- done".into(), Some(300))
    );
    assert_eq!(
        limited("with c as (select 1) select * from c union select 2", 5),
        (
            "with c as (select 1) select * from c union select 2 LIMIT 5".into(),
            Some(5)
        )
    );
}

#[test]
fn caps_a_larger_literal_limit_and_keeps_a_smaller_one() {
    assert_eq!(
        limited("select * from t limit 5000 offset 10", 1000),
        ("select * from t limit 1000 offset 10".into(), Some(1000))
    );
    assert_eq!(
        limited("select * from t limit 50", 1000),
        ("select * from t limit 50".into(), None)
    );
    let mysql = apply_row_limit(
        "select * from t limit 10, 900",
        100,
        Some(MysqlSqlMode::default()),
    );
    assert_eq!(
        (mysql.sql.as_str(), mysql.applied),
        ("select * from t limit 10, 100", Some(100))
    );
    assert_eq!(
        limited("select * from t limit $1", 10),
        ("select * from t limit $1".into(), None)
    );
}

#[test]
fn inserts_before_offset_and_locking_clauses() {
    assert_eq!(
        limited("select * from t offset 5", 10).0,
        "select * from t LIMIT 10 offset 5"
    );
    assert_eq!(
        limited("select * from t for update", 10).0,
        "select * from t LIMIT 10 for update"
    );
}

#[test]
fn leaves_writes_subquery_limits_and_multiple_statements_unchanged() {
    for sql in [
        "update t set a = 1",
        "with d as (delete from t returning *) select * from d",
        "select * into backup from t",
        "select * from t; select * from u",
        "select * from (select * from t limit 5) s fetch first 3 rows only",
        "explain select 1",
        "",
    ] {
        assert_eq!(limited(sql, 10), (sql.into(), None), "{sql}");
    }
    assert_eq!(
        limited("select * from (select * from t limit 5000) s", 10).0,
        "select * from (select * from t limit 5000) s LIMIT 10"
    );
    assert_eq!(limited("select 1", 0), ("select 1".into(), None));
}

#[test]
fn row_limit_choices_are_listed_by_rust() {
    assert_eq!(
        ROW_LIMIT_CHOICES,
        [0, 100, 300, 500, 1_000, 5_000, 10_000, 50_000, 100_000]
    );
}
