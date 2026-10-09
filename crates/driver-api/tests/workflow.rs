use choscordb_driver_api::driver_workflow;

#[test]
fn mysql_sequences_catalog_work_after_results_and_refreshes_sql_mode() {
    let mysql = driver_workflow("mysql");
    assert!(mysql.inspect_after_result && mysql.sql_mode_before_execution);
    for driver in ["sqlite", "postgres", ""] {
        let workflow = driver_workflow(driver);
        assert!(
            !workflow.inspect_after_result && !workflow.sql_mode_before_execution,
            "{driver}"
        );
    }
}
