use async_trait::async_trait;
use choscordb_core::{
    Event, ExportFormat, FilterCondition, FilterOperator, ResultSort, SortDirection,
};
use choscordb_driver_api::{
    CancelHandle, Column, Connection, ConnectionOptions, DatabaseDriver, DriverCapabilities,
    PageSize, QueryOptions, QuerySummary, ResultCursor, ResultPage, Value,
};
use choscordb_driver_sqlite::SqliteDriver;
use std::{
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicU64, Ordering},
    },
    time::Duration,
};

struct SlowDriver(Arc<AtomicBool>);
struct SlowConnection(Arc<AtomicBool>);
struct SlowCursor {
    next: i64,
    started: Arc<AtomicBool>,
}
struct NoopCancel;

#[async_trait]
impl DatabaseDriver for SlowDriver {
    fn id(&self) -> &'static str {
        "slow-view"
    }
    fn capabilities(&self) -> DriverCapabilities {
        DriverCapabilities::default()
    }
    async fn connect(
        &self,
        _: ConnectionOptions,
    ) -> choscordb_driver_api::Result<Box<dyn Connection>> {
        Ok(Box::new(SlowConnection(self.0.clone())))
    }
}
#[async_trait]
impl CancelHandle for NoopCancel {
    async fn cancel(&self) -> choscordb_driver_api::Result<()> {
        Ok(())
    }
}
#[async_trait]
impl Connection for SlowConnection {
    fn cancellation_handle(&self) -> Arc<dyn CancelHandle> {
        Arc::new(NoopCancel)
    }
    async fn execute(
        &mut self,
        _: &str,
        _: QueryOptions,
    ) -> choscordb_driver_api::Result<Box<dyn ResultCursor>> {
        Ok(Box::new(SlowCursor {
            next: 1,
            started: self.0.clone(),
        }))
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
impl ResultCursor for SlowCursor {
    fn columns(&self) -> &[Column] {
        static COLUMNS: std::sync::LazyLock<Vec<Column>> = std::sync::LazyLock::new(|| {
            vec![Column {
                name: "value".into(),
                database_type: "INTEGER".into(),
                precision: None,
                scale: None,
                timezone: None,
                nullable: Some(false),
            }]
        });
        &COLUMNS
    }
    async fn fetch_page(&mut self, _: PageSize) -> choscordb_driver_api::Result<ResultPage> {
        self.started.store(true, Ordering::Release);
        tokio::time::sleep(Duration::from_millis(200)).await;
        let first = self.next;
        let end = (first + 100).min(301);
        self.next = end;
        Ok(ResultPage {
            index: ((first - 1) / 100) as u64,
            rows: (first..end)
                .map(|value| vec![Value::Integer(value)])
                .collect(),
            has_more: end < 301,
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
        std::thread::sleep(Duration::from_millis(5));
    }
    panic!("timed out waiting for event")
}

fn failed_view(engine: &mut choscordb_core::Engine) -> choscordb_driver_api::DriverError {
    for _ in 0..1000 {
        if let Some(Event::ResultViewFailed { error, .. }) = engine.try_event() {
            return error;
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    panic!("timed out waiting for result view failure")
}

fn directory_bytes(path: &std::path::Path) -> u64 {
    std::fs::read_dir(path).map_or(0, |entries| {
        entries
            .filter_map(Result::ok)
            .map(|entry| {
                entry.metadata().map_or(0, |metadata| {
                    if metadata.is_dir() {
                        directory_bytes(&entry.path())
                    } else {
                        metadata.len()
                    }
                })
            })
            .sum()
    })
}

#[test]
fn whole_result_view_filters_sorts_pages_and_clears_without_reexecution() {
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(
            connection,
            "with recursive c(x) as (select 1 union all select x+1 from c where x<206) select case when x=206 then null else printf('row%03d',206-x) end as name, x as score from c".into(),
            QueryOptions { page_size: PageSize::new(100).unwrap(), ..Default::default() },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Schema { .. }));

    engine
        .apply_result_view(
            query,
            vec![FilterCondition {
                column: 1,
                operator: FilterOperator::GreaterThan,
                value: Some(Value::Integer(100)),
            }],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Ascending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    event(
        &mut engine,
        |e| matches!(e, Event::ResultViewApplied { query: q, rows: 106 } if *q == query),
    );

    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let first = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    let Event::StoredPage {
        page,
        first_row,
        lease,
        ..
    } = first
    else {
        unreachable!()
    };
    assert_eq!(first_row, 0);
    assert_eq!(
        page.rows[0],
        vec![Value::Text("row001".into()), Value::Integer(205)]
    );
    assert_eq!(
        page.rows[99],
        vec![Value::Text("row100".into()), Value::Integer(106)]
    );
    assert!(page.has_more);
    drop(lease);

    engine
        .fetch_page_at(query, 1, PageSize::new(100).unwrap())
        .unwrap();
    let second = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    let Event::StoredPage {
        page,
        first_row,
        lease,
        ..
    } = second
    else {
        unreachable!()
    };
    assert_eq!(first_row, 100);
    assert_eq!(page.rows[0][0], Value::Text("row101".into()));
    assert_eq!(page.rows[5][0], Value::Null); // NULL is last even ascending.
    assert!(!page.has_more);
    drop(lease);

    let export_directory = tempfile::tempdir().unwrap();
    let destination = export_directory.path().join("filtered.csv");
    engine
        .start_export(query, destination.clone(), ExportFormat::Csv)
        .unwrap();
    event(
        &mut engine,
        |e| matches!(e, Event::ExportFinished { query: q, rows: 106, .. } if *q == query),
    );
    let exported = std::fs::read_to_string(destination).unwrap();
    let lines = exported.lines().collect::<Vec<_>>();
    assert_eq!(lines[1], "\"row001\",\"205\"");
    assert_eq!(lines.last().copied(), Some(",\"206\""));

    engine
        .apply_result_view(
            query,
            vec![FilterCondition {
                column: 1,
                operator: FilterOperator::LessThan,
                value: Some(Value::Integer(10)),
            }],
            None,
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    engine.cancel_result_view(query).unwrap();
    assert_eq!(
        failed_view(&mut engine).kind,
        choscordb_driver_api::ErrorKind::Cancelled
    );
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let retained = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    let Event::StoredPage { page, lease, .. } = retained else {
        unreachable!()
    };
    assert_eq!(page.rows[0][0], Value::Text("row001".into()));
    drop(lease);

    engine.clear_result_view(query).unwrap();
    event(
        &mut engine,
        |e| matches!(e, Event::ResultViewApplied { query: q, rows: 206 } if *q == query),
    );
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let restored = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    assert!(
        matches!(restored, Event::StoredPage { page, .. } if page.rows[0][0] == Value::Text("row205".into()))
    );
}

#[test]
fn descending_text_sort_is_case_insensitive_stable_and_keeps_null_last() {
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(
            connection,
            "select 'a' as value, 1 as sequence union all select 'A', 2 union all select null, 3"
                .into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..Default::default()
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Descending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    event(&mut engine, |e| {
        matches!(e, Event::ResultViewApplied { rows: 3, .. })
    });
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let page = event(&mut engine, |e| matches!(e, Event::StoredPage { .. }));
    let Event::StoredPage { page, lease, .. } = page else {
        unreachable!()
    };
    assert_eq!(
        page.rows[0],
        vec![Value::Text("a".into()), Value::Integer(1)]
    );
    assert_eq!(
        page.rows[1],
        vec![Value::Text("A".into()), Value::Integer(2)]
    );
    assert_eq!(page.rows[2], vec![Value::Null, Value::Integer(3)]);
    drop(lease);
}

#[test]
fn text_predicates_are_case_insensitive_and_deferred_values_fail_atomically() {
    // The comparator contract is exercised independently of driver truncation policy.
    assert!(
        choscordb_core::value_matches(
            &Value::Text("StraSSe".into()),
            FilterOperator::Contains,
            Some(&Value::Text("strass".into()))
        )
        .unwrap()
    );
    assert!(choscordb_core::value_matches(&Value::Null, FilterOperator::IsNull, None).unwrap());
    assert!(
        choscordb_core::value_matches(
            &Value::Deferred {
                handle: choscordb_driver_api::Handle {
                    slot: 1,
                    generation: 0
                },
                byte_length: 100,
                database_type: "TEXT".into()
            },
            FilterOperator::Equals,
            Some(&Value::Text("x".into()))
        )
        .is_err()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Date("2026-09-21".into()),
            FilterOperator::LessThan,
            Some(&Value::Date("2026-09-22".into()))
        )
        .is_err()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Decimal("2e1".into()),
            FilterOperator::LessThan,
            Some(&Value::Decimal("99".into()))
        )
        .unwrap()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Decimal("1.20".into()),
            FilterOperator::Equals,
            Some(&Value::Decimal("1.2".into()))
        )
        .unwrap()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Integer(9_007_199_254_740_993),
            FilterOperator::GreaterThan,
            Some(&Value::Real(9_007_199_254_740_992.0))
        )
        .unwrap()
    );
}

#[test]
fn external_sort_keeps_engine_buffers_within_one_page_under_tight_memory() {
    let config = choscordb_core::EngineConfig {
        page_memory: choscordb_core::PageMemoryConfig {
            per_result_bytes: 1024 * 1024,
            global_bytes: 4 * 1024 * 1024,
        },
        ..Default::default()
    };
    let mut engine = choscordb_core::Engine::new(config, vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(
            connection,
            "with recursive c(x) as (select 1 union all select x+1 from c where x<5000) select x, printf('%.*c',2048,'x') from c".into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..Default::default()
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Descending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    let mut maximum_buffered = 0;
    loop {
        match event(
            &mut engine,
            |event| matches!(event, Event::ResultViewProgress { query: q, .. } | Event::ResultViewApplied { query: q, .. } if *q == query),
        ) {
            Event::ResultViewProgress { buffered_rows, .. } => {
                maximum_buffered = maximum_buffered.max(buffered_rows);
            }
            Event::ResultViewApplied { rows, .. } => {
                assert_eq!(rows, 5000);
                break;
            }
            _ => unreachable!(),
        }
    }
    assert!(maximum_buffered <= 100);
    assert!(engine.memory_usage().peak_bytes <= 1024 * 1024);
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let page = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    assert!(
        matches!(page, Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(5000) && page.rows[0][1] == Value::Text("x".repeat(2048)))
    );
}

#[test]
fn cancelling_a_live_transform_keeps_already_stored_original_pages_readable() {
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Connected { .. }));
    let query = engine
        .execute(
            connection,
            "with recursive c(x) as (select 1 union all select x+1 from c where x<100000) select x from c".into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..Default::default()
            },
        )
        .unwrap();
    event(&mut engine, |e| matches!(e, Event::Schema { .. }));
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let first = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    let Event::StoredPage { lease, .. } = first else {
        unreachable!()
    };
    drop(lease);
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Descending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    engine.cancel_result_view(query).unwrap();
    assert_eq!(
        failed_view(&mut engine).kind,
        choscordb_driver_api::ErrorKind::Cancelled
    );
    engine
        .fetch_page_at(query, 0, PageSize::new(100).unwrap())
        .unwrap();
    let retained = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    let Event::StoredPage { page, lease, .. } = retained else {
        unreachable!()
    };
    assert_eq!(page.rows[0][0], Value::Integer(1));
    drop(lease);
    engine
        .fetch_page_at(query, 1, PageSize::new(100).unwrap())
        .unwrap();
    let continued = event(
        &mut engine,
        |e| matches!(e, Event::StoredPage { query: q, .. } if *q == query),
    );
    assert!(
        matches!(continued, Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(101))
    );
}

#[test]
fn sqlite_declared_types_compare_against_typed_filter_operands() {
    let directory = tempfile::tempdir().unwrap();
    let database = directory.path().join("typed.sqlite");
    rusqlite::Connection::open(&database)
        .unwrap()
        .execute_batch(
            "CREATE TABLE typed(id INTEGER, flag BOOLEAN, happened DATE, at TIME, stamp TIMESTAMP, amount DECIMAL, payload JSON);
             INSERT INTO typed VALUES
             (1,1,'2026-09-21','10:30:00','2026-09-21T10:30:00Z',12.5,'{\"Name\":\"A\",\"n\":1}'),
             (2,0,'2026-09-22','11:30:00','2026-09-22T11:30:00Z',9,'{\"Name\":\"a\",\"n\":2}');",
        )
        .unwrap();
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: database,
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let query = engine
        .execute(
            connection,
            "SELECT id,flag,happened,at,stamp,amount,payload FROM typed".into(),
            QueryOptions::default(),
        )
        .unwrap();
    event(&mut engine, |event| matches!(event, Event::Schema { .. }));
    engine
        .apply_result_view(
            query,
            vec![
                FilterCondition {
                    column: 1,
                    operator: FilterOperator::Equals,
                    value: Some(Value::Bool(true)),
                },
                FilterCondition {
                    column: 2,
                    operator: FilterOperator::Equals,
                    value: Some(Value::Date("2026-09-21".into())),
                },
                FilterCondition {
                    column: 3,
                    operator: FilterOperator::Equals,
                    value: Some(Value::Time("10:30:00".into())),
                },
                FilterCondition {
                    column: 4,
                    operator: FilterOperator::Equals,
                    value: Some(Value::Timestamp("2026-09-21T10:30:00Z".into())),
                },
                FilterCondition {
                    column: 5,
                    operator: FilterOperator::Equals,
                    value: Some(Value::Decimal("12.500".into())),
                },
                FilterCondition {
                    column: 6,
                    operator: FilterOperator::Equals,
                    value: Some(Value::Json("{\"n\":1.0,\"Name\":\"A\"}".into())),
                },
            ],
            None,
            PageSize::default(),
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::ResultViewApplied { rows: 1, .. })
    });
    engine.fetch_page_at(query, 0, PageSize::default()).unwrap();
    let page = event(&mut engine, |event| {
        matches!(event, Event::StoredPage { .. })
    });
    let Event::StoredPage { page, lease, .. } = page else {
        unreachable!()
    };
    assert_eq!(page.rows[0][0], Value::Integer(1));
    drop(lease);

    engine
        .apply_result_view(
            query,
            vec![FilterCondition {
                column: 6,
                operator: FilterOperator::Equals,
                value: Some(Value::Json("{\"Name\":\"a\",\"n\":1}".into())),
            }],
            None,
            PageSize::default(),
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::ResultViewApplied { rows: 0, .. })
    });
    let zero_export = directory.path().join("zero.csv");
    engine
        .start_export(query, zero_export.clone(), ExportFormat::Csv)
        .unwrap();
    event(
        &mut engine,
        |event| matches!(event, Event::ExportFinished { query: q, rows: 0, .. } if *q == query),
    );
    let zero_csv = std::fs::read_to_string(zero_export).unwrap();
    assert_eq!(
        zero_csv.lines().collect::<Vec<_>>(),
        vec!["\"id\",\"flag\",\"happened\",\"at\",\"stamp\",\"amount\",\"payload\""]
    );

    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 6,
                direction: SortDirection::Ascending,
            }),
            PageSize::default(),
        )
        .unwrap();
    assert_eq!(
        failed_view(&mut engine).kind,
        choscordb_driver_api::ErrorKind::InvalidInput
    );
}

