use choscordb_sql_language::{
    MAX_RESULT_TABS, MysqlSqlMode, StatementPlanError, statement_plan, statement_plan_mysql,
};

#[test]
fn each_statement_gets_a_numbered_or_named_label() {
    let sql = "SELECT 1;\n-- name: Active users\nSELECT 2;\n-- other note\n-- name:   Totals  \nSELECT 3;";
    let plan = statement_plan(sql, 0..sql.len()).unwrap();
    let labels: Vec<_> = plan
        .iter()
        .map(|statement| statement.label.as_str())
        .collect();
    assert_eq!(labels, ["Query 1", "Active users", "Totals"]);
    assert_eq!(&sql[plan[0].range.clone()], "SELECT 1;");
    assert!(sql[plan[2].range.clone()].ends_with("SELECT 3;"));
}

#[test]
fn a_name_after_code_does_not_label_the_statement() {
    let sql = "SELECT 1 -- name: late\n;";
    let plan = statement_plan(sql, 0..sql.len()).unwrap();
    assert_eq!(plan[0].label, "Query 1");
}

#[test]
fn the_plan_keeps_only_statements_inside_the_range() {
    let sql = "SELECT 1; SELECT 2; SELECT 3;";
    let plan = statement_plan(sql, 10..19).unwrap();
    assert_eq!(plan.len(), 1);
    assert_eq!(plan[0].label, "Query 1");
    assert_eq!(
        statement_plan(sql, 0..0).unwrap_err(),
        StatementPlanError::Empty
    );
}

#[test]
fn a_selection_is_planned_from_its_own_text() {
    let sql = "SELECT 1 AS a; SELECT 2 AS b;";
    let plan = statement_plan(sql, 0..13).unwrap();
    assert_eq!(plan.len(), 1);
    assert_eq!(plan[0].range, 0..13);
    let plan = statement_plan(sql, 7..sql.len()).unwrap();
    let texts: Vec<_> = plan
        .iter()
        .map(|statement| &sql[statement.range.clone()])
        .collect();
    assert_eq!(texts, ["1 AS a;", "SELECT 2 AS b;"]);
    assert_eq!(
        statement_plan("SELECT 'é';", 0..9).unwrap_err(),
        StatementPlanError::Empty
    );
}

#[test]
fn mysql_mode_and_the_tab_limit_apply() {
    let sql = "SELECT 'a\\';b'; SELECT 2;";
    assert_eq!(
        statement_plan_mysql(sql, 0..sql.len(), MysqlSqlMode::default())
            .unwrap()
            .len(),
        2
    );
    let many = "SELECT 1;".repeat(MAX_RESULT_TABS + 1);
    let error = statement_plan(&many, 0..many.len()).unwrap_err();
    assert_eq!(error, StatementPlanError::TooMany);
    assert_eq!(
        error.to_string(),
        "Run all can show up to 64 results. To run a longer script, use File > Run SQL file."
    );
}
