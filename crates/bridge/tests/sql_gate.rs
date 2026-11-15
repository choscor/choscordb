use choscordb_bridge::{ffi, sql_execution_gate, sql_execution_range};

fn gate(safety: &str, sql: &str) -> ffi::SqlGateDto {
    let range = sql_execution_range(sql, 0, 0, 0, ffi::ExecutionScope::All, "postgres", "");
    assert!(range.valid);
    sql_execution_gate(safety, range.reads_only, range.confirmation_required)
}

#[test]
fn safety_levels_decide_how_statements_run() {
    assert_eq!(gate("", "SELECT 1").decision, "run");
    assert_eq!(gate("", "DELETE FROM t").decision, "confirm");
    assert_eq!(gate("silent", "DELETE FROM t").decision, "run");
    let all = gate("confirm_all", "SELECT 1");
    assert_eq!(all.decision, "confirm");
    assert_eq!(
        all.message,
        "This connection asks for confirmation before each statement."
    );
    assert_eq!(gate("read_only", "SELECT 1").decision, "run");
    let rejected = gate("read_only", "INSERT INTO t VALUES (1)");
    assert_eq!(rejected.decision, "reject");
    assert!(rejected.message.contains("read-only"));
    let unknown = sql_execution_gate("loose", true, false);
    assert_eq!(unknown.decision, "reject");
    assert_eq!(unknown.message, "Unknown safety level");
}
