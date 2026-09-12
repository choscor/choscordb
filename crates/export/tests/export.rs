use choscordb_export::*;
#[test]
fn csv_quotes_commas_quotes_and_newlines() {
    assert_eq!(csv_field("a,\"b\"\nc"), "\"a,\"\"b\"\"\nc\"");
    assert_eq!(csv_field(""), "\"\"");
}
use async_trait::async_trait;
use choscordb_driver_api::*;
fn columns() -> Vec<Column> {
    vec![
        Column {
            name: "duplicate".into(),
            database_type: "TEXT".into(),
            precision: None,
            scale: None,
            timezone: None,
            nullable: Some(true)
        };
        2
    ]
}
struct Source {
    pages: std::collections::VecDeque<Result<ResultPage>>,
    columns: Vec<Column>,
    cancel: Option<Cancellation>,
}
#[async_trait]
impl ExportSource for Source {
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn next_page(&mut self) -> Result<Option<ResultPage>> {
        if let Some(c) = self.cancel.take() {
            c.cancel();
        }
        self.pages.pop_front().transpose()
    }
    async fn resolve(&mut self, _: Handle, _: usize) -> Result<Value> {
        Ok(Value::Text("resolved".into()))
    }
}
fn source() -> Source {
    Source {
        pages: vec![Ok(ResultPage {
            index: 0,
            rows: vec![
                vec![Value::Null, Value::Text("".into())],
                vec![
                    Value::Decimal("12345678901234567890.12345678901234567890".into()),
                    Value::Text("a,\"b\"".into()),
                ],
            ],
            has_more: false,
        })]
        .into(),
        columns: columns(),
        cancel: None,
    }
}
#[tokio::test]
async fn successful_file_replaces_destination_only_when_finished() {
    let dir = tempfile::tempdir().unwrap();
    let destination = dir.path().join("result.json");
    std::fs::write(&destination, "original").unwrap();
    let mut sink = FileSink::create(destination.clone()).await.unwrap();
    assert_eq!(std::fs::read_to_string(&destination).unwrap(), "original");
    let report = export(
        &mut source(),
        &mut sink,
        ExportFormat::Json,
        Cancellation::default(),
        Limits::default(),
        None,
    )
    .await
    .unwrap();
    assert_eq!(report.rows, 2);
    let bytes = std::fs::read(&destination).unwrap();
    assert_eq!(report.bytes, bytes.len() as u64);
    let json: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(json["columns"].as_array().unwrap().len(), 2);
    assert_eq!(json["rows"][0][0], serde_json::Value::Null);
    assert_eq!(json["rows"][0][1], "");
    assert_eq!(
        json["rows"][1][0]["decimal"],
        "12345678901234567890.12345678901234567890"
    );
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
}
#[tokio::test]
async fn cancellation_preserves_old_file_and_removes_temporary() {
    let dir = tempfile::tempdir().unwrap();
    let destination = dir.path().join("result.csv");
    std::fs::write(&destination, "original").unwrap();
    let mut sink = FileSink::create(destination.clone()).await.unwrap();
    let cancellation = Cancellation::default();
    let mut source = source();
    source.cancel = Some(cancellation.clone());
    let result = export(
        &mut source,
        &mut sink,
        ExportFormat::Csv,
        cancellation,
        Limits::default(),
        None,
    )
    .await;
    assert_eq!(result.unwrap_err().kind, ErrorKind::Cancelled);
    assert_eq!(std::fs::read_to_string(&destination).unwrap(), "original");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
}
#[tokio::test]
async fn source_failure_preserves_old_file_and_removes_temporary() {
    let dir = tempfile::tempdir().unwrap();
    let destination = dir.path().join("result.csv");
    std::fs::write(&destination, "original").unwrap();
    let mut sink = FileSink::create(destination.clone()).await.unwrap();
    let mut source = source();
    source.pages.push_back(Err(DriverError::new(
        ErrorKind::Disconnected,
        "source stopped",
    )));
    assert!(
        export(
            &mut source,
            &mut sink,
            ExportFormat::Csv,
            Cancellation::default(),
            Limits::default(),
            None
        )
        .await
        .is_err()
    );
    assert_eq!(std::fs::read_to_string(&destination).unwrap(), "original");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
}
#[test]
fn sql_literals_cannot_inject_through_decimals_or_identifiers() {
    let format = ExportFormat::SqlInsert {
        table: vec!["odd.schema".into(), "a\"b".into()],
        dialect: SqlDialect::Postgres,
    };
    let row = vec![
        Value::Text("a\\'; DROP TABLE t;--".into()),
        Value::Decimal("1); DROP TABLE t;--".into()),
    ];
    assert!(encode_row(&format, &columns(), &row, true).is_err());
    let output = encode_row(
        &format,
        &columns(),
        &vec![Value::Text("a\\'".into()), Value::Decimal("1.2300".into())],
        true,
    )
    .unwrap();
    assert!(output.starts_with("INSERT INTO \"odd.schema\".\"a\"\"b\""));
    assert!(output.contains("E'a\\\\'''"));
    assert!(output.contains("1.2300"));
    for bad in [
        "", "+1", "01", "NaN", "1.", ".1", "1e", "1;SELECT", "-", "1 2",
    ] {
        assert!(!valid_decimal(bad), "{bad}");
    }
    for good in [
        "0",
        "-0",
        "1.2300",
        "1e-10",
        "-123456789012345678901234567890",
    ] {
        assert!(valid_decimal(good));
    }
}
struct FailingSink {
    calls: usize,
    aborted: bool,
    finished: bool,
}
#[async_trait]
impl ExportSink for FailingSink {
    async fn write(&mut self, _: &[u8]) -> Result<()> {
        self.calls += 1;
        if self.calls > 1 {
            Err(DriverError::new(ErrorKind::Io, "disk full"))
        } else {
            Ok(())
        }
    }
    async fn finish(&mut self) -> Result<()> {
        self.finished = true;
        Ok(())
    }
    async fn abort(&mut self) {
        self.aborted = true;
    }
}
#[tokio::test]
async fn disk_full_sink_is_aborted_and_never_committed() {
    let mut sink = FailingSink {
        calls: 0,
        aborted: false,
        finished: false,
    };
    let error = export(
        &mut source(),
        &mut sink,
        ExportFormat::Csv,
        Cancellation::default(),
        Limits::default(),
        None,
    )
    .await
    .unwrap_err();
    assert_eq!(error.kind, ErrorKind::Io);
    assert!(sink.aborted);
    assert!(!sink.finished);
}
#[tokio::test]
async fn json_lines_is_schema_then_positional_rows() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("rows.jsonl");
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    export(
        &mut source(),
        &mut sink,
        ExportFormat::JsonLines,
        Cancellation::default(),
        Limits::default(),
        None,
    )
    .await
    .unwrap();
    let lines = std::fs::read_to_string(path).unwrap();
    let values = lines
        .lines()
        .map(|line| serde_json::from_str::<serde_json::Value>(line).unwrap())
        .collect::<Vec<_>>();
    assert_eq!(values.len(), 3);
    assert!(values[0].get("columns").is_some());
    assert_eq!(values[1]["row"][1], "");
}