#[test]
fn aggregate_disk_budget_holds_across_success_failure_cancel_and_replacement() {
    const DISK_LIMIT: u64 = 16 * 1024 * 1024;
    let directory = tempfile::tempdir().unwrap();
    let config = choscordb_core::EngineConfig {
        result_store_directory: Some(directory.path().into()),
        result_store: choscordb_result_store::StoreConfig {
            max_disk_bytes: DISK_LIMIT,
            ..Default::default()
        },
        ..Default::default()
    };
    let mut engine = choscordb_core::Engine::new(config, vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            choscordb_driver_api::ConnectionOptions::Sqlite {
                path: ":memory:".into(),
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let query = engine
        .execute(
            connection,
            "with recursive c(x) as (select 1 union all select x+1 from c where x<2000) select x, printf('%.*c',256,'x'), case when x=2000 then 'bad' else x end from c".into(),
            QueryOptions { page_size: PageSize::new(100).unwrap(), ..Default::default() },
        )
        .unwrap();
    event(&mut engine, |event| matches!(event, Event::Schema { .. }));

    let stop = Arc::new(AtomicBool::new(false));
    let maximum = Arc::new(AtomicU64::new(0));
    let monitor_path = directory.path().to_path_buf();
    let monitor_stop = stop.clone();
    let monitor_maximum = maximum.clone();
    let monitor = std::thread::spawn(move || {
        while !monitor_stop.load(Ordering::Relaxed) {
            monitor_maximum.fetch_max(directory_bytes(&monitor_path), Ordering::Relaxed);
            std::thread::sleep(Duration::from_millis(1));
        }
    });

    for direction in [SortDirection::Ascending, SortDirection::Descending] {
        engine
            .apply_result_view(
                query,
                vec![],
                Some(ResultSort {
                    column: 0,
                    direction,
                }),
                PageSize::new(100).unwrap(),
            )
            .unwrap();
        event(&mut engine, |event| {
            matches!(event, Event::ResultViewApplied { rows: 2000, .. })
        });
    }
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 2,
                direction: SortDirection::Ascending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    assert_eq!(
        failed_view(&mut engine).kind,
        choscordb_driver_api::ErrorKind::InvalidInput
    );
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Ascending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    event(
        &mut engine,
        |event| matches!(event, Event::ResultViewProgress { query: q, scanned_rows, .. } if *q == query && *scanned_rows > 0),
    );
    engine.cancel_result_view(query).unwrap();
    assert_eq!(
        failed_view(&mut engine).kind,
        choscordb_driver_api::ErrorKind::Cancelled
    );
    engine.clear_result_view(query).unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::ResultViewApplied { rows: 2000, .. })
    });
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Ascending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::ResultViewApplied { rows: 2000, .. })
    });

    stop.store(true, Ordering::Relaxed);
    monitor.join().unwrap();
    assert!(maximum.load(Ordering::Relaxed) > 0);
    assert!(maximum.load(Ordering::Relaxed) <= DISK_LIMIT);
    assert!(directory_bytes(directory.path()) <= DISK_LIMIT);
}

