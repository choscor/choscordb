use async_trait::async_trait;
use choscordb_core::*;
use choscordb_driver_api::*;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

struct Driver {
    log: Arc<Mutex<Vec<String>>>,
}
struct Conn {
    log: Arc<Mutex<Vec<String>>>,
}
struct Cancel {
    log: Arc<Mutex<Vec<String>>>,
}
struct Cursor;
#[async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "fake"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, _: ConnectionOptions) -> Result<Box<dyn Connection>> {
        Ok(Box::new(Conn {
            log: self.log.clone(),
        }))
    }
}
#[async_trait]
impl CancelHandle for Cancel {
    async fn cancel(&self) -> Result<()> {
        tokio::time::sleep(Duration::from_millis(30)).await;
        self.log.lock().unwrap().push("cancel-settled".into());
        Ok(())
    }
}
#[async_trait]
impl Connection for Conn {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(Cancel {
            log: self.log.clone(),
        })
    }
    async fn execute(&mut self, sql: &str, _: QueryOptions) -> Result<Box<dyn ResultCursor>> {
        self.log.lock().unwrap().push(sql.into());
        if sql == "slow" {
            tokio::time::sleep(Duration::from_millis(80)).await;
        }
        Ok(Box::new(Cursor))
    }
    async fn load_metadata(&mut self, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        if parent.as_ref().is_some_and(|id| id.0 == "bad") {
            return Err(DriverError::new(ErrorKind::Query, "Metadata unavailable"));
        }
        Ok(vec![])
    }
    async fn commit(&mut self) -> Result<()> {
        self.log.lock().unwrap().push("commit".into());
        Ok(())
    }
    async fn rollback(&mut self) -> Result<()> {
        self.log.lock().unwrap().push("rollback".into());
        Ok(())
    }
    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}
#[async_trait]
impl ResultCursor for Cursor {
    fn columns(&self) -> &[Column] {
        &[]
    }
    async fn fetch_page(&mut self, _: PageSize) -> Result<ResultPage> {
        Ok(ResultPage {
            index: 0,
            rows: vec![],
            has_more: false,
        })
    }
    async fn load_value(&mut self, _: Handle) -> Result<Value> {
        Ok(Value::Text("deferred data".into()))
    }
    fn summary(&self) -> QuerySummary {
        QuerySummary::default()
    }
    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}
fn engine(config: EngineConfig) -> (Engine, Arc<Mutex<Vec<String>>>) {
    let log = Arc::new(Mutex::new(vec![]));
    (
        Engine::new(config, vec![Arc::new(Driver { log: log.clone() })]).unwrap(),
        log,
    )
}
fn options() -> ConnectionOptions {
    ConnectionOptions::Sqlite {
        path: ":memory:".into(),
        read_only: false,
    }
}
fn wait_for(engine: &mut Engine, predicate: impl Fn(&Event) -> bool) -> Event {
    let until = Instant::now() + Duration::from_secs(3);
    loop {
        if let Some(event) = engine.try_event()
            && predicate(&event)
        {
            return event;
        }
        assert!(Instant::now() < until, "event timed out");
        std::thread::sleep(Duration::from_millis(1));
    }
}
#[test]
fn query_pages_and_manual_transactions_are_sequential() {
    let (mut engine, log) = engine(EngineConfig::default());
    let c = engine.connect("fake", options()).unwrap();
    wait_for(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let q = engine
        .execute(c, "first".into(), QueryOptions::default())
        .unwrap();
    engine.fetch_page(q, PageSize::default()).unwrap();
    engine.commit(c).unwrap();
    engine.rollback(c).unwrap();
    wait_for(&mut engine, |e| {
        matches!(
            e,
            Event::TransactionFinished {
                committed: false,
                ..
            }
        )
    });
    assert_eq!(*log.lock().unwrap(), vec!["first", "commit", "rollback"]);
    engine.release_query(q).unwrap();
    assert_eq!(engine.cancel(q), Err(SubmitError::StaleHandle));
}
#[test]
fn cancellation_settles_before_following_query_and_other_connections_progress() {
    let (mut engine, log) = engine(EngineConfig::default());
    let c1 = engine.connect("fake", options()).unwrap();
    let c2 = engine.connect("fake", options()).unwrap();
    let q = engine
        .execute(c1, "slow".into(), QueryOptions::default())
        .unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::QueryState { query, state: QueryState::Running } if *query == q),
    );
    engine.cancel(q).unwrap();
    engine
        .execute(c1, "after".into(), QueryOptions::default())
        .unwrap();
    let other = engine
        .execute(c2, "other".into(), QueryOptions::default())
        .unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::Schema { query, .. } if *query == other),
    );
    wait_for(
        &mut engine,
        |e| matches!(e, Event::Schema { query, .. } if *query != other && *query != q),
    );
    let log = log.lock().unwrap();
    assert!(
        log.iter().position(|s| s == "cancel-settled").unwrap()
            < log.iter().position(|s| s == "after").unwrap()
    );
    assert!(
        log.iter().position(|s| s == "other").unwrap()
            < log.iter().position(|s| s == "after").unwrap()
    );
}

#[test]
fn query_timeout_is_not_retried_and_next_command_waits_for_settlement() {
    let (mut engine, log) = engine(EngineConfig::default());
    let c = engine.connect("fake", options()).unwrap();
    let q = engine
        .execute(
            c,
            "slow".into(),
            QueryOptions {
                timeout: Some(Duration::from_millis(5)),
                ..Default::default()
            },
        )
        .unwrap();
    engine
        .execute(c, "after".into(), QueryOptions::default())
        .unwrap();
    let event = wait_for(
        &mut engine,
        |e| matches!(e, Event::QueryFailed { query, .. } if *query == q),
    );
    assert!(matches!(event, Event::QueryFailed { error, .. } if error.kind == ErrorKind::Timeout));
    wait_for(
        &mut engine,
        |e| matches!(e, Event::Schema { query, .. } if *query != q),
    );
    let log = log.lock().unwrap();
    assert_eq!(log.iter().filter(|s| *s == "slow").count(), 1);
    assert!(
        log.iter().position(|s| s == "cancel-settled").unwrap()
            < log.iter().position(|s| s == "after").unwrap()
    );
}

