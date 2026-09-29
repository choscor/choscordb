use async_trait::async_trait;
use choscordb_driver_api::*;
use choscordb_export::*;

struct Source {
    value: Value,
    bytes: Vec<u8>,
    kind: DeferredKind,
    once: bool,
    reads: usize,
}

fn column() -> Column {
    Column {
        name: "v".into(),
        database_type: "mood".into(),
        precision: None,
        scale: None,
        timezone: None,
        nullable: None,
    }
}

#[async_trait]
impl ExportSource for Source {
    fn columns(&self) -> &[Column] {
        static COLUMNS: std::sync::OnceLock<Vec<Column>> = std::sync::OnceLock::new();
        COLUMNS.get_or_init(|| vec![column()])
    }

    async fn next_page(&mut self) -> Result<Option<ResultPage>> {
        if self.once {
            return Ok(None);
        }
        self.once = true;
        Ok(Some(ResultPage {
            index: 0,
            rows: vec![vec![self.value.clone()]],
            has_more: false,
        }))
    }

    async fn read_value_chunk(
        &mut self,
        handle: Handle,
        offset: u64,
        max: usize,
    ) -> Result<ValueChunk> {
        assert_eq!(handle.slot, 7);
        assert!(max <= 2, "export must request bounded chunks");
        self.reads += 1;
        let start = offset as usize;
        Ok(ValueChunk {
            bytes: self.bytes[start..(start + max).min(self.bytes.len())].to_vec(),
            offset,
            total_bytes: self.bytes.len() as u64,
            kind: self.kind,
        })
    }
}

fn source(value: Value, bytes: &[u8]) -> Source {
    Source {
        value,
        bytes: bytes.to_vec(),
        kind: DeferredKind::Text,
        once: false,
        reads: 0,
    }
}

async fn file_export(
    source: &mut Source,
    format: ExportFormat,
    limits: Limits,
) -> (Result<Progress>, String) {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("out");
    std::fs::write(&path, "original").unwrap();
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    let result = export(
        source,
        &mut sink,
        format,
        Cancellation::default(),
        limits,
        None,
    )
    .await;
    let output = std::fs::read_to_string(&path).unwrap();
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
    (result, output)
}

#[tokio::test]
async fn inline_fallback_exports_full_server_text_and_type() {
    let value = Value::FallbackText {
        text: "angry, \"é\"\n".into(),
        database_type: "mood".into(),
    };
    let (result, csv) = file_export(
        &mut source(value.clone(), b""),
        ExportFormat::Csv,
        Limits::default(),
    )
    .await;
    assert_eq!(result.unwrap().rows, 1);
    assert_eq!(csv, "\"v\"\r\n\"angry, \"\"é\"\"\n\"\r\n");
    for format in [ExportFormat::Json, ExportFormat::JsonLines] {
        let (result, output) = file_export(
            &mut source(value.clone(), b""),
            format.clone(),
            Limits::default(),
        )
        .await;
        result.unwrap();
        let json: serde_json::Value = match format {
            ExportFormat::Json => serde_json::from_str(&output).unwrap(),
            _ => serde_json::from_str(output.lines().nth(1).unwrap()).unwrap(),
        };
        let cell = if matches!(format, ExportFormat::Json) {
            &json["rows"][0][0]
        } else {
            &json["row"][0]
        };
        assert_eq!(
            cell,
            &serde_json::json!({"fallback_text":"angry, \"é\"\n", "database_type":"mood"})
        );
    }
}

