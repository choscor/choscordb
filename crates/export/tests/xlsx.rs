use async_trait::async_trait;
use choscordb_driver_api::*;
use choscordb_export::*;
use std::{collections::BTreeMap, io::Read};

fn column(name: &str) -> Column {
    Column {
        name: name.into(),
        database_type: "TEXT".into(),
        precision: None,
        scale: None,
        timezone: None,
        nullable: None,
    }
}

struct Source {
    columns: Vec<Column>,
    pages: Vec<Vec<Row>>,
    deferred: Vec<u8>,
    kind: DeferredKind,
}

#[async_trait]
impl ExportSource for Source {
    fn columns(&self) -> &[Column] {
        &self.columns
    }
    async fn next_page(&mut self) -> Result<Option<ResultPage>> {
        if self.pages.is_empty() {
            return Ok(None);
        }
        Ok(Some(ResultPage {
            index: 0,
            rows: self.pages.remove(0),
            has_more: !self.pages.is_empty(),
        }))
    }
    async fn read_value_chunk(&mut self, _: Handle, offset: u64, max: usize) -> Result<ValueChunk> {
        let start = offset as usize;
        let end = (start + max.min(1)).min(self.deferred.len());
        Ok(ValueChunk {
            bytes: self.deferred[start..end].to_vec(),
            offset,
            total_bytes: self.deferred.len() as u64,
            kind: self.kind,
        })
    }
}

fn u16_at(bytes: &[u8], at: usize) -> usize {
    u16::from_le_bytes([bytes[at], bytes[at + 1]]) as usize
}

fn u32_at(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
}

fn unzip(bytes: &[u8]) -> BTreeMap<String, String> {
    let end = bytes.len() - 22;
    assert_eq!(u32_at(bytes, end), 0x0605_4b50);
    let count = u16_at(bytes, end + 10);
    let mut at = u32_at(bytes, end + 16) as usize;
    let mut parts = BTreeMap::new();
    for _ in 0..count {
        assert_eq!(u32_at(bytes, at), 0x0201_4b50);
        let method = u16_at(bytes, at + 10);
        let crc = u32_at(bytes, at + 16);
        let compressed = u32_at(bytes, at + 20) as usize;
        let size = u32_at(bytes, at + 24) as usize;
        let name_len = u16_at(bytes, at + 28);
        let local = u32_at(bytes, at + 42) as usize;
        let name = String::from_utf8(bytes[at + 46..at + 46 + name_len].to_vec()).unwrap();
        at += 46 + name_len;
        assert_eq!(u32_at(bytes, local), 0x0403_4b50);
        let data_start = local + 30 + u16_at(bytes, local + 26) + u16_at(bytes, local + 28);
        let data = &bytes[data_start..data_start + compressed];
        let mut text = String::new();
        match method {
            0 => text = String::from_utf8(data.to_vec()).unwrap(),
            8 => {
                flate2::read::DeflateDecoder::new(data)
                    .read_to_string(&mut text)
                    .unwrap();
            }
            other => panic!("method {other}"),
        }
        assert_eq!(text.len(), size);
        assert_eq!(crc32fast::hash(text.as_bytes()), crc);
        parts.insert(name, text);
    }
    parts
}

async fn export_to_file(
    mut source: Source,
) -> std::result::Result<BTreeMap<String, String>, DriverError> {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("result.xlsx");
    let mut sink = FileSink::create(path.clone()).await.unwrap();
    let limits = Limits::default();
    let result = export(
        &mut source,
        &mut sink,
        ExportFormat::Xlsx,
        Cancellation::default(),
        limits,
        None,
    )
    .await;
    match result {
        Ok(_) => Ok(unzip(&std::fs::read(&path).unwrap())),
        Err(error) => {
            assert!(!path.exists());
            Err(error)
        }
    }
}

fn source(columns: &[&str], rows: Vec<Row>) -> Source {
    Source {
        columns: columns.iter().map(|name| column(name)).collect(),
        pages: vec![rows],
        deferred: Vec::new(),
        kind: DeferredKind::Text,
    }
}

const TEXT: &str = "<c t=\"inlineStr\"><is><t xml:space=\"preserve\">";

