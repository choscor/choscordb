use choscordb_driver_api::{
    DriverError, EditStatement, EditTarget, ErrorKind, GridEditDriver, ImportColumn,
    ImportConflict, ObjectId, RowImportError, create_import_table_sql, drop_import_table_sql,
    inserts_allowed, row_import_sql, row_import_statement,
};
use serde::de::{
    Deserialize, DeserializeSeed, Deserializer, IgnoredAny, MapAccess, SeqAccess, Visitor,
};
use std::collections::HashSet;
use std::io::{BufRead, BufReader, Read};
use std::path::PathBuf;

pub const MAX_IMPORT_FILE_BYTES: u64 = 64 * 1024 * 1024;
pub const MAX_IMPORT_ROWS: usize = 100_000;
pub const IMPORT_PREVIEW_ROWS: usize = 100;
pub const MAX_IMPORT_COLUMNS: usize = 1_000;
pub const IMPORT_NEW_COLUMN_TYPE: &str = "TEXT";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImportEncoding {
    Utf8,
    Latin1,
}

impl ImportEncoding {
    pub const ALL: [Self; 2] = [Self::Utf8, Self::Latin1];

    pub fn code(self) -> &'static str {
        match self {
            Self::Utf8 => "utf-8",
            Self::Latin1 => "latin-1",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Utf8 => "UTF-8",
            Self::Latin1 => "Latin-1 (ISO-8859-1)",
        }
    }

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL
            .into_iter()
            .find(|encoding| encoding.code() == code)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CsvReadOptions {
    pub delimiter: u8,
    pub quote: u8,
    pub header: bool,
    pub null_text: String,
}

impl Default for CsvReadOptions {
    fn default() -> Self {
        Self {
            delimiter: b',',
            quote: b'"',
            header: true,
            null_text: String::new(),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ImportFileFormat {
    Csv(CsvReadOptions),
    Json,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ImportFile {
    pub path: PathBuf,
    pub format: ImportFileFormat,
    pub encoding: ImportEncoding,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImportRead {
    Preview,
    All,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ImportData {
    pub columns: Vec<String>,
    pub rows: Vec<Vec<Option<String>>>,
}

fn invalid(message: impl Into<String>) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message)
}

fn row_error(index: usize, message: String) -> DriverError {
    invalid(message).at_statement(index)
}

const UTF8_ERROR: &str = "The file is not valid UTF-8. Choose the encoding of the file.";

fn io(error: std::io::Error) -> DriverError {
    DriverError::new(ErrorKind::Io, error.to_string())
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImportFlow {
    Continue,
    Stop,
}

pub type ImportRowSink<'a> = dyn FnMut(Vec<Option<String>>) -> Result<ImportFlow, DriverError> + 'a;

struct Latin1Reader<R> {
    source: R,
    pending: std::collections::VecDeque<u8>,
}

impl<R: Read> Read for Latin1Reader<R> {
    fn read(&mut self, out: &mut [u8]) -> std::io::Result<usize> {
        if self.pending.is_empty() {
            let mut bytes = [0_u8; 8192];
            let count = self.source.read(&mut bytes)?;
            let mut encoded = [0_u8; 2];
            for byte in &bytes[..count] {
                self.pending
                    .extend(char::from(*byte).encode_utf8(&mut encoded).as_bytes());
            }
        }
        let count = out.len().min(self.pending.len());
        for (slot, byte) in out.iter_mut().zip(self.pending.drain(..count)) {
            *slot = byte;
        }
        Ok(count)
    }
}

fn check_utf8(path: &std::path::Path) -> Result<(), DriverError> {
    let mut reader = BufReader::new(std::fs::File::open(path).map_err(io)?);
    let mut carry = Vec::new();
    loop {
        let buffer = reader.fill_buf().map_err(io)?;
        if buffer.is_empty() {
            break;
        }
        carry.extend_from_slice(buffer);
        let consumed = buffer.len();
        reader.consume(consumed);
        match std::str::from_utf8(&carry) {
            Ok(_) => carry.clear(),
            Err(error) if error.error_len().is_none() => {
                carry.drain(..error.valid_up_to());
            }
            Err(_) => return Err(invalid(UTF8_ERROR)),
        }
    }
    if carry.is_empty() {
        Ok(())
    } else {
        Err(invalid(UTF8_ERROR))
    }
}

fn open_text(file: &ImportFile) -> Result<Box<dyn BufRead>, DriverError> {
    let size = std::fs::metadata(&file.path).map_err(io)?.len();
    if size > MAX_IMPORT_FILE_BYTES {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "The file is larger than 64 MiB. Split the file and import each part.",
        ));
    }
    let source = std::fs::File::open(&file.path).map_err(io)?;
    match file.encoding {
        ImportEncoding::Utf8 => {
            check_utf8(&file.path)?;
            let mut reader = BufReader::new(source);
            if reader.fill_buf().map_err(io)?.starts_with(b"\xEF\xBB\xBF") {
                reader.consume(3);
            }
            Ok(Box::new(reader))
        }
        ImportEncoding::Latin1 => Ok(Box::new(BufReader::new(Latin1Reader {
            source: BufReader::new(source),
            pending: std::collections::VecDeque::new(),
        }))),
    }
}

fn row_limit(rows: usize, read: ImportRead) -> Result<bool, DriverError> {
    match read {
        ImportRead::Preview => Ok(rows >= IMPORT_PREVIEW_ROWS),
        ImportRead::All if rows > MAX_IMPORT_ROWS => Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "The file has more than 100,000 rows. Split the file and import each part.",
        )),
        ImportRead::All => Ok(false),
    }
}

fn column_limit(columns: usize) -> Result<(), DriverError> {
    if columns > MAX_IMPORT_COLUMNS {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "The file has more than 1,000 columns.",
        ));
    }
    Ok(())
}

