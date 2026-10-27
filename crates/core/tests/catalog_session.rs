//! Catalog reads use a second session of the Connect when the driver can open one.
use async_trait::async_trait;
use choscordb_core::{Engine, Event};
use choscordb_driver_api::*;
use std::{
    sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    },
    time::{Duration, Instant},
};

/// Counts the catalog sessions that the driver opens and closes.
#[derive(Default)]
struct Opens {
    count: AtomicUsize,
    closed: AtomicUsize,
    fail: bool,
    /// The transaction state of the query session: 0 none, 1 open, 2 unreadable.
    transaction: AtomicUsize,
}
struct Driver(Arc<Opens>);
struct Session {
    name: &'static str,
    opens: Arc<Opens>,
}
struct Opener(Arc<Opens>);
struct NoCancel;
#[async_trait]
impl CancelHandle for NoCancel {
    async fn cancel(&self) -> Result<()> {
        Ok(())
    }
}
#[async_trait]
impl SessionOpener for Opener {
    async fn open(&self) -> Result<Box<dyn Connection>> {
        self.0.count.fetch_add(1, Ordering::SeqCst);
        if self.0.fail {
            return Err(DriverError::new(
                ErrorKind::Connection,
                "catalog session refused",
            ));
        }
        Ok(Box::new(Session {
            name: "catalog",
            opens: self.0.clone(),
        }))
    }
}
#[async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "catalog_stub"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, _: ConnectionOptions) -> Result<Box<dyn Connection>> {
        Ok(Box::new(Session {
            name: "query",
            opens: self.0.clone(),
        }))
    }
}
#[async_trait]
impl Connection for Session {
    fn sibling_sessions(&self) -> Option<Arc<dyn SessionOpener>> {
        Some(Arc::new(Opener(self.opens.clone())))
    }
    async fn transaction_state(&mut self) -> Result<Option<bool>> {
        match self.opens.transaction.load(Ordering::SeqCst) {
            0 => Ok(Some(false)),
            1 => Ok(Some(true)),
            _ => Err(DriverError::new(ErrorKind::Connection, "state unreadable")),
        }
    }
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(NoCancel)
    }
    async fn execute_bounded(
        &mut self,
        _: &str,
        _: QueryOptions,
        _: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        Err(DriverError::new(ErrorKind::Unsupported, "no SQL"))
    }
    /// Names the session that answered.
    async fn load_metadata(&mut self, _: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        Ok(vec![SchemaObject {
            id: ObjectId(self.name.into()),
            parent: None,
            name: self.name.into(),
            qualified_name: self.name.into(),
            kind: ObjectKind::Schema,
            has_children: false,
            column: None,
            properties: Vec::new(),
        }])
    }
    async fn commit(&mut self) -> Result<()> {
        Ok(())
    }
    async fn rollback(&mut self) -> Result<()> {
        Ok(())
    }
    async fn close(&mut self) -> Result<()> {
        if self.name == "catalog" {
            self.opens.closed.fetch_add(1, Ordering::SeqCst);
        }
        Ok(())
    }
}

