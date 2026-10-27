//! Saved navigator children show at once on a reconnect, until the live children arrive.
use async_trait::async_trait;
use choscordb_core::{ConnectionProfile, Engine, Event, ProfileConfiguration};
use choscordb_driver_api::*;
use std::{
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

/// The children that the server gives now, or the error that it gives.
type Answer = std::result::Result<Vec<&'static str>, &'static str>;
/// The server state that the test controls.
#[derive(Clone)]
struct Server {
    answer: Arc<Mutex<Answer>>,
    /// The transaction state that the session reports after each statement.
    transaction: Arc<Mutex<Option<bool>>>,
}
impl Server {
    fn new(children: Vec<&'static str>) -> Self {
        Self {
            answer: Arc::new(Mutex::new(Ok(children))),
            transaction: Arc::new(Mutex::new(Some(false))),
        }
    }
    fn answer(&self, answer: Answer) {
        *self.answer.lock().unwrap() = answer;
    }
}
struct Driver(Server);
struct Session(Server);
struct NoCancel;
#[async_trait]
impl CancelHandle for NoCancel {
    async fn cancel(&self) -> Result<()> {
        Ok(())
    }
}
#[async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "sqlite"
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
        Arc::new(NoCancel)
    }
    async fn transaction_state(&mut self) -> Result<Option<bool>> {
        Ok(*self.0.transaction.lock().unwrap())
    }
    async fn execute_bounded(
        &mut self,
        _: &str,
        _: QueryOptions,
        _: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        Err(DriverError::new(ErrorKind::Unsupported, "no SQL"))
    }
    async fn load_metadata(&mut self, parent: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        let answer = self.0.answer.lock().unwrap().clone();
        let names = answer.map_err(|message| DriverError::new(ErrorKind::Connection, message))?;
        Ok(names
            .into_iter()
            .map(|name| SchemaObject {
                id: ObjectId(name.into()),
                parent: parent.clone(),
                name: name.into(),
                qualified_name: name.into(),
                kind: ObjectKind::Schema,
                has_children: true,
                column: None,
                // Properties are details that the cache does not keep.
                properties: vec![MetadataProperty::available("Owner", "admin")],
            })
            .collect())
    }
    async fn commit(&mut self) -> Result<()> {
        Ok(())
    }
    async fn rollback(&mut self) -> Result<()> {
        Ok(())
    }
    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}

