use async_trait::async_trait;
use choscordb_core::{Engine, EngineConfig, Event, QueryState};
use choscordb_driver_api::*;
use serde_json::{Value as Json, json};
use std::collections::{HashMap, VecDeque};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

#[derive(Default)]
struct Script {
    connect_fails: bool,
    probe_hangs: bool,
    transaction_active: bool,
    catalog: bool,
    catalog_fails: bool,
    reads: Mutex<VecDeque<&'static str>>,
}
struct Driver(Arc<Script>);
struct Session {
    script: Arc<Script>,
    name: &'static str,
}
struct Opener(Arc<Script>);
struct Cancel;
struct Cursor;

#[async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "trace"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, _: ConnectionOptions) -> Result<Box<dyn Connection>> {
        if self.0.connect_fails {
            tokio::time::sleep(Duration::from_millis(50)).await;
            return Err(DriverError::new(ErrorKind::Connection, "refused"));
        }
        Ok(Box::new(Session {
            script: self.0.clone(),
            name: "Query",
        }))
    }
}
#[async_trait]
impl SessionOpener for Opener {
    async fn open(&self) -> Result<Box<dyn Connection>> {
        if self.0.catalog_fails {
            return Err(DriverError::new(ErrorKind::Connection, "catalog refused"));
        }
        Ok(Box::new(Session {
            script: self.0.clone(),
            name: "Catalog",
        }))
    }
}
#[async_trait]
impl CancelHandle for Cancel {
    async fn cancel(&self) -> Result<()> {
        Ok(())
    }
}
#[async_trait]
impl Connection for Session {
    fn sibling_sessions(&self) -> Option<Arc<dyn SessionOpener>> {
        (self.name == "Query" && self.script.catalog)
            .then(|| Arc::new(Opener(self.script.clone())) as Arc<dyn SessionOpener>)
    }
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(Cancel)
    }
    async fn sql_mode(&mut self) -> Result<Option<String>> {
        if self.script.probe_hangs {
            std::future::pending::<()>().await;
        }
        Ok(None)
    }
    async fn transaction_state(&mut self) -> Result<Option<bool>> {
        Ok(Some(self.script.transaction_active))
    }
    async fn execute_bounded(
        &mut self,
        sql: &str,
        _: QueryOptions,
        _: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        if sql == "slow" {
            tokio::time::sleep(Duration::from_secs(30)).await;
        }
        Ok(Box::new(Cursor))
    }
    async fn open_object(
        &mut self,
        _: &ObjectId,
        _: &ObjectDataTime,
        _: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        Ok(Box::new(Cursor))
    }
    async fn load_metadata(&mut self, _: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
        self.script.reads.lock().unwrap().push_back(self.name);
        Ok(vec![])
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
#[async_trait]
impl ResultCursor for Cursor {
    fn columns(&self) -> &[Column] {
        &[]
    }
    async fn fetch_page_bounded(&mut self, _: PageSize, _: usize) -> Result<ResultPage> {
        Ok(ResultPage {
            index: 0,
            rows: vec![],
            has_more: false,
        })
    }
    async fn load_value(&mut self, _: Handle) -> Result<Value> {
        Err(DriverError::new(ErrorKind::Unsupported, "no values"))
    }
    fn summary(&self) -> QuerySummary {
        QuerySummary::default()
    }
    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}

struct Recorder {
    engine: Engine,
    script: Arc<Script>,
    slots: HashMap<QueryId, &'static str>,
    lines: Vec<Json>,
}

impl Recorder {
    fn new(script: Script) -> Self {
        let script = Arc::new(script);
        Self {
            engine: Engine::new(
                EngineConfig::default(),
                vec![Arc::new(Driver(script.clone()))],
            )
            .unwrap(),
            script,
            slots: HashMap::new(),
            lines: vec![],
        }
    }

    fn connect(&mut self, options: ConnectionOptions) -> ConnectionId {
        self.engine.connect("trace", options).unwrap()
    }

    fn execute(&mut self, connection: ConnectionId, sql: &str, options: QueryOptions) -> QueryId {
        let query = self
            .engine
            .execute(connection, sql.into(), options)
            .unwrap();
        self.slots.insert(query, "sql");
        query
    }

    fn read_object(&mut self, connection: ConnectionId, timeout: Duration) -> QueryId {
        let query = self
            .engine
            .open_object_data(
                connection,
                ObjectId("main.items".into()),
                ObjectDataTime::Current,
                QueryOptions {
                    timeout: Some(timeout),
                    ..Default::default()
                },
            )
            .unwrap();
        self.slots.insert(query, "object");
        query
    }

    fn until(&mut self, done: impl Fn(&Event) -> bool) -> Event {
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            if let Some(event) = self.engine.try_event() {
                self.record(&event);
                if done(&event) {
                    return event;
                }
                continue;
            }
            assert!(Instant::now() < deadline, "event timed out");
            std::thread::sleep(Duration::from_millis(1));
        }
    }

    fn record(&mut self, event: &Event) {
        let line = match event {
            Event::Connected {
                transaction_active, ..
            } => json!({"kind": "Connected", "transaction": *transaction_active != Some(false)}),
            Event::ConnectionFailed { .. } => json!({"kind": "ConnectionFailed"}),
            Event::Disconnected { .. } => json!({"kind": "Disconnected"}),
            Event::QueryState { query, state } => json!({
                "kind": "QueryState",
                "slot": self.slots[query],
                "state": format!("{state:?}"),
            }),
            Event::TransactionState { active, .. } => {
                json!({"kind": "TransactionState", "active": *active})
            }
            Event::QueryFailed { query, error } => json!({
                "kind": "QueryFailed",
                "slot": self.slots[query],
                "error": match error.kind {
                    ErrorKind::Cancelled | ErrorKind::Timeout | ErrorKind::Disconnected => {
                        format!("{:?}", error.kind)
                    }
                    _ => "Error".to_owned(),
                },
            }),
            Event::OperationFailed { .. } => json!({"kind": "OperationFailed"}),
            Event::Metadata { .. } => {
                let session = self.script.reads.lock().unwrap().pop_front().unwrap();
                json!({"kind": "Read", "session": session})
            }
            _ => return,
        };
        self.lines.push(line);
    }

    fn write(self, scenario: &str) -> Vec<Json> {
        let directory =
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../target/tla-traces");
        std::fs::create_dir_all(&directory).unwrap();
        let text: String = self.lines.iter().map(|line| format!("{line}\n")).collect();
        std::fs::write(directory.join(format!("{scenario}.ndjson")), text).unwrap();
        self.lines
    }
}