fn next(engine: &mut Engine) -> Event {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(event) = engine.try_event() {
            return event;
        }
        assert!(Instant::now() < deadline, "timed out waiting for an event");
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn connected(opens: Arc<Opens>) -> (Engine, Handle) {
    let mut engine = Engine::new(Default::default(), vec![Arc::new(Driver(opens))]).unwrap();
    let options = ConnectionOptions::Sqlite {
        path: ":memory:".into(),
        read_only: false,
    };
    let connection = engine.connect("catalog_stub", options).unwrap();
    assert!(matches!(next(&mut engine), Event::Connected { .. }));
    (engine, connection)
}

fn answered_by(engine: &mut Engine, connection: Handle) -> String {
    engine.load_metadata(connection, None).unwrap();
    loop {
        match next(engine) {
            Event::Metadata { objects, .. } => return objects[0].name.clone(),
            Event::OperationFailed { error, .. } => panic!("{}", error.message),
            _ => {}
        }
    }
}

#[test]
fn the_catalog_session_opens_at_the_first_read_and_answers_each_read() {
    let opens = Arc::new(Opens::default());
    let (mut engine, connection) = connected(opens.clone());
    assert_eq!(opens.count.load(Ordering::SeqCst), 0);
    assert_eq!(answered_by(&mut engine, connection), "catalog");
    assert_eq!(answered_by(&mut engine, connection), "catalog");
    assert_eq!(opens.count.load(Ordering::SeqCst), 1);
}

#[test]
fn a_catalog_session_that_cannot_open_shows_the_error_and_the_query_session_answers() {
    let opens = Arc::new(Opens {
        fail: true,
        ..Default::default()
    });
    let (mut engine, connection) = connected(opens.clone());
    engine.load_metadata(connection, None).unwrap();
    match next(&mut engine) {
        Event::OperationFailed { error, .. } => {
            assert_eq!(error.message, "catalog session refused")
        }
        _ => panic!("the open failure must show before the answer"),
    }
    match next(&mut engine) {
        Event::Metadata { objects, .. } => assert_eq!(objects[0].name, "query"),
        _ => panic!("the query session must answer the read"),
    }
}

/// Runs one statement, so the engine reads the transaction state again.
fn statement(engine: &mut Engine, connection: Handle) {
    engine
        .execute(connection, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    while !matches!(next(engine), Event::QueryFailed { .. }) {}
}

#[test]
fn the_query_session_answers_while_its_transaction_is_open() {
    let opens = Arc::new(Opens::default());
    let (mut engine, connection) = connected(opens.clone());
    assert_eq!(answered_by(&mut engine, connection), "catalog");
    opens.transaction.store(1, Ordering::SeqCst);
    statement(&mut engine, connection);
    assert_eq!(answered_by(&mut engine, connection), "query");
    opens.transaction.store(0, Ordering::SeqCst);
    statement(&mut engine, connection);
    assert_eq!(answered_by(&mut engine, connection), "catalog");
}

#[test]
fn an_unreadable_transaction_state_shows_the_error_and_the_query_session_answers() {
    let opens = Arc::new(Opens::default());
    let (mut engine, connection) = connected(opens.clone());
    opens.transaction.store(2, Ordering::SeqCst);
    engine
        .execute(connection, "SELECT 1".into(), QueryOptions::default())
        .unwrap();
    let shown = loop {
        if let Event::OperationFailed { error, .. } = next(&mut engine) {
            break error.message;
        }
    };
    assert_eq!(shown, "state unreadable");
    assert_eq!(answered_by(&mut engine, connection), "query");
}

#[test]
fn a_failed_catalog_session_shows_one_error_for_a_burst_of_reads() {
    let opens = Arc::new(Opens {
        fail: true,
        ..Default::default()
    });
    let (mut engine, connection) = connected(opens.clone());
    let mut errors = 0;
    for _ in 0..3 {
        engine.load_metadata(connection, None).unwrap();
        loop {
            match next(&mut engine) {
                Event::OperationFailed { .. } => errors += 1,
                Event::Metadata { objects, .. } => {
                    assert_eq!(objects[0].name, "query");
                    break;
                }
                _ => {}
            }
        }
    }
    assert_eq!(errors, 1);
    assert_eq!(opens.count.load(Ordering::SeqCst), 1);
}

#[test]
fn the_catalog_session_closes_on_disconnect() {
    let opens = Arc::new(Opens::default());
    let (mut engine, connection) = connected(opens.clone());
    answered_by(&mut engine, connection);
    engine.disconnect(connection).unwrap();
    while !matches!(next(&mut engine), Event::Disconnected { .. }) {}
    let deadline = Instant::now() + Duration::from_secs(5);
    while opens.closed.load(Ordering::SeqCst) == 0 {
        assert!(Instant::now() < deadline, "the catalog session stayed open");
        std::thread::sleep(Duration::from_millis(1));
    }
}