fn read_csv(
    reader: Box<dyn BufRead>,
    options: &CsvReadOptions,
    sink: &mut ImportRowSink<'_>,
) -> Result<Vec<String>, DriverError> {
    if options.delimiter == options.quote {
        return Err(invalid("The delimiter and the quote must be different."));
    }
    let mut reader = csv::ReaderBuilder::new()
        .delimiter(options.delimiter)
        .quote(options.quote)
        .has_headers(false)
        .flexible(true)
        .from_reader(reader);
    let mut columns = Vec::new();
    let mut width = None;
    for (index, record) in reader.records().enumerate() {
        let record =
            record.map_err(|error| invalid(format!("The CSV file is not valid: {error}")))?;
        let fields: Vec<&str> = record.iter().collect();
        let Some(expected) = width else {
            column_limit(fields.len())?;
            width = Some(fields.len());
            columns = (1..=fields.len())
                .map(|position| match fields[position - 1].trim() {
                    name if options.header && !name.is_empty() => name.to_owned(),
                    _ => format!("column_{position}"),
                })
                .collect();
            if !options.header && sink(csv_row(&fields, options))? == ImportFlow::Stop {
                break;
            }
            continue;
        };
        let row = if options.header { index } else { index + 1 };
        if fields.len() != expected {
            return Err(row_error(
                row - 1,
                format!(
                    "Row {row} has {} fields. The first row has {expected} fields.",
                    fields.len()
                ),
            ));
        }
        if sink(csv_row(&fields, options))? == ImportFlow::Stop {
            break;
        }
    }
    Ok(columns)
}

fn csv_row(fields: &[&str], options: &CsvReadOptions) -> Vec<Option<String>> {
    fields
        .iter()
        .map(|field| (*field != options.null_text).then(|| (*field).to_owned()))
        .collect()
}

enum JsonRow {
    Object(Vec<(String, serde_json::Value)>),
    Other,
}