fn sqlite() -> ConnectionOptions {
    ConnectionOptions::Sqlite {
        path: ":memory:".into(),
        read_only: false,
    }
}

fn postgres_with_probe_timeout(seconds: u32) -> ConnectionOptions {
    ConnectionOptions::Postgres {
        proxy: None,
        proxy_secret: None,
        host: "localhost".into(),
        port: 5432,
        database: "trace".into(),
        user: "trace".into(),
        password: None,
        ssh_secret: None,
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        tls: TlsMode::Disable,
        root_certificate: None,
        tls_identity: None,
        ssh: Some(SshTunnel {
            host: "bastion".into(),
            port: 22,
            user: "trace".into(),
            authentication: SshAuthentication::Agent,
            identity_file: None,
            identity_source: Default::default(),
            options: Box::new(SshOptions {
                connect_timeout_seconds: seconds,
                ..Default::default()
            }),
        }),
    }
}

fn query_state(slot: &str, state: &str) -> Json {
    json!({"kind": "QueryState", "slot": slot, "state": state})
}

fn query_failed(slot: &str, error: &str) -> Json {
    json!({"kind": "QueryFailed", "slot": slot, "error": error})
}

fn connected(transaction: bool) -> Json {
    json!({"kind": "Connected", "transaction": transaction})
}

fn finished(query: QueryId) -> impl Fn(&Event) -> bool {
    move |event| matches!(event, Event::QueryFailed { query: q, .. } | Event::QueryFinished { query: q, .. } if *q == query)
}

#[test]
fn connect_failure_ends_queued_queries() {
    let mut trace = Recorder::new(Script {
        connect_fails: true,
        ..Default::default()
    });
    let connection = trace.connect(sqlite());
    let query = trace.execute(connection, "select 1", QueryOptions::default());
    trace.until(finished(query));
    assert_eq!(
        trace.write("connect_failure"),
        [
            query_state("sql", "Queued"),
            json!({"kind": "ConnectionFailed"}),
            query_state("sql", "Failed"),
            query_failed("sql", "Disconnected"),
        ]
    );
}

#[test]
fn probe_timeout_fails_the_connection() {
    let mut trace = Recorder::new(Script {
        probe_hangs: true,
        ..Default::default()
    });
    trace.connect(postgres_with_probe_timeout(1));
    let failed = trace.until(|event| matches!(event, Event::ConnectionFailed { .. }));
    assert!(
        matches!(failed, Event::ConnectionFailed { error, .. } if error.kind == ErrorKind::Timeout)
    );
    assert_eq!(
        trace.write("probe_timeout"),
        [json!({"kind": "ConnectionFailed"})]
    );
}

#[test]
fn run_completes() {
    let mut trace = Recorder::new(Script::default());
    let connection = trace.connect(sqlite());
    trace.until(|event| matches!(event, Event::Connected { .. }));
    let query = trace.execute(connection, "select 1", QueryOptions::default());
    trace.until(|event| matches!(event, Event::Schema { .. }));
    trace.engine.fetch_page(query).unwrap();
    trace.until(finished(query));
    assert_eq!(
        trace.write("run_complete"),
        [
            connected(false),
            query_state("sql", "Queued"),
            query_state("sql", "Running"),
            query_state("sql", "Completed { empty: false }"),
        ]
    );
}