#[test]
fn delayed_inflight_cancel_reports_progress_then_preserves_cursor() {
    let started = Arc::new(AtomicBool::new(false));
    let mut engine = choscordb_core::Engine::new(
        Default::default(),
        vec![Arc::new(SlowDriver(started.clone()))],
    )
    .unwrap();
    let connection = engine
        .connect(
            "slow-view",
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
        .execute(
            connection,
            "slow".into(),
            QueryOptions {
                page_size: PageSize::new(100).unwrap(),
                ..Default::default()
            },
        )
        .unwrap();
    event(&mut engine, |event| matches!(event, Event::Schema { .. }));
    engine
        .apply_result_view(
            query,
            vec![],
            Some(ResultSort {
                column: 0,
                direction: SortDirection::Descending,
            }),
            PageSize::new(100).unwrap(),
        )
        .unwrap();
    let wait_started = std::time::Instant::now();
    while !started.load(Ordering::Acquire) {
        assert!(wait_started.elapsed() < Duration::from_secs(1));
        std::thread::sleep(Duration::from_millis(1));
    }
    let cancelled = std::time::Instant::now();
    engine.cancel_result_view(query).unwrap();
    let mut saw_progress = false;
    let failure = loop {
        if let Some(event) = engine.try_event() {
            match event {
                Event::ResultViewProgress {
                    scanned_rows: 100, ..
                } => saw_progress = true,
                Event::ResultViewFailed { error, .. } => break error,
                _ => {}
            }
        }
        assert!(cancelled.elapsed() < Duration::from_secs(1));
        std::thread::sleep(Duration::from_millis(1));
    };
    assert_eq!(failure.kind, choscordb_driver_api::ErrorKind::Cancelled);
    assert!(saw_progress);
    assert!(cancelled.elapsed() >= Duration::from_millis(150));
    engine
        .fetch_page_at(query, 1, PageSize::new(100).unwrap())
        .unwrap();
    let next = event(
        &mut engine,
        |event| matches!(event, Event::StoredPage { query: q, .. } if *q == query),
    );
    assert!(
        matches!(next, Event::StoredPage { page, .. } if page.rows[0][0] == Value::Integer(101))
    );
}

#[test]
fn temporal_filters_and_sorts_use_normalized_database_values() {
    assert!(
        choscordb_core::value_matches(
            &Value::Time("-01:00:00".into()),
            FilterOperator::Equals,
            Some(&Value::Text("-01:00:00.000000".into())),
        )
        .unwrap()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Time("+25:00:00.000000".into()),
            FilterOperator::Equals,
            Some(&Value::Text("25:00:00".into())),
        )
        .unwrap()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Timestamp("2024-01-01 00:00:00.000000+00:00".into()),
            FilterOperator::Equals,
            Some(&Value::Text("2023-12-31T19:00:00-05:00".into())),
        )
        .unwrap()
    );
    assert!(
        choscordb_core::value_matches(
            &Value::Date("2023-02-29".into()),
            FilterOperator::Equals,
            Some(&Value::Text("2023-02-29".into())),
        )
        .is_err()
    );

    let directory = tempfile::tempdir().unwrap();
    let database = directory.path().join("temporal.sqlite");
    rusqlite::Connection::open(&database)
        .unwrap()
        .execute_batch(
            "CREATE TABLE temporal(id INTEGER, day DATE, duration TIME, stamp TIMESTAMP);
             INSERT INTO temporal VALUES
             (1,'2024-02-29','25:00:00.000000','2024-01-01 00:00:00+00:00'),
             (2,'2024-03-01','-01:00:00','2023-12-31T19:00:00-05:00'),
             (3,'2023-12-31','100:00:00.5','2024-01-01T00:00:00.250000Z');",
        )
        .unwrap();
    let mut engine =
        choscordb_core::Engine::new(Default::default(), vec![Arc::new(SqliteDriver)]).unwrap();
    let connection = engine
        .connect(
            "sqlite",
            ConnectionOptions::Sqlite {
                path: database,
                read_only: false,
            },
        )
        .unwrap();
    event(&mut engine, |event| {
        matches!(event, Event::Connected { .. })
    });
    let query = engine
        .execute(
            connection,
            "SELECT id,day,duration,stamp FROM temporal".into(),
            QueryOptions::default(),
        )
        .unwrap();
    event(&mut engine, |event| matches!(event, Event::Schema { .. }));

    for (column, expected) in [(1, vec![3, 1, 2]), (2, vec![2, 1, 3]), (3, vec![1, 2, 3])] {
        engine
            .apply_result_view(
                query,
                vec![],
                Some(ResultSort {
                    column,
                    direction: SortDirection::Ascending,
                }),
                PageSize::default(),
            )
            .unwrap();
        event(&mut engine, |event| {
            matches!(event, Event::ResultViewApplied { rows: 3, .. })
        });
        engine.fetch_page_at(query, 0, PageSize::default()).unwrap();
        let result = event(&mut engine, |event| {
            matches!(event, Event::StoredPage { .. })
        });
        let Event::StoredPage { page, lease, .. } = result else {
            unreachable!()
        };
        let ids = page
            .rows
            .iter()
            .map(|row| match row[0] {
                Value::Integer(value) => value,
                _ => unreachable!(),
            })
            .collect::<Vec<_>>();
        assert_eq!(ids, expected);
        drop(lease);
    }
}