impl<'de> Deserialize<'de> for JsonRow {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        struct RowVisitor;
        impl<'de> Visitor<'de> for RowVisitor {
            type Value = JsonRow;
            fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
                formatter.write_str("a JSON value")
            }
            fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<JsonRow, A::Error> {
                let mut entries = Vec::new();
                while let Some(entry) = map.next_entry()? {
                    entries.push(entry);
                }
                Ok(JsonRow::Object(entries))
            }
            fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<JsonRow, A::Error> {
                while seq.next_element::<IgnoredAny>()?.is_some() {}
                Ok(JsonRow::Other)
            }
            fn visit_bool<E>(self, _: bool) -> Result<JsonRow, E> {
                Ok(JsonRow::Other)
            }
            fn visit_i64<E>(self, _: i64) -> Result<JsonRow, E> {
                Ok(JsonRow::Other)
            }
            fn visit_u64<E>(self, _: u64) -> Result<JsonRow, E> {
                Ok(JsonRow::Other)
            }
            fn visit_f64<E>(self, _: f64) -> Result<JsonRow, E> {
                Ok(JsonRow::Other)
            }
            fn visit_str<E>(self, _: &str) -> Result<JsonRow, E> {
                Ok(JsonRow::Other)
            }
            fn visit_unit<E>(self) -> Result<JsonRow, E> {
                Ok(JsonRow::Other)
            }
        }
        deserializer.deserialize_any(RowVisitor)
    }
}

fn json_text(value: serde_json::Value) -> Option<String> {
    match value {
        serde_json::Value::Null => None,
        serde_json::Value::String(text) => Some(text),
        other => Some(other.to_string()),
    }
}

#[derive(Default)]
struct JsonColumns {
    names: Vec<String>,
    positions: std::collections::HashMap<String, usize>,
}

impl JsonColumns {
    fn row(&mut self, index: usize, row: JsonRow) -> Result<Vec<Option<String>>, DriverError> {
        let JsonRow::Object(fields) = row else {
            return Err(row_error(
                index,
                format!("Row {} is not a JSON object.", index + 1),
            ));
        };
        for (name, _) in &fields {
            if !self.positions.contains_key(name) {
                self.positions.insert(name.clone(), self.names.len());
                self.names.push(name.clone());
                column_limit(self.names.len())?;
            }
        }
        let mut values = vec![None; self.names.len()];
        for (name, value) in fields {
            values[self.positions[&name]] = json_text(value);
        }
        Ok(values)
    }
}

struct JsonArrayRows<'s, 'a> {
    columns: &'s mut JsonColumns,
    sink: &'s mut ImportRowSink<'a>,
    outcome: &'s mut Option<Result<(), DriverError>>,
    read: &'s mut usize,
}

impl<'de> DeserializeSeed<'de> for JsonArrayRows<'_, '_> {
    type Value = ();
    fn deserialize<D: Deserializer<'de>>(self, deserializer: D) -> Result<(), D::Error> {
        deserializer.deserialize_seq(self)
    }
}

impl<'de> Visitor<'de> for JsonArrayRows<'_, '_> {
    type Value = ();
    fn expecting(&self, formatter: &mut std::fmt::Formatter) -> std::fmt::Result {
        formatter.write_str("a JSON array")
    }
    fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<(), A::Error> {
        let mut index = 0;
        while let Some(row) = seq.next_element::<JsonRow>()? {
            let flow = self
                .columns
                .row(index, row)
                .and_then(|values| (self.sink)(values));
            index += 1;
            *self.read = index;
            match flow {
                Ok(ImportFlow::Continue) => {}
                Ok(ImportFlow::Stop) => {
                    *self.outcome = Some(Ok(()));
                    return Err(serde::de::Error::custom("stopped"));
                }
                Err(error) => {
                    *self.outcome = Some(Err(error));
                    return Err(serde::de::Error::custom("stopped"));
                }
            }
        }
        Ok(())
    }
}

