use async_trait::async_trait;
use choscordb_driver_api::*;
use choscordb_export::*;
struct Source {
    columns: Vec<Column>,
    bytes: Vec<u8>,
    kind: DeferredKind,
    once: bool,
    cancel: Option<Cancellation>,
    calls: usize,
    malformed: bool,
}
#[async_trait]
impl ExportSource for Source {
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
            rows: vec![vec![Value::Deferred {
                handle: Handle {
                    slot: 0,
                    generation: 1,
                },
                byte_length: self.bytes.len() as u64,
                database_type: "TEXT".into(),
            }]],
            has_more: false,
        }))
    }
    async fn read_value_chunk(&mut self, _: Handle, offset: u64, max: usize) -> Result<ValueChunk> {
        assert!(max <= 1);
        self.calls += 1;
        if self.calls == 2
            && let Some(c) = &self.cancel
        {
            c.cancel();
        }
        Ok(ValueChunk {
            bytes: self.bytes[offset as usize..(offset as usize + max).min(self.bytes.len())]
                .to_vec(),
            offset: offset + u64::from(self.malformed),
            total_bytes: self.bytes.len() as u64,
            kind: self.kind,
        })
    }
}
fn source(bytes: &[u8], kind: DeferredKind) -> Source {
    Source {
        columns: vec![Column {
            name: "v".into(),
            database_type: "TEXT".into(),
            precision: None,
            scale: None,
            timezone: None,
            nullable: None,
        }],
        bytes: bytes.to_vec(),
        kind,
        once: false,
        cancel: None,
        calls: 0,
        malformed: false,
    }
}
fn limits() -> Limits {
    Limits {
        value_bytes: 1,
        ..Limits::default()
    }
}
#[tokio::test]
async fn one_byte_chunks_preserve_utf8_and_escaping_in_all_formats() {
    let text = "é😀\"'\\\n\t";
    for format in [
        ExportFormat::Csv,
        ExportFormat::Json,
        ExportFormat::JsonLines,
        ExportFormat::SqlInsert {
            table: vec!["t".into()],
            dialect: SqlDialect::Sqlite,
        },
        ExportFormat::SqlInsert {
            table: vec!["t".into()],
            dialect: SqlDialect::Postgres,
        },
    ] {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("out");
        let mut src = source(text.as_bytes(), DeferredKind::Text);
        let expected =
            encode_row(&format, &src.columns, &vec![Value::Text(text.into())], true).unwrap();
        let mut sink = FileSink::create(path.clone()).await.unwrap();
        let report = export(
            &mut src,
            &mut sink,
            format.clone(),
            Cancellation::default(),
            limits(),
            None,
        )
        .await
        .unwrap();
        let out = std::fs::read_to_string(path).unwrap();
        assert_eq!(report.bytes, out.len() as u64);
        assert!(out.contains(&expected), "{out:?} lacks {expected:?}");
        assert_eq!(src.calls, text.len());
    }
}
#[tokio::test]
async fn binary_chunks_match_literal_encoding_in_all_formats() {
    for format in [
        ExportFormat::Csv,
        ExportFormat::Json,
        ExportFormat::JsonLines,
        ExportFormat::SqlInsert {
            table: vec!["t".into()],
            dialect: SqlDialect::Sqlite,
        },
        ExportFormat::SqlInsert {
            table: vec!["t".into()],
            dialect: SqlDialect::Postgres,
        },
    ] {
        for bytes in [vec![], vec![0, 255, 34, 128]] {
            let dir = tempfile::tempdir().unwrap();
            let path = dir.path().join("out");
            let mut src = source(&bytes, DeferredKind::Binary);
            let expected =
                encode_row(&format, &src.columns, &vec![Value::Binary(bytes)], true).unwrap();
            let mut sink = FileSink::create(path.clone()).await.unwrap();
            export(
                &mut src,
                &mut sink,
                format.clone(),
                Cancellation::default(),
                limits(),
                None,
            )
            .await
            .unwrap();
            assert!(std::fs::read_to_string(path).unwrap().contains(&expected));
        }
    }
}
#[tokio::test]
async fn invalid_text_and_chunk_identity_abort_without_replacing_destination() {
    for (bytes, malformed) in [
        (vec![0xc3], false),
        (vec![0xc3, 0x20], false),
        (vec![0xff], false),
        (b"ok".to_vec(), true),
    ] {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("out");
        std::fs::write(&path, "original").unwrap();
        let mut src = source(&bytes, DeferredKind::Text);
        src.malformed = malformed;
        let mut sink = FileSink::create(path.clone()).await.unwrap();
        assert_eq!(
            export(
                &mut src,
                &mut sink,
                ExportFormat::Json,
                Cancellation::default(),
                limits(),
                None
            )
            .await
            .unwrap_err()
            .kind,
            ErrorKind::InvalidInput
        );
        assert_eq!(std::fs::read_to_string(path).unwrap(), "original");
        assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
    }
}
#[tokio::test]
async fn cancellation_between_chunks_discards_partial_output() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("out");
    std::fs::write(&path, "original").unwrap();
    let cancel = Cancellation::default();
    let mut src = source(b"many bytes", DeferredKind::Text);
    src.cancel = Some(cancel.clone());
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    assert_eq!(
        export(
            &mut src,
            &mut sink,
            ExportFormat::Csv,
            cancel,
            limits(),
            None
        )
        .await
        .unwrap_err()
        .kind,
        ErrorKind::Cancelled
    );
    assert!(src.calls <= 2);
    assert_eq!(std::fs::read_to_string(path).unwrap(), "original");
    assert_eq!(std::fs::read_dir(dir.path()).unwrap().count(), 1);
}

