use async_trait::async_trait;
use choscordb_core::{Engine, Event};
use choscordb_driver_api::*;
use std::{
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

#[derive(Default)]
struct State {
    calls: Mutex<Vec<&'static str>>,
}
impl State {
    fn mark(&self, value: &'static str) {
        self.calls.lock().unwrap().push(value);
    }
    fn contains(&self, value: &str) -> bool {
        self.calls.lock().unwrap().contains(&value)
    }
}
struct PendingGuard(Arc<State>);
impl Drop for PendingGuard {
    fn drop(&mut self) {
        self.0.mark("operation dropped");
    }
}
struct Driver(Arc<State>);
struct Session(Arc<State>);
struct QueryCancel(Arc<State>);
#[async_trait]
impl CancelHandle for QueryCancel {
    async fn cancel(&self) -> Result<()> {
        self.0.mark("query cancelled");
        Ok(())
    }
}
#[async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "pending_metadata"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, _: ConnectionOptions) -> Result<Box<dyn Connection>> {
        Ok(Box::new(Session(self.0.clone())))
    }
}
#[async_trait]
impl Connection for Session {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(QueryCancel(self.0.clone()))
    }
    async fn execute(&mut self, _: &str, _: QueryOptions) -> Result<Box<dyn ResultCursor>> {
        panic!("metadata disconnect must not execute SQL")
    }
    async fn load_metadata(&mut self, _: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        self.0.mark("metadata");
        let _guard = PendingGuard(self.0.clone());
        std::future::pending().await
    }
    async fn object_ddl(&mut self, _: &ObjectId) -> Result<String> {
        self.0.mark("ddl");
        let _guard = PendingGuard(self.0.clone());
        std::future::pending().await
    }
    async fn commit(&mut self) -> Result<()> {
        panic!("unexpected commit")
    }
    async fn rollback(&mut self) -> Result<()> {
        panic!("close owns rollback")
    }
    async fn close(&mut self) -> Result<()> {
        assert!(
            self.0.contains("operation dropped"),
            "close borrowed an unfinished operation"
        );
        self.0.mark("close");
        Ok(())
    }
}
fn wait(mut condition: impl FnMut() -> bool) {
    let deadline = Instant::now() + Duration::from_secs(2);
    while !condition() {
        assert!(
            Instant::now() < deadline,
            "pending metadata prevented disconnection"
        );
        std::thread::sleep(Duration::from_millis(1));
    }
}
fn check(ddl: bool, shutdown: bool) {
    let state = Arc::new(State::default());
    let mut engine =
        Engine::new(Default::default(), vec![Arc::new(Driver(state.clone()))]).unwrap();
    let connection = engine
        .connect(
            "pending_metadata",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    wait(|| matches!(engine.try_event(), Some(Event::Connected { .. })));
    let object = ObjectId("selected".into());
    if ddl {
        engine.object_ddl(connection, object.clone()).unwrap();
    } else {
        engine
            .load_metadata_request(connection, Some(object.clone()), 71)
            .unwrap();
    }
    wait(|| state.contains(if ddl { "ddl" } else { "metadata" }));
    if shutdown {
        engine.initiate_shutdown();
    } else {
        engine.disconnect(connection).unwrap();
    }
    let mut failed = false;
    wait(|| {
        match engine.try_event() {
            Some(Event::Metadata { .. } | Event::Ddl { .. }) => {
                panic!("cancelled metadata reported success")
            }
            Some(Event::MetadataFailed {
                connection: id,
                parent,
                request_token,
                error,
            }) => {
                assert!(!ddl);
                assert_eq!(id, connection);
                assert_eq!(parent, Some(object.clone()));
                assert_eq!(request_token, 71);
                assert_eq!(error.kind, ErrorKind::Disconnected);
                failed = true;
            }
            Some(Event::OperationFailed {
                connection: id,
                error,
            }) => {
                assert!(ddl);
                assert_eq!(id, connection);
                assert_eq!(error.kind, ErrorKind::Disconnected);
                failed = true;
            }
            Some(Event::Disconnected { connection: id }) => {
                assert_eq!(id, connection);
                assert!(failed);
                return true;
            }
            _ => {}
        }
        false
    });
    assert_eq!(
        *state.calls.lock().unwrap(),
        vec![
            if ddl { "ddl" } else { "metadata" },
            "operation dropped",
            "close"
        ]
    );
}
#[test]
fn disconnect_interrupts_pending_metadata() {
    check(false, false);
}
#[test]
fn disconnect_interrupts_pending_ddl() {
    check(true, false);
}
#[test]
fn shutdown_interrupts_pending_metadata() {
    check(false, true);
}
#[test]
fn shutdown_interrupts_pending_ddl() {
    check(true, true);
}
