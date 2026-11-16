use async_trait::async_trait;
use choscordb_core::*;
use choscordb_driver_api::*;
use std::{
    sync::Arc,
    time::{Duration, Instant},
};

struct Driver;
struct Conn;
struct NoCancel;
struct Sets {
    set: usize,
    columns: Vec<Column>,
}

fn column(name: &str) -> Vec<Column> {
    vec![Column {
        name: name.into(),
        database_type: "INTEGER".into(),
        precision: None,
        scale: None,
        timezone: None,
        nullable: None,
    }]
}

#[async_trait]
impl DatabaseDriver for Driver {
    fn id(&self) -> &'static str {
        "sets"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(&self, _: ConnectionOptions) -> Result<Box<dyn Connection>> {
        Ok(Box::new(Conn))
    }
}
#[async_trait]
impl CancelHandle for NoCancel {
    async fn cancel(&self) -> Result<()> {
        Ok(())
    }
}
#[async_trait]
impl Connection for Conn {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(NoCancel)
    }
    async fn execute_bounded(
        &mut self,
        _: &str,
        _: QueryOptions,
        _: usize,
    ) -> Result<Box<dyn ResultCursor>> {
        Ok(Box::new(Sets {
            set: 0,
            columns: column("first"),
        }))
    }
    async fn load_metadata(&mut self, _: Option<ObjectId>) -> Result<Vec<SchemaObject>> {
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
impl ResultCursor for Sets {
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn fetch_page_bounded(&mut self, _: PageSize, _: usize) -> Result<ResultPage> {
        Ok(ResultPage {
            index: 0,
            rows: vec![vec![Value::Integer(self.set as i64 + 1)]],
            has_more: false,
        })
    }
    async fn next_result_set(&mut self) -> Result<bool> {
        if self.set == 1 {
            return Ok(false);
        }
        self.set = 1;
        self.columns = column("second");
        Ok(true)
    }
    fn summary(&self) -> QuerySummary {
        QuerySummary {
            has_more_results: self.set == 0,
            ..Default::default()
        }
    }
    async fn close(&mut self) -> Result<()> {
        Ok(())
    }
}

fn wait<T>(engine: &mut Engine, mut pick: impl FnMut(Event) -> Option<T>) -> T {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if let Some(found) = engine.try_event().and_then(&mut pick) {
            return found;
        }
        assert!(Instant::now() < deadline, "no expected event");
        std::thread::sleep(Duration::from_millis(1));
    }
}

fn schema(engine: &mut Engine, query: QueryId) -> String {
    wait(engine, |event| match event {
        Event::Schema {
            query: q, columns, ..
        } if q == query => Some(columns[0].name.clone()),
        Event::QueryFailed { error, .. } => panic!("{}", error.message),
        _ => None,
    })
}

fn rows(engine: &mut Engine, query: QueryId) -> Vec<Row> {
    engine.fetch_page_at(query, 0).unwrap();
    wait(engine, |event| match event {
        Event::StoredPage { query: q, page, .. } if q == query => Some(page.rows.clone()),
        Event::QueryFailed { error, .. } => panic!("{}", error.message),
        _ => None,
    })
}

fn finished(engine: &mut Engine, query: QueryId) -> bool {
    wait(engine, |event| match event {
        Event::QueryFinished {
            query: q, summary, ..
        } if q == query => Some(summary.has_more_results),
        Event::QueryFailed { error, .. } => panic!("{}", error.message),
        _ => None,
    })
}

#[test]
fn each_further_result_set_gets_its_own_query_and_the_first_stays_readable() {
    let mut engine = Engine::new(EngineConfig::default(), vec![Arc::new(Driver)]).unwrap();
    let connection = engine
        .connect(
            "sets",
            ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    let first = engine
        .execute(
            connection,
            "CALL two_sets()".into(),
            QueryOptions::default(),
        )
        .unwrap();
    assert_eq!(schema(&mut engine, first), "first");
    assert_eq!(rows(&mut engine, first), [vec![Value::Integer(1)]]);
    assert!(finished(&mut engine, first));
    let second = engine.next_result_set(first).unwrap();
    assert_ne!(second, first);
    assert_eq!(schema(&mut engine, second), "second");
    assert_eq!(rows(&mut engine, second), [vec![Value::Integer(2)]]);
    assert!(!finished(&mut engine, second));
    assert_eq!(rows(&mut engine, first), [vec![Value::Integer(1)]]);
    engine.release_query(first).unwrap();
    assert_eq!(rows(&mut engine, second), [vec![Value::Integer(2)]]);
    engine.release_query(second).unwrap();
}