#[test]
fn run_cancel_passes_through_cancelling() {
    let mut trace = Recorder::new(Script::default());
    let connection = trace.connect(sqlite());
    let query = trace.execute(connection, "slow", QueryOptions::default());
    trace.until(|event| {
        matches!(
            event,
            Event::QueryState {
                state: QueryState::Running,
                ..
            }
        )
    });
    trace.engine.cancel(query).unwrap();
    let failed = trace.until(finished(query));
    assert!(
        matches!(failed, Event::QueryFailed { error, .. } if error.kind == ErrorKind::Cancelled)
    );
    let lines = trace.write("run_cancel");
    assert_eq!(
        lines[lines.len() - 3..],
        [
            query_state("sql", "Cancelling"),
            query_state("sql", "Cancelled"),
            query_failed("sql", "Cancelled"),
        ]
    );
}

#[test]
fn run_timeout_passes_through_cancelling() {
    let mut trace = Recorder::new(Script::default());
    let connection = trace.connect(sqlite());
    let query = trace.execute(
        connection,
        "slow",
        QueryOptions {
            timeout: Some(Duration::from_millis(30)),
            ..Default::default()
        },
    );
    let failed = trace.until(finished(query));
    assert!(matches!(failed, Event::QueryFailed { error, .. } if error.kind == ErrorKind::Timeout));
    let lines = trace.write("run_timeout");
    assert_eq!(
        lines[lines.len() - 3..],
        [
            query_state("sql", "Cancelling"),
            query_state("sql", "Failed"),
            query_failed("sql", "Timeout"),
        ]
    );
}

#[test]
fn object_read_timeout_emits_cancelling_before_the_failure() {
    let mut trace = Recorder::new(Script::default());
    let connection = trace.connect(sqlite());
    trace.until(|event| matches!(event, Event::Connected { .. }));
    let query = trace.read_object(connection, Duration::from_millis(30));
    let failed = trace.until(finished(query));
    assert!(matches!(failed, Event::QueryFailed { error, .. } if error.kind == ErrorKind::Timeout));
    assert_eq!(
        trace.write("object_read_timeout"),
        [
            connected(false),
            query_state("object", "Queued"),
            query_state("object", "Running"),
            query_state("object", "Cancelling"),
            query_state("object", "Failed"),
            query_failed("object", "Timeout"),
        ]
    );
}

#[test]
fn disconnect_during_run_ends_the_query_before_the_connection() {
    let mut trace = Recorder::new(Script::default());
    let connection = trace.connect(sqlite());
    trace.execute(connection, "slow", QueryOptions::default());
    trace.until(|event| {
        matches!(
            event,
            Event::QueryState {
                state: QueryState::Running,
                ..
            }
        )
    });
    trace.engine.disconnect(connection).unwrap();
    trace.until(|event| matches!(event, Event::Disconnected { .. }));
    let lines = trace.write("disconnect_during_run");
    let tail: Vec<_> = lines
        .iter()
        .skip_while(|line| **line != query_state("sql", "Running"))
        .skip(1)
        .filter(|line| **line != query_state("sql", "Cancelling"))
        .cloned()
        .collect();
    assert_eq!(
        tail,
        [
            query_state("sql", "Failed"),
            query_failed("sql", "Disconnected"),
            json!({"kind": "Disconnected"})
        ]
    );
}

#[test]
fn an_open_transaction_routes_navigator_reads_to_the_query_session() {
    let mut trace = Recorder::new(Script {
        transaction_active: true,
        catalog: true,
        ..Default::default()
    });
    let connection = trace.connect(sqlite());
    trace.until(|event| matches!(event, Event::Connected { .. }));
    trace.engine.load_metadata(connection, None).unwrap();
    trace.until(|event| matches!(event, Event::Metadata { .. }));
    assert_eq!(
        trace.write("catalog_fallback_open_transaction"),
        [connected(true), json!({"kind": "Read", "session": "Query"})]
    );
}

#[test]
fn a_failed_catalog_session_falls_back_to_the_query_session() {
    let mut trace = Recorder::new(Script {
        catalog: true,
        catalog_fails: true,
        ..Default::default()
    });
    let connection = trace.connect(sqlite());
    trace.until(|event| matches!(event, Event::Connected { .. }));
    trace.engine.load_metadata(connection, None).unwrap();
    trace.until(|event| matches!(event, Event::Metadata { .. }));
    assert_eq!(
        trace.write("catalog_open_failure"),
        [
            connected(false),
            json!({"kind": "OperationFailed"}),
            json!({"kind": "Read", "session": "Query"})
        ]
    );
}