fn read_json(
    mut reader: Box<dyn BufRead>,
    sink: &mut ImportRowSink<'_>,
) -> Result<Vec<String>, DriverError> {
    let mut columns = JsonColumns::default();
    let array = loop {
        let buffer = reader.fill_buf().map_err(io)?;
        let Some(&first) = buffer.first() else {
            break false;
        };
        if first.is_ascii_whitespace() {
            reader.consume(1);
            continue;
        }
        break first == b'[';
    };
    if array {
        let mut outcome = None;
        let mut read = 0;
        let mut deserializer = serde_json::Deserializer::from_reader(reader);
        let parsed = JsonArrayRows {
            columns: &mut columns,
            sink,
            outcome: &mut outcome,
            read: &mut read,
        }
        .deserialize(&mut deserializer)
        .and_then(|()| deserializer.end());
        match (outcome, parsed) {
            (Some(stopped), _) => stopped?,
            (None, parsed) => parsed
                .map_err(|error| row_error(read, format!("The JSON file is not valid: {error}")))?,
        }
        return Ok(columns.names);
    }
    let mut line = String::new();
    let mut index = 0;
    loop {
        line.clear();
        if reader.read_line(&mut line).map_err(io)? == 0 {
            break;
        }
        if line.trim().is_empty() {
            continue;
        }
        let row: JsonRow = serde_json::from_str(&line).map_err(|error| {
            row_error(
                index,
                format!("Row {} is not valid JSON: {error}", index + 1),
            )
        })?;
        let values = columns.row(index, row)?;
        index += 1;
        if sink(values)? == ImportFlow::Stop {
            break;
        }
    }
    Ok(columns.names)
}

pub fn read_import_rows(
    file: &ImportFile,
    sink: &mut ImportRowSink<'_>,
) -> Result<Vec<String>, DriverError> {
    let reader = open_text(file)?;
    match &file.format {
        ImportFileFormat::Csv(options) => read_csv(reader, options, sink),
        ImportFileFormat::Json => read_json(reader, sink),
    }
}

pub fn read_import_file(file: &ImportFile, read: ImportRead) -> Result<ImportData, DriverError> {
    let mut rows = Vec::new();
    let columns = read_import_rows(file, &mut |row| {
        if row_limit(rows.len(), read)? {
            return Ok(ImportFlow::Stop);
        }
        rows.push(row);
        Ok(ImportFlow::Continue)
    })?;
    row_limit(rows.len(), read)?;
    for row in &mut rows {
        row.resize(columns.len(), None);
    }
    Ok(ImportData { columns, rows })
}