#[tokio::test]
async fn unavailable_and_sql_fallback_preserve_destination() {
    for (value, format, message) in [
        (
            Value::Unavailable {
                database_type: "mood".into(),
                reason: "output failed".into(),
            },
            ExportFormat::Csv,
            "output failed",
        ),
        (
            Value::Unavailable {
                database_type: "mood".into(),
                reason: "output failed".into(),
            },
            ExportFormat::Json,
            "output failed",
        ),
        (
            Value::Unavailable {
                database_type: "mood".into(),
                reason: "output failed".into(),
            },
            ExportFormat::JsonLines,
            "output failed",
        ),
        (
            Value::Unavailable {
                database_type: "mood".into(),
                reason: "output failed".into(),
            },
            ExportFormat::SqlInsert {
                table: vec!["t".into()],
                dialect: SqlDialect::Postgres,
            },
            "output failed",
        ),
        (
            Value::FallbackText {
                text: "happy".into(),
                database_type: "mood".into(),
            },
            ExportFormat::SqlInsert {
                table: vec!["t".into()],
                dialect: SqlDialect::Postgres,
            },
            "fallback",
        ),
    ] {
        let (result, output) =
            file_export(&mut source(value, b""), format, Limits::default()).await;
        let error = result.unwrap_err();
        assert_eq!(error.kind, ErrorKind::InvalidInput);
        assert!(error.message.to_lowercase().contains(message), "{error:?}");
        assert_eq!(output, "original");
    }
}

#[tokio::test]
async fn deferred_fallback_streams_complete_text_and_keeps_type_in_json() {
    let text = "é\"x".repeat(200);
    let deferred = Value::DeferredFallback {
        handle: Handle {
            slot: 7,
            generation: 1,
        },
        byte_length: text.len() as u64,
        database_type: "mood".into(),
    };
    let limits = Limits {
        value_bytes: 2,
        ..Limits::default()
    };
    let mut csv_source = source(deferred.clone(), text.as_bytes());
    let (result, csv) = file_export(&mut csv_source, ExportFormat::Csv, limits).await;
    result.unwrap();
    let mut reader = csv::Reader::from_reader(csv.as_bytes());
    assert_eq!(
        reader.records().next().unwrap().unwrap().get(0),
        Some(text.as_str())
    );
    assert!(csv_source.reads > 200);
    for format in [ExportFormat::Json, ExportFormat::JsonLines] {
        let mut src = source(deferred.clone(), text.as_bytes());
        let (result, output) = file_export(&mut src, format.clone(), limits).await;
        result.unwrap();
        let json: serde_json::Value = match format {
            ExportFormat::Json => serde_json::from_str(&output).unwrap(),
            _ => serde_json::from_str(output.lines().nth(1).unwrap()).unwrap(),
        };
        let cell = if matches!(format, ExportFormat::Json) {
            &json["rows"][0][0]
        } else {
            &json["row"][0]
        };
        assert_eq!(
            cell,
            &serde_json::json!({"fallback_text":text, "database_type":"mood"})
        );
        assert!(src.reads > 200);
    }
    let (result, output) = file_export(
        &mut source(deferred, text.as_bytes()),
        ExportFormat::SqlInsert {
            table: vec!["t".into()],
            dialect: SqlDialect::Postgres,
        },
        limits,
    )
    .await;
    assert_eq!(result.unwrap_err().kind, ErrorKind::InvalidInput);
    assert_eq!(output, "original");
}

#[tokio::test]
async fn deferred_fallback_rejects_binary_chunks_without_replacing_destination() {
    let value = Value::DeferredFallback {
        handle: Handle {
            slot: 7,
            generation: 1,
        },
        byte_length: 2,
        database_type: "mood".into(),
    };
    let mut src = source(value, b"ok");
    src.kind = DeferredKind::Binary;
    let (result, output) = file_export(
        &mut src,
        ExportFormat::Json,
        Limits {
            value_bytes: 2,
            ..Limits::default()
        },
    )
    .await;
    assert_eq!(result.unwrap_err().kind, ErrorKind::InvalidInput);
    assert_eq!(output, "original");
}

#[tokio::test]
async fn deferred_fallback_type_metadata_respects_encoding_budget() {
    let value = Value::DeferredFallback {
        handle: Handle {
            slot: 7,
            generation: 1,
        },
        byte_length: 2,
        database_type: "q".repeat(100),
    };
    let mut src = source(value, b"ok");
    let limits = Limits {
        encoded_row_bytes: 1800,
        value_bytes: 2,
        ..Limits::default()
    };
    let (result, output) = file_export(&mut src, ExportFormat::Json, limits).await;
    assert_eq!(result.unwrap_err().kind, ErrorKind::ResourceLimit);
    assert_eq!(src.reads, 0);
    assert_eq!(output, "original");
}
