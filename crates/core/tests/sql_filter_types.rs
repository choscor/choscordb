use async_trait::async_trait;
use choscordb_core::{Event, FilterCondition, FilterOperator};
use choscordb_driver_api::*;
use std::sync::Arc;
struct TypedDriver(Vec<Vec<Value>>);
struct TypedConnection(Vec<Vec<Value>>);
struct TypedCursor(Vec<Vec<Value>>);
struct NoopCancel;
#[async_trait]
impl DatabaseDriver for TypedDriver {
    fn id(&self) -> &'static str {
        "typed-view"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(
        &self,
        _: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        Ok(Box::new(TypedConnection(self.0.clone())))
    }
}
#[async_trait]
impl CancelHandle for NoopCancel {
    async fn cancel(&self) -> choscordb_driver_api::Result<()> {
        Ok(())
    }
}
#[async_trait]
impl Connection for TypedConnection {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(NoopCancel)
    }
    async fn execute(
        &mut self,
        _: &str,
        _: QueryOptions,
    ) -> choscordb_driver_api::Result<Box<dyn ResultCursor>> {
        Ok(Box::new(TypedCursor(self.0.clone())))
    }
    async fn load_metadata(
        &mut self,
        _: Option<choscordb_driver_api::ObjectId>,
    ) -> choscordb_driver_api::Result<Vec<choscordb_driver_api::SchemaObject>> {
        Ok(vec![])
    }
    async fn commit(&mut self) -> choscordb_driver_api::Result<()> {
        Ok(())
    }
    async fn rollback(&mut self) -> choscordb_driver_api::Result<()> {
        Ok(())
    }
    async fn close(&mut self) -> choscordb_driver_api::Result<()> {
        Ok(())
    }
}
#[async_trait]
impl ResultCursor for TypedCursor {
    fn columns(&self) -> &[Column] {
        static COLUMNS: std::sync::LazyLock<Vec<Column>> = std::sync::LazyLock::new(|| {
            vec![
                Column {
                    name: "amount".into(),
                    database_type: "INTEGER".into(),
                    precision: None,
                    scale: None,
                    timezone: None,
                    nullable: Some(false),
                },
                Column {
                    name: "payload".into(),
                    database_type: "TEXT".into(),
                    precision: None,
                    scale: None,
                    timezone: None,
                    nullable: None,
                },
            ]
        });
        &COLUMNS
    }
    async fn fetch_page(&mut self, _: PageSize) -> choscordb_driver_api::Result<ResultPage> {
        Ok(ResultPage {
            index: 0,
            rows: std::mem::take(&mut self.0),
            has_more: false,
        })
    }

    fn summary(&self) -> QuerySummary {
        QuerySummary::default()
    }
    async fn close(&mut self) -> choscordb_driver_api::Result<()> {
        Ok(())
    }
}

fn event(engine: &mut choscordb_core::Engine, wanted: impl Fn(&Event) -> bool) -> Event {
    for _ in 0..1000 {
        if let Some(event) = engine.try_event() {
            if let Event::ResultViewFailed { error, .. } = &event {
                panic!("view failed: {:?}: {}", error.kind, error.message);
            }
            if let Event::QueryFailed { error, .. } = &event {
                panic!("query failed: {error}");
            }
            if wanted(&event) {
                return event;
            }
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    panic!("timed out waiting for event")
}

fn failed_view(engine: &mut choscordb_core::Engine) -> choscordb_driver_api::DriverError {
    for _ in 0..1000 {
        if let Some(Event::ResultViewFailed { error, .. }) = engine.try_event() {
            return error;
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    panic!("timed out waiting for result view failure")
}

fn setup(rows: Vec<Vec<Value>>) -> (choscordb_core::Engine, Handle) {
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(TypedDriver(rows))]).unwrap();
    let connection = engine
        .connect(
            "typed-view",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let query = engine
        .execute(connection, "fixture".into(), QueryOptions::default())
        .unwrap();
    event(&mut engine, |event| matches!(event, Event::Schema { .. }));
    (engine, query)
}
fn apply(engine: &mut choscordb_core::Engine, query: Handle, predicate: &str) {
    engine
        .apply_result_view(
            query,
            vec![FilterCondition {
                column: 0,
                operator: FilterOperator::Sql,
                value: Some(Value::Text(predicate.into())),
            }],
            None,
            PageSize::new(100).unwrap(),
        )
        .unwrap();
}
#[test]
fn sql_decimal_comparisons_are_numeric_and_reject_loss() {
    let (mut engine, query) = setup(vec![
        vec![Value::Decimal("2.5".into()), Value::Null],
        vec![Value::Decimal("20".into()), Value::Null],
    ]);
    for predicate in ["amount > 10", "amount = 2.5"] {
        apply(&mut engine, query, predicate);
        assert!(matches!(
            event(&mut engine, |event| matches!(
                event,
                Event::ResultViewApplied { .. }
            )),
            Event::ResultViewApplied { rows: 1, .. }
        ));
    }
    let (mut engine, query) = setup(vec![vec![
        Value::Decimal("9007199254740993.01".into()),
        Value::Null,
    ]]);
    apply(&mut engine, query, "amount > 10");
    assert!(failed_view(&mut engine).message.contains("precision"));
}
#[test]
fn sql_ignores_unreferenced_deferred_cells() {
    let deferred = Value::Deferred {
        handle: Handle {
            slot: 1,
            generation: 0,
        },
        byte_length: 100,
        database_type: "TEXT".into(),
    };
    let (mut engine, query) = setup(vec![vec![Value::Integer(2), deferred]]);
    apply(&mut engine, query, "amount = 2");
    assert!(matches!(
        event(&mut engine, |event| matches!(
            event,
            Event::ResultViewApplied { .. }
        )),
        Event::ResultViewApplied { rows: 1, .. }
    ));
    apply(&mut engine, query, "payload = 'x'");
    assert!(failed_view(&mut engine).message.contains("Deferred"));
}
#[test]
fn manual_patterns_and_lists_preserve_valid_empty_and_typed_literals() {
    use choscordb_core::value_matches;
    assert!(
        value_matches(
            &Value::Text(String::new()),
            FilterOperator::Like,
            Some(&Value::Text(String::new()))
        )
        .unwrap()
    );
    assert!(
        value_matches(
            &Value::Text("x".into()),
            FilterOperator::NotLike,
            Some(&Value::Text(String::new()))
        )
        .unwrap()
    );
    assert!(
        value_matches(
            &Value::Uuid("550e8400-e29b-41d4-a716-446655440000".into()),
            FilterOperator::In,
            Some(&Value::Text(
                "'550e8400-e29b-41d4-a716-446655440000'".into()
            ))
        )
        .unwrap()
    );
    assert!(
        value_matches(
            &Value::Binary(vec![0, 255]),
            FilterOperator::In,
            Some(&Value::Text("X'00ff', x'1234'".into()))
        )
        .unwrap()
    );
    assert!(
        !value_matches(
            &Value::Binary(vec![0, 254]),
            FilterOperator::In,
            Some(&Value::Text("X'00ff'".into()))
        )
        .unwrap()
    );
    assert!(
        value_matches(
            &Value::Binary(vec![]),
            FilterOperator::In,
            Some(&Value::Text("X'f'".into()))
        )
        .is_err()
    );
}
