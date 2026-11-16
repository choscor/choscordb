use choscordb_core::{PlanNode, explain_sql, parse_explain};
use choscordb_driver_api::{GridEditDriver, Value};

fn node(
    depth: u32,
    operation: &str,
    target: &str,
    cost: &str,
    rows: &str,
    detail: &str,
) -> PlanNode {
    PlanNode {
        depth,
        operation: operation.into(),
        target: target.into(),
        cost: cost.into(),
        rows: rows.into(),
        detail: detail.into(),
    }
}

#[test]
fn each_driver_wraps_the_statement_in_its_plan_syntax() {
    assert_eq!(
        explain_sql(GridEditDriver::Postgres, " SELECT 1; ").unwrap(),
        "EXPLAIN (FORMAT JSON) SELECT 1"
    );
    assert_eq!(
        explain_sql(GridEditDriver::Mysql, "SELECT 1").unwrap(),
        "EXPLAIN FORMAT=JSON SELECT 1"
    );
    assert_eq!(
        explain_sql(GridEditDriver::Sqlite, "select 1").unwrap(),
        "EXPLAIN QUERY PLAN select 1"
    );
    assert_eq!(
        explain_sql(GridEditDriver::Sqlite, " ;").unwrap_err(),
        "Place the cursor in a statement to explain."
    );
    assert_eq!(
        explain_sql(GridEditDriver::Postgres, "explain select 1").unwrap_err(),
        "The statement is already an EXPLAIN statement."
    );
}

#[test]
fn sqlite_plan_rows_become_a_tree_from_their_parent_ids() {
    let rows = vec![
        vec![
            Value::Integer(2),
            Value::Integer(0),
            Value::Integer(0),
            Value::Text("SCAN a".into()),
        ],
        vec![
            Value::Integer(5),
            Value::Integer(2),
            Value::Integer(0),
            Value::Text("SEARCH b USING INDEX b_a (a=?)".into()),
        ],
        vec![
            Value::Integer(9),
            Value::Integer(0),
            Value::Integer(0),
            Value::Text("USE TEMP B-TREE FOR ORDER BY".into()),
        ],
    ];
    assert_eq!(
        parse_explain(GridEditDriver::Sqlite, &rows).unwrap(),
        [
            node(0, "SCAN a", "", "", "", ""),
            node(1, "SEARCH b USING INDEX b_a (a=?)", "", "", "", ""),
            node(0, "USE TEMP B-TREE FOR ORDER BY", "", "", "", ""),
        ]
    );
}

#[test]
fn postgres_json_plans_show_cost_rows_and_conditions() {
    let json = r#"[{"Plan": {"Node Type": "Hash Join", "Join Type": "Inner",
        "Startup Cost": 1.5, "Total Cost": 30.25, "Plan Rows": 12,
        "Hash Cond": "(a.id = b.a_id)",
        "Plans": [
          {"Node Type": "Seq Scan", "Relation Name": "a", "Startup Cost": 0.0,
           "Total Cost": 10.0, "Plan Rows": 100, "Filter": "(x > 1)"},
          {"Node Type": "Index Scan", "Relation Name": "b", "Index Name": "b_pkey",
           "Startup Cost": 0.25, "Total Cost": 8.5, "Plan Rows": 1}
        ]}}]"#;
    assert_eq!(
        parse_explain(GridEditDriver::Postgres, &[vec![Value::Json(json.into())]]).unwrap(),
        [
            node(
                0,
                "Hash Join",
                "",
                "1.50..30.25",
                "12",
                "Join Type: Inner · Hash Cond: (a.id = b.a_id)"
            ),
            node(1, "Seq Scan", "a", "0.00..10.00", "100", "Filter: (x > 1)"),
            node(1, "Index Scan", "b using b_pkey", "0.25..8.50", "1", ""),
        ]
    );
}

#[test]
fn mysql_json_plans_show_blocks_tables_and_costs() {
    let json = r#"{"query_block": {"select_id": 1,
        "cost_info": {"query_cost": "2.45"},
        "nested_loop": [
          {"table": {"table_name": "a", "access_type": "ALL",
             "rows_examined_per_scan": 4, "cost_info": {"prefix_cost": "0.65"},
             "attached_condition": "(`a`.`x` > 1)"}},
          {"table": {"table_name": "b", "access_type": "ref", "key": "b_a",
             "rows_examined_per_scan": 1, "cost_info": {"prefix_cost": "2.45"}}}
        ]}}"#;
    assert_eq!(
        parse_explain(GridEditDriver::Mysql, &[vec![Value::Text(json.into())]]).unwrap(),
        [
            node(0, "query block", "", "2.45", "", ""),
            node(1, "nested loop", "", "", "", ""),
            node(
                2,
                "table (ALL)",
                "a",
                "0.65",
                "4",
                "Condition: (`a`.`x` > 1)"
            ),
            node(2, "table (ref)", "b using b_a", "2.45", "1", ""),
        ]
    );
}