#[tokio::test]
async fn progress_advances_before_a_large_deferred_row_finishes() {
    struct ProgressSource {
        inner: Source,
        updates: tokio::sync::mpsc::Receiver<Progress>,
        observed: bool,
    }
    #[async_trait]
    impl ExportSource for ProgressSource {
        fn columns(&self) -> &[Column] {
            &self.inner.columns
        }
        async fn next_page(&mut self) -> Result<Option<ResultPage>> {
            self.inner.next_page().await
        }
        async fn read_value_chunk(
            &mut self,
            _: Handle,
            offset: u64,
            max: usize,
        ) -> Result<ValueChunk> {
            assert!(max <= MAX_VALUE_CHUNK_BYTES);
            while let Ok(p) = self.updates.try_recv() {
                if p.bytes >= 65536 && p.rows == 0 && offset < self.inner.bytes.len() as u64 {
                    self.observed = true;
                }
            }
            Ok(ValueChunk {
                bytes: self.inner.bytes
                    [offset as usize..(offset as usize + max).min(self.inner.bytes.len())]
                    .to_vec(),
                offset,
                total_bytes: self.inner.bytes.len() as u64,
                kind: DeferredKind::Binary,
            })
        }
    }
    let (tx, rx) = tokio::sync::mpsc::channel(1);
    let mut src = ProgressSource {
        inner: source(&vec![7; 300000], DeferredKind::Binary),
        updates: rx,
        observed: false,
    };
    let dir = tempfile::tempdir().unwrap();
    let mut sink = FileSink::create(dir.path().join("out")).await.unwrap();
    export(
        &mut src,
        &mut sink,
        ExportFormat::Json,
        Cancellation::default(),
        Limits::default(),
        Some(tx),
    )
    .await
    .unwrap();
    assert!(
        src.observed,
        "must publish bytes while the row is still incomplete"
    );
}

#[tokio::test]
async fn mysql_deferred_text_exports_independent_of_chunk_boundaries() {
    for (bytes, literal) in [
        ("é\\'".as_bytes(), "c3a95c27"),
        (b"".as_slice(), ""),
        (b"a\0b".as_slice(), "610062"),
    ] {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("mysql.sql");
        let mut src = source(bytes, DeferredKind::Text);
        let mut sink = FileSink::create(path.clone()).await.unwrap();
        export(
            &mut src,
            &mut sink,
            ExportFormat::SqlInsert {
                table: vec!["t".into()],
                dialect: SqlDialect::Mysql,
            },
            Cancellation::default(),
            limits(),
            None,
        )
        .await
        .unwrap();
        assert_eq!(
            std::fs::read_to_string(path).unwrap(),
            format!("INSERT INTO `t` (`v`) VALUES (CONVERT(X'{literal}' USING utf8mb4));\n")
        );
    }
}

#[tokio::test]
async fn mysql_export_rejects_invalid_utf8_without_replacing_destination() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("mysql.sql");
    std::fs::write(&path, "original").unwrap();
    let mut src = source(&[0xc3, 0x20], DeferredKind::Text);
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    assert_eq!(
        export(
            &mut src,
            &mut sink,
            ExportFormat::SqlInsert {
                table: vec!["t".into()],
                dialect: SqlDialect::Mysql
            },
            Cancellation::default(),
            limits(),
            None
        )
        .await
        .unwrap_err()
        .kind,
        ErrorKind::InvalidInput
    );
    assert_eq!(std::fs::read_to_string(path).unwrap(), "original");
}
