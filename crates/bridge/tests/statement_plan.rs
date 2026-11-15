use choscordb_bridge::sql_statement_plan;

#[test]
fn the_plan_uses_the_driver_dialect_and_reports_errors() {
    let sql = "-- name: First\nSELECT 'a\\';b'; SELECT 2;";
    let mysql = sql_statement_plan(sql, 0, sql.len() as u64, "mysql", "");
    assert!(mysql.valid);
    let labels: Vec<_> = mysql.statements.iter().map(|s| s.label.as_str()).collect();
    assert_eq!(labels, ["First", "Query 2"]);
    assert_eq!(
        &sql[..mysql.statements[0].end as usize],
        "-- name: First\nSELECT 'a\\';b';"
    );
    let postgres = sql_statement_plan(sql, 0, sql.len() as u64, "postgres", "");
    assert_eq!(
        &sql[..postgres.statements[0].end as usize],
        "-- name: First\nSELECT 'a\\';"
    );
    let empty = sql_statement_plan("-- note", 0, 7, "sqlite", "");
    assert!(!empty.valid);
    assert_eq!(empty.error, "No executable statement in the document.");
    let invalid = sql_statement_plan("SELECT 1;", 5, 2, "sqlite", "");
    assert!(!invalid.valid);
}