struct DeferredSource {
    once: bool,
    columns: Vec<Column>,
}
#[async_trait]
impl ExportSource for DeferredSource {
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn next_page(&mut self) -> Result<Option<ResultPage>> {
        if self.once {
            return Ok(None);
        }
        self.once = true;
        Ok(Some(ResultPage {
            index: 0,
            rows: vec![vec![
                Value::Deferred {
                    handle: Handle {
                        slot: 0,
                        generation: 1
                    },
                    byte_length: 300,
                    database_type: "text".into()
                };
                2
            ]],
            has_more: false,
        }))
    }
    async fn read_value_chunk(
        &mut self,
        _: Handle,
        offset: u64,
        max_bytes: usize,
    ) -> Result<ValueChunk> {
        assert!(max_bytes <= 64);
        Ok(ValueChunk {
            bytes: vec![b'x'; max_bytes.min(300 - offset as usize)],
            offset,
            total_bytes: 300,
            kind: DeferredKind::Text,
        })
    }
}
#[tokio::test]
async fn deferred_row_streams_beyond_the_resident_value_budget() {
    let dir = tempfile::tempdir().unwrap();
    let mut sink = FileSink::create(dir.path().join("large.csv"))
        .await
        .unwrap();
    let mut source = DeferredSource {
        once: false,
        columns: columns(),
    };
    let result = export(
        &mut source,
        &mut sink,
        ExportFormat::Csv,
        Cancellation::default(),
        Limits {
            page_bytes: 512,
            value_bytes: 64,
            encoded_row_bytes: 10000,
        },
        None,
    )
    .await;
    assert_eq!(result.unwrap().rows, 1);
    assert_eq!(
        std::fs::read_to_string(dir.path().join("large.csv")).unwrap(),
        format!(
            "\"duplicate\",\"duplicate\"\r\n\"{}\",\"{}\"\r\n",
            "x".repeat(300),
            "x".repeat(300)
        )
    );
}