#[test]
fn an_unreadable_plan_is_an_error() {
    assert_eq!(
        parse_explain(GridEditDriver::Postgres, &[vec![Value::Text("{".into())]]).unwrap_err(),
        "The database returned a plan that ChoscorDB cannot read."
    );
    assert_eq!(
        parse_explain(GridEditDriver::Mysql, &[]).unwrap_err(),
        "The database returned a plan that ChoscorDB cannot read."
    );
}

fn wait(
    engine: &mut choscordb_core::Engine,
    predicate: impl Fn(&choscordb_core::Event) -> bool,
) -> choscordb_core::Event {
    let until = std::time::Instant::now() + std::time::Duration::from_secs(10);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(std::time::Instant::now() < until, "event timed out");
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}

#[test]
fn the_engine_explains_a_statement_without_running_it() {
    use choscordb_core::{Engine, Event};
    let mut engine = Engine::new(
        Default::default(),
        vec![std::sync::Arc::new(choscordb_driver_sqlite::SqliteDriver)],
    )
    .unwrap();
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("db.sqlite");
    rusqlite::Connection::open(&path)
        .unwrap()
        .execute_batch(
            "CREATE TABLE t (a INTEGER); CREATE INDEX t_a ON t (a); INSERT INTO t VALUES (1);",
        )
        .unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: path.clone(),
                read_only: false,
            },
        )
        .unwrap();
    wait(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    engine
        .explain(
            connection,
            GridEditDriver::Sqlite,
            "DELETE FROM t WHERE a = 1",
            3,
        )
        .unwrap();
    let event = wait(&mut engine, |event| {
        matches!(
            event,
            Event::ExplainReady { .. } | Event::ExplainFailed { .. }
        )
    });
    let Event::ExplainReady {
        request_token: 3,
        nodes,
        ..
    } = event
    else {
        panic!("{event:?}");
    };
    assert!(
        nodes[0].operation.starts_with("SEARCH t USING INDEX t_a"),
        "{nodes:?}"
    );
    let remaining: i64 = rusqlite::Connection::open(&path)
        .unwrap()
        .query_row("SELECT count(*) FROM t", [], |row| row.get(0))
        .unwrap();
    assert_eq!(remaining, 1);
    engine
        .explain(
            connection,
            GridEditDriver::Sqlite,
            "SELECT * FROM missing",
            4,
        )
        .unwrap();
    let event = wait(&mut engine, |event| {
        matches!(
            event,
            Event::ExplainReady { .. } | Event::ExplainFailed { .. }
        )
    });
    assert!(
        matches!(&event, Event::ExplainFailed { request_token: 4, error, .. } if error.message.contains("no such table")),
        "{event:?}"
    );
    assert_eq!(
        engine.explain(connection, GridEditDriver::Sqlite, "  ", 5),
        Err("Place the cursor in a statement to explain.".into())
    );
}

struct PlanlessSqlite;

#[async_trait::async_trait]
impl choscordb_driver_api::DatabaseDriver for PlanlessSqlite {
    fn id(&self) -> &'static str {
        "sqlite"
    }
    fn capabilities(&self) -> choscordb_driver_api::DriverCapabilities {
        choscordb_driver_api::DriverCapabilities {
            explain_plans: false,
            ..choscordb_driver_sqlite::SqliteDriver.capabilities()
        }
    }
    async fn connect(
        &self,
        options: choscordb_driver_api::ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn choscordb_driver_api::Connection>> {
        choscordb_driver_sqlite::SqliteDriver.connect(options).await
    }
}

#[test]
fn a_driver_without_query_plans_reports_a_typed_unsupported_error() {
    use choscordb_core::{Engine, Event};
    let mut engine = Engine::new(
        Default::default(),
        vec![std::sync::Arc::new(PlanlessSqlite)],
    )
    .unwrap();
    let dir = tempfile::tempdir().unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: dir.path().join("db.sqlite"),
                read_only: false,
            },
        )
        .unwrap();
    wait(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    engine
        .explain(connection, GridEditDriver::Sqlite, "SELECT 1", 7)
        .unwrap();
    let event = wait(&mut engine, |event| {
        matches!(
            event,
            Event::ExplainReady { .. } | Event::ExplainFailed { .. }
        )
    });
    assert!(
        matches!(&event, Event::ExplainFailed { request_token: 7, error, .. }
            if error.kind == choscordb_driver_api::ErrorKind::Unsupported
                && error.message == "This database driver cannot show query plans."),
        "{event:?}"
    );
}