#[tokio::test]
async fn a_workbook_has_one_sheet_with_header_numbers_booleans_and_text() {
    let parts = export_to_file(source(
        &["id", "a&b"],
        vec![
            vec![
                Value::Integer(7),
                Value::Text("x<y>\r\n_x0041_ \u{1}".into()),
            ],
            vec![Value::Bool(true), Value::Null],
            vec![
                Value::Decimal("12.50".into()),
                Value::Decimal("12345678901234567890.1".into()),
            ],
            vec![Value::Integer(9_007_199_254_740_993), Value::Real(1.5)],
            vec![Value::Binary(vec![1, 255]), Value::Real(f64::NAN)],
        ],
    ))
    .await
    .unwrap();
    assert_eq!(
        parts.keys().collect::<Vec<_>>(),
        [
            "[Content_Types].xml",
            "_rels/.rels",
            "xl/_rels/workbook.xml.rels",
            "xl/workbook.xml",
            "xl/worksheets/sheet1.xml"
        ]
    );
    let sheet = &parts["xl/worksheets/sheet1.xml"];
    let data = sheet.split("<sheetData>").nth(1).unwrap();
    assert_eq!(
        data,
        format!(
            "<row>{TEXT}id</t></is></c>{TEXT}a&amp;b</t></is></c></row>\
             <row><c><v>7</v></c>{TEXT}x&lt;y&gt;&#13;\n_x005F_x0041_ _x0001_</t></is></c></row>\
             <row><c t=\"b\"><v>1</v></c><c/></row>\
             <row><c><v>12.50</v></c>{TEXT}12345678901234567890.1</t></is></c></row>\
             <row>{TEXT}9007199254740993</t></is></c><c><v>1.5</v></c></row>\
             <row>{TEXT}\\x01ff</t></is></c>{TEXT}NaN</t></is></c></row>\
             </sheetData></worksheet>"
        )
    );
}

#[tokio::test]
async fn deferred_text_and_binary_stream_into_cells_with_the_same_escaping() {
    for (bytes, kind, expected) in [
        (
            "é_x0041_&\u{1}".as_bytes().to_vec(),
            DeferredKind::Text,
            "é_x005F_x0041_&amp;_x0001_",
        ),
        (vec![0, 255], DeferredKind::Binary, "\\x00ff"),
    ] {
        let mut input = source(&["v"], Vec::new());
        input.pages = vec![vec![vec![Value::Deferred {
            handle: Handle {
                slot: 0,
                generation: 1,
            },
            byte_length: bytes.len() as u64,
            database_type: "TEXT".into(),
        }]]];
        input.deferred = bytes;
        input.kind = kind;
        let sheet = export_to_file(input).await.unwrap()["xl/worksheets/sheet1.xml"].clone();
        assert!(
            sheet.contains(&format!("<row>{TEXT}{expected}</t></is></c></row>")),
            "{sheet}"
        );
    }
}

#[tokio::test]
async fn excel_limits_fail_the_export_and_keep_no_file() {
    let long = "a".repeat(XLSX_MAX_CELL_CHARS as usize + 1);
    let error = export_to_file(source(&["v"], vec![vec![Value::Text(long)]]))
        .await
        .unwrap_err();
    assert_eq!(
        error.message,
        "An Excel cell holds at most 32,767 characters."
    );
    let exact = "a".repeat(XLSX_MAX_CELL_CHARS as usize);
    assert!(
        export_to_file(source(&["v"], vec![vec![Value::Text(exact)]]))
            .await
            .is_ok()
    );
    let names: Vec<String> = (0..=XLSX_MAX_COLUMNS).map(|i| format!("c{i}")).collect();
    let names: Vec<&str> = names.iter().map(String::as_str).collect();
    let error = export_to_file(source(&names, Vec::new()))
        .await
        .unwrap_err();
    assert_eq!(
        error.message,
        "An Excel sheet holds at most 16,384 columns."
    );
    let rows = |count: usize| {
        let mut input = source(&["v"], Vec::new());
        input.pages = (0..count)
            .collect::<Vec<_>>()
            .chunks(100_000)
            .map(|chunk| vec![vec![Value::Null]; chunk.len()])
            .collect();
        input
    };
    let full = export_to_file(rows(XLSX_MAX_ROWS as usize - 1))
        .await
        .unwrap();
    assert_eq!(
        full["xl/worksheets/sheet1.xml"].matches("<row>").count(),
        XLSX_MAX_ROWS as usize
    );
    let many = rows(XLSX_MAX_ROWS as usize);
    let error = export_to_file(many).await.unwrap_err();
    assert_eq!(
        error.message,
        "An Excel sheet holds at most 1,048,576 rows, including the header row."
    );
}