#[test]
fn event_backpressure_bounds_commands_without_blocking_caller() {
    let (mut engine, _) = engine(EngineConfig {
        command_capacity: 1,
        event_capacity: 1,
        ..Default::default()
    });
    let c = engine.connect("fake", options()).unwrap();
    let started = Instant::now();
    let mut full = false;
    for _ in 0..100 {
        match engine.execute(c, "queued".into(), QueryOptions::default()) {
            Err(SubmitError::QueueFull) => {
                full = true;
                break;
            }
            Ok(_) => (),
            other => panic!("unexpected {other:?}"),
        }
    }
    assert!(full);
    assert!(started.elapsed() < Duration::from_millis(100));
    engine.initiate_shutdown();
    assert_eq!(engine.commit(c), Err(SubmitError::ShuttingDown));
    let started = Instant::now();
    drop(engine);
    assert!(started.elapsed() < Duration::from_millis(100));
}

#[test]
fn idle_cursor_can_be_cancelled_without_fetching_another_page() {
    let (mut engine, _) = engine(EngineConfig::default());
    let c = engine.connect("fake", options()).unwrap();
    let q = engine
        .execute(c, "first".into(), QueryOptions::default())
        .unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::Schema { query, .. } if *query == q),
    );
    engine.cancel(q).unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::QueryFailed { query, error } if *query == q && error.kind == ErrorKind::Cancelled),
    );
}

#[test]
fn disconnected_handles_are_released_and_generations_change() {
    let (mut engine, _) = engine(EngineConfig {
        max_connections: 1,
        max_queries: 1,
        ..Default::default()
    });
    let c = engine.connect("fake", options()).unwrap();
    let q = engine
        .execute(c, "first".into(), QueryOptions::default())
        .unwrap();
    engine.disconnect(c).unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::Disconnected { connection } if *connection == c),
    );
    assert_eq!(engine.cancel(q), Err(SubmitError::StaleHandle));
    let next = engine.connect("fake", options()).unwrap();
    assert_ne!(c, next);
    assert!(
        engine
            .execute(next, "new".into(), QueryOptions::default())
            .is_ok()
    );
}

#[test]
fn completed_cursor_keeps_deferred_values_until_released() {
    let (mut engine, _) = engine(EngineConfig::default());
    let c = engine.connect("fake", options()).unwrap();
    let q = engine
        .execute(c, "first".into(), QueryOptions::default())
        .unwrap();
    engine.fetch_page(q, PageSize::default()).unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::QueryFinished { query, .. } if *query == q),
    );
    let handle = Handle {
        slot: 1,
        generation: 2,
    };
    engine.load_value(q, handle).unwrap();
    let event = wait_for(
        &mut engine,
        |e| matches!(e, Event::Value { query, .. } if *query == q),
    );
    assert!(
        matches!(event, Event::Value { value: Value::Text(text), .. } if text == "deferred data")
    );
    engine.release_query(q).unwrap();
    assert_eq!(engine.load_value(q, handle), Err(SubmitError::StaleHandle));
}

#[test]
fn expired_query_does_not_begin_a_database_write() {
    let (mut engine, log) = engine(EngineConfig::default());
    let c = engine.connect("fake", options()).unwrap();
    let q = engine
        .execute(
            c,
            "must-not-run".into(),
            QueryOptions {
                timeout: Some(Duration::ZERO),
                ..Default::default()
            },
        )
        .unwrap();
    wait_for(
        &mut engine,
        |e| matches!(e, Event::QueryFailed { query, error } if *query == q && error.kind == ErrorKind::Timeout),
    );
    assert!(!log.lock().unwrap().iter().any(|sql| sql == "must-not-run"));
}

#[test]
fn metadata_refresh_and_sibling_results_preserve_request_identity_on_success_and_error() {
    let (mut engine, _) = engine(EngineConfig::default());
    let c = engine.connect("fake", options()).unwrap();
    for (parent, token) in [("table", 41), ("sibling", 7), ("table", 42), ("bad", 99)] {
        engine
            .load_metadata_request(c, Some(ObjectId(parent.into())), token)
            .unwrap();
    }
    let mut received = vec![];
    while received.len() < 4 {
        let event = wait_for(&mut engine, |event| {
            matches!(event, Event::Metadata { .. } | Event::MetadataFailed { .. })
        });
        match event {
            Event::Metadata {
                connection,
                parent,
                request_token,
                ..
            } => {
                assert_eq!(connection, c);
                received.push((parent.unwrap().0, request_token, false));
            }
            Event::MetadataFailed {
                connection,
                parent,
                request_token,
                error,
            } => {
                assert_eq!(connection, c);
                assert_eq!(error.kind, ErrorKind::Query);
                received.push((parent.unwrap().0, request_token, true));
            }
            _ => unreachable!(),
        }
    }
    assert_eq!(
        received,
        vec![
            ("table".into(), 41, false),
            ("sibling".into(), 7, false),
            ("table".into(), 42, false),
            ("bad".into(), 99, true)
        ]
    );
    engine.load_metadata(c, None).unwrap();
    wait_for(&mut engine, |event| {
        matches!(
            event,
            Event::Metadata {
                parent: None,
                request_token: 0,
                ..
            }
        )
    });
}