proptest::proptest! {
 #[test]
 fn csv_round_trips_arbitrary_unicode(value in ".{0,1000}") {
  let encoded=format!("{}\r\n",csv_field(&value));
  let mut reader=csv::ReaderBuilder::new().has_headers(false).from_reader(encoded.as_bytes());
  let row=reader.records().next().unwrap().unwrap();
  proptest::prop_assert_eq!(row.get(0),Some(value.as_str()));
 }
}

#[tokio::test]
async fn oversized_schema_is_rejected_before_writing_or_reading_rows() {
    let dir = tempfile::tempdir().unwrap();
    let destination = dir.path().join("result.json");
    std::fs::write(&destination, "original").unwrap();
    let mut source = source();
    source.columns[0].name = "\u{0001}".repeat(10000);
    source.pages.clear();
    let mut sink = FileSink::create(destination.clone()).await.unwrap();
    let result = export(
        &mut source,
        &mut sink,
        ExportFormat::Json,
        Cancellation::default(),
        Limits {
            page_bytes: 100000,
            value_bytes: 100000,
            encoded_row_bytes: 100,
        },
        None,
    )
    .await;
    assert_eq!(result.unwrap_err().kind, ErrorKind::ResourceLimit);
    assert_eq!(std::fs::read_to_string(destination).unwrap(), "original");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
}

#[tokio::test]
async fn escape_heavy_row_fails_conservative_preflight_preserving_destination() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("row.json");
    std::fs::write(&path, "original").unwrap();
    let mut source = source();
    source.pages = vec![Ok(ResultPage {
        index: 0,
        rows: vec![vec![Value::Text("\u{0001}".repeat(1000)), Value::Null]],
        has_more: false,
    })]
    .into();
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    let result = export(
        &mut source,
        &mut sink,
        ExportFormat::Json,
        Cancellation::default(),
        Limits {
            page_bytes: 100000,
            value_bytes: 100000,
            encoded_row_bytes: 4000,
        },
        None,
    )
    .await;
    assert_eq!(result.unwrap_err().kind, ErrorKind::ResourceLimit);
    assert_eq!(std::fs::read_to_string(path).unwrap(), "original");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
}

#[tokio::test]
async fn wide_inline_row_streams_cells_within_the_encoding_allowance() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("wide.csv");
    let mut source = source();
    source.pages = vec![Ok(ResultPage {
        index: 0,
        rows: vec![vec![
            Value::Text("x".repeat(1000)),
            Value::Text("y".repeat(1000)),
        ]],
        has_more: false,
    })]
    .into();
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    let report = export(
        &mut source,
        &mut sink,
        ExportFormat::Csv,
        Cancellation::default(),
        Limits {
            page_bytes: 4096,
            value_bytes: 2048,
            encoded_row_bytes: 30000,
        },
        None,
    )
    .await
    .unwrap();
    assert_eq!(report.rows, 1);
    assert_eq!(
        std::fs::read_to_string(path).unwrap(),
        format!(
            "\"duplicate\",\"duplicate\"\r\n\"{}\",\"{}\"\r\n",
            "x".repeat(1000),
            "y".repeat(1000)
        )
    );
}