pub fn count_import_rows(file: &ImportFile) -> Result<u64, DriverError> {
    let mut rows = 0_usize;
    read_import_rows(file, &mut |_| {
        rows += 1;
        row_limit(rows, ImportRead::All)?;
        Ok(ImportFlow::Continue)
    })?;
    Ok(rows as u64)
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TableImportTarget {
    Existing { object: ObjectId },
    New { qualified_name: String },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ImportMapping {
    pub target: String,
    pub source: usize,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TableImportRequest {
    pub driver: GridEditDriver,
    pub file: ImportFile,
    pub target: TableImportTarget,
    pub mapping: Vec<ImportMapping>,
    pub conflict: ImportConflict,
}

#[derive(Clone, Debug, Default)]
pub struct PlannedImport {
    pub insert: String,
    pub create: Option<String>,
    pub drop: Option<String>,
    columns: Vec<ImportColumn>,
    sources: Vec<usize>,
}

impl PlannedImport {
    pub fn statement(&self, row: &[Option<String>]) -> Result<EditStatement, DriverError> {
        let values = self
            .sources
            .iter()
            .map(|source| row.get(*source).cloned().flatten())
            .collect();
        row_import_statement(&self.insert, &self.columns, values).map_err(rule)
    }
}

fn rule(error: RowImportError) -> DriverError {
    invalid(error.to_string())
}

pub fn table_import_mapping_error(mapping: &[ImportMapping]) -> Option<String> {
    if mapping.is_empty() {
        return Some(RowImportError::NoColumns.to_string());
    }
    if mapping.iter().any(|entry| entry.target.trim().is_empty()) {
        return Some("Enter a name for each imported column.".into());
    }
    let mut seen = HashSet::new();
    mapping
        .iter()
        .find(|entry| !seen.insert(entry.target.as_str()))
        .map(|entry| {
            format!(
                "The column {} receives more than one file column.",
                entry.target
            )
        })
}

fn existing_columns(
    target: &EditTarget,
    mapping: &[ImportMapping],
) -> Result<Vec<ImportColumn>, DriverError> {
    if !inserts_allowed(&target.reason) {
        return Err(invalid(target.reason.clone()));
    }
    mapping
        .iter()
        .map(|entry| {
            let column = target
                .columns
                .iter()
                .find(|column| column.name == entry.target)
                .ok_or_else(|| invalid(format!("The table has no column {}.", entry.target)))?;
            if column.generated {
                return Err(invalid(format!(
                    "The column {} is generated and cannot receive data.",
                    entry.target
                )));
            }
            Ok(ImportColumn {
                name: column.name.clone(),
                database_type: column.database_type.clone(),
                key: column.key,
            })
        })
        .collect()
}

pub fn plan_table_import(
    request: &TableImportRequest,
    target: Option<&EditTarget>,
    file_columns: &[String],
) -> Result<PlannedImport, DriverError> {
    if let Some(message) = table_import_mapping_error(&request.mapping) {
        return Err(invalid(message));
    }
    if let Some(entry) = request
        .mapping
        .iter()
        .find(|entry| entry.source >= file_columns.len())
    {
        return Err(invalid(format!(
            "The file has no column {}.",
            entry.source + 1
        )));
    }
    let mut planned = PlannedImport::default();
    let (qualified_name, columns, keys) = match (&request.target, target) {
        (TableImportTarget::Existing { .. }, Some(target)) => (
            target.qualified_name.clone(),
            existing_columns(target, &request.mapping)?,
            target.key_columns.clone(),
        ),
        (TableImportTarget::Existing { .. }, None) => {
            return Err(DriverError::new(
                ErrorKind::Internal,
                "The import table was not inspected.",
            ));
        }
        (TableImportTarget::New { qualified_name }, _) => {
            let columns: Vec<ImportColumn> = request
                .mapping
                .iter()
                .map(|entry| ImportColumn {
                    name: entry.target.clone(),
                    database_type: IMPORT_NEW_COLUMN_TYPE.into(),
                    key: false,
                })
                .collect();
            planned.create = Some(
                create_import_table_sql(request.driver, qualified_name, &columns).map_err(rule)?,
            );
            planned.drop =
                Some(drop_import_table_sql(request.driver, qualified_name).map_err(rule)?);
            (qualified_name.clone(), columns, Vec::new())
        }
    };
    let sql = row_import_sql(
        request.driver,
        &qualified_name,
        &columns,
        &keys,
        request.conflict,
    )
    .map_err(rule)?;
    planned.insert = sql;
    planned.columns = columns;
    planned.sources = request.mapping.iter().map(|entry| entry.source).collect();
    Ok(planned)
}

fn plain_table(kind: &str, relation_subtype: &str) -> bool {
    kind == "table" && relation_subtype.is_empty()
}

pub fn table_import_unavailable(kind: &str, relation_subtype: &str) -> Option<&'static str> {
    (!plain_table(kind, relation_subtype)).then_some("Only tables can receive imported rows.")
}

pub fn structure_edit_unavailable(kind: &str, relation_subtype: &str) -> Option<&'static str> {
    (!plain_table(kind, relation_subtype)).then_some("Only tables have a structure to edit.")
}

pub fn import_failure_row(error: &DriverError) -> Option<u64> {
    error.failed_statement.map(|index| index.saturating_add(1))
}