fn profile() -> ConnectionProfile {
    ConnectionProfile {
        id: "saved".into(),
        name: "Saved".into(),
        group_id: None,
        configuration: ProfileConfiguration::Sqlite {
            path: ":memory:".into(),
            read_only: false,
        },
        credential_ref: None,
        proxy_credential_ref: None,
        ssh_jump_credential_refs: Default::default(),
        ssh_jump_private_key_refs: Default::default(),
        tls_credential_ref: None,
        ssh_credential_ref: None,
        ssh_private_key_ref: None,
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

/// What one root expand shows: each children event as (cached, names), or the error.
#[derive(Debug, PartialEq)]
enum Shown {
    Children(bool, Vec<String>),
    Failed(String),
}

fn connect(engine: &mut Engine) -> Handle {
    let connection = engine.connect_profile(profile(), None).unwrap();
    assert!(matches!(next(engine), Event::Connected { .. }));
    connection
}

fn expand(engine: &mut Engine, connection: Handle, refresh: bool) -> Vec<Shown> {
    if refresh {
        engine.refresh_metadata_page(connection, None, 7, 0, 100)
    } else {
        engine.load_metadata_page(connection, None, 7, 0, 100)
    }
    .unwrap();
    let mut shown = Vec::new();
    loop {
        match next(engine) {
            Event::Metadata {
                objects, cached, ..
            } => {
                assert!(objects.iter().all(|o| o.properties.is_empty() == cached));
                let names = objects.into_iter().map(|o| o.name).collect();
                shown.push(Shown::Children(cached, names));
                if !cached {
                    return shown;
                }
            }
            Event::MetadataFailed { error, .. } => {
                shown.push(Shown::Failed(error.message));
                return shown;
            }
            Event::OperationFailed { error, .. } => panic!("{}", error.message),
            _ => {}
        }
    }
}

fn reconnect(engine: &mut Engine, connection: Handle) -> Handle {
    engine.disconnect(connection).unwrap();
    while !matches!(next(engine), Event::Disconnected { .. }) {}
    connect(engine)
}

fn children(cached: bool, names: &[&str]) -> Shown {
    Shown::Children(cached, names.iter().map(|name| name.to_string()).collect())
}

#[test]
fn a_reconnect_shows_saved_children_first_and_then_the_live_children() {
    let server = Server::new(vec!["sales", "audit"]);
    let mut engine =
        Engine::new(Default::default(), vec![Arc::new(Driver(server.clone()))]).unwrap();
    let connection = connect(&mut engine);
    // Nothing is saved yet.
    assert_eq!(
        expand(&mut engine, connection, false),
        [children(false, &["sales", "audit"])]
    );
    let connection = reconnect(&mut engine, connection);
    server.answer(Ok(vec!["sales", "audit", "hr"]));
    assert_eq!(
        expand(&mut engine, connection, false),
        [
            children(true, &["sales", "audit"]),
            children(false, &["sales", "audit", "hr"])
        ]
    );
    // The live children replaced the saved ones.
    server.answer(Ok(vec!["hr"]));
    let connection = reconnect(&mut engine, connection);
    assert_eq!(
        expand(&mut engine, connection, false)[0],
        children(true, &["sales", "audit", "hr"])
    );
    // A manual refresh skips the saved children.
    assert_eq!(
        expand(&mut engine, connection, true),
        [children(false, &["hr"])]
    );
}

#[test]
fn a_failed_live_load_after_saved_children_reports_the_error() {
    let server = Server::new(vec!["sales"]);
    let mut engine =
        Engine::new(Default::default(), vec![Arc::new(Driver(server.clone()))]).unwrap();
    let connection = connect(&mut engine);
    expand(&mut engine, connection, false);
    let connection = reconnect(&mut engine, connection);
    server.answer(Err("server unreachable"));
    assert_eq!(
        expand(&mut engine, connection, false),
        [
            children(true, &["sales"]),
            Shown::Failed("server unreachable".into())
        ]
    );
}

#[test]
fn deleting_the_profile_clears_its_saved_children() {
    let server = Server::new(vec!["sales"]);
    let mut engine =
        Engine::new(Default::default(), vec![Arc::new(Driver(server.clone()))]).unwrap();
    let connection = connect(&mut engine);
    expand(&mut engine, connection, false);
    engine.disconnect(connection).unwrap();
    while !matches!(next(&mut engine), Event::Disconnected { .. }) {}
    engine.profile_delete("saved".into(), 9).unwrap();
    while !matches!(next(&mut engine), Event::ProfileDeleted { .. }) {}
    let connection = connect(&mut engine);
    assert_eq!(
        expand(&mut engine, connection, false),
        [children(false, &["sales"])]
    );
}

#[test]
fn an_object_inspection_reads_only_live_children() {
    let server = Server::new(vec!["sales"]);
    let mut engine =
        Engine::new(Default::default(), vec![Arc::new(Driver(server.clone()))]).unwrap();
    let connection = connect(&mut engine);
    expand(&mut engine, connection, false);
    let connection = reconnect(&mut engine, connection);
    engine.load_metadata_request(connection, None, 8).unwrap();
    match next(&mut engine) {
        Event::Metadata {
            objects, cached, ..
        } => {
            assert!(!cached, "an inspection must not show saved children");
            assert_eq!(objects[0].properties.len(), 1);
        }
        _ => panic!("expected the live children"),
    }
}

#[test]
fn children_that_a_transaction_shows_are_not_saved() {
    let server = Server::new(vec!["sales"]);
    let mut engine =
        Engine::new(Default::default(), vec![Arc::new(Driver(server.clone()))]).unwrap();
    // The transaction state is not known, so the children may hold uncommitted changes.
    *server.transaction.lock().unwrap() = None;
    let connection = connect(&mut engine);
    server.answer(Ok(vec!["sales", "uncommitted"]));
    expand(&mut engine, connection, false);
    let connection = reconnect(&mut engine, connection);
    assert_eq!(
        expand(&mut engine, connection, false),
        [children(false, &["sales", "uncommitted"])]
    );
}
