use choscordb_driver_api::{
    DriverError, EditBatch, EditTarget, ErrorKind, GridEditDriver, ImportColumn, ImportConflict,
    ObjectId, RowImportError, create_import_table_sql, drop_import_table_sql, inserts_allowed,
    row_import_sql, row_import_statement,
};
use serde::de::{Deserialize, Deserializer, IgnoredAny, MapAccess, SeqAccess, Visitor};
use std::collections::HashSet;
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

fn decode(bytes: Vec<u8>, encoding: ImportEncoding) -> Result<String, DriverError> {
    let text = match encoding {
        ImportEncoding::Utf8 => String::from_utf8(bytes).map_err(|_| {
            invalid("The file is not valid UTF-8. Choose the encoding of the file.")
        })?,
        ImportEncoding::Latin1 => bytes.into_iter().map(char::from).collect(),
    };
    Ok(text
        .strip_prefix('\u{feff}')
        .map(str::to_owned)
        .unwrap_or(text))
}

fn read_text(file: &ImportFile) -> Result<String, DriverError> {
    let io = |error: std::io::Error| DriverError::new(ErrorKind::Io, error.to_string());
    let size = std::fs::metadata(&file.path).map_err(io)?.len();
    if size > MAX_IMPORT_FILE_BYTES {
        return Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "The file is larger than 64 MiB. Split the file and import each part.",
        ));
    }
    decode(std::fs::read(&file.path).map_err(io)?, file.encoding)
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
    text: &str,
    options: &CsvReadOptions,
    read: ImportRead,
) -> Result<ImportData, DriverError> {
    if options.delimiter == options.quote {
        return Err(invalid("The delimiter and the quote must be different."));
    }
    let mut reader = csv::ReaderBuilder::new()
        .delimiter(options.delimiter)
        .quote(options.quote)
        .has_headers(false)
        .flexible(true)
        .from_reader(text.as_bytes());
    let mut data = ImportData::default();
    let mut width = None;
    for (index, record) in reader.records().enumerate() {
        let record =
            record.map_err(|error| invalid(format!("The CSV file is not valid: {error}")))?;
        let fields: Vec<&str> = record.iter().collect();
        let Some(expected) = width else {
            column_limit(fields.len())?;
            width = Some(fields.len());
            data.columns = (1..=fields.len())
                .map(|position| match fields[position - 1].trim() {
                    name if options.header && !name.is_empty() => name.to_owned(),
                    _ => format!("column_{position}"),
                })
                .collect();
            if options.header {
                continue;
            }
            data.rows.push(csv_row(&fields, options));
            continue;
        };
        let row = if options.header { index } else { index + 1 };
        if fields.len() != expected {
            return Err(invalid(format!(
                "Row {row} has {} fields. The first row has {expected} fields.",
                fields.len()
            )));
        }
        if row_limit(data.rows.len(), read)? {
            break;
        }
        data.rows.push(csv_row(&fields, options));
    }
    row_limit(data.rows.len(), read)?;
    Ok(data)
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

fn read_json(text: &str, read: ImportRead) -> Result<ImportData, DriverError> {
    let rows: Vec<JsonRow> = if text.trim_start().starts_with('[') {
        serde_json::from_str(text)
            .map_err(|error| invalid(format!("The JSON file is not valid: {error}")))?
    } else {
        text.lines()
            .filter(|line| !line.trim().is_empty())
            .enumerate()
            .map(|(index, line)| {
                serde_json::from_str(line).map_err(|error| {
                    invalid(format!("Row {} is not valid JSON: {error}", index + 1))
                })
            })
            .collect::<Result<_, _>>()?
    };
    let mut data = ImportData::default();
    let mut positions = std::collections::HashMap::new();
    let mut entries = Vec::new();
    for (index, row) in rows.into_iter().enumerate() {
        if row_limit(entries.len(), read)? {
            break;
        }
        let JsonRow::Object(fields) = row else {
            return Err(invalid(format!("Row {} is not a JSON object.", index + 1)));
        };
        for (name, _) in &fields {
            if !positions.contains_key(name) {
                positions.insert(name.clone(), data.columns.len());
                data.columns.push(name.clone());
                column_limit(data.columns.len())?;
            }
        }
        entries.push(fields);
    }
    row_limit(entries.len(), read)?;
    data.rows = entries
        .into_iter()
        .map(|fields| {
            let mut row = vec![None; data.columns.len()];
            for (name, value) in fields {
                row[positions[&name]] = json_text(value);
            }
            row
        })
        .collect();
    Ok(data)
}

pub fn read_import_file(file: &ImportFile, read: ImportRead) -> Result<ImportData, DriverError> {
    let text = read_text(file)?;
    match &file.format {
        ImportFileFormat::Csv(options) => read_csv(&text, options, read),
        ImportFileFormat::Json => read_json(&text, read),
    }
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
    pub batch: EditBatch,
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
    data: ImportData,
) -> Result<PlannedImport, DriverError> {
    if let Some(message) = table_import_mapping_error(&request.mapping) {
        return Err(invalid(message));
    }
    if let Some(entry) = request
        .mapping
        .iter()
        .find(|entry| entry.source >= data.columns.len())
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
    planned.insert = sql.clone();
    planned.batch.statements = data
        .rows
        .into_iter()
        .map(|row| {
            let values = request
                .mapping
                .iter()
                .map(|entry| row[entry.source].clone())
                .collect();
            row_import_statement(&sql, &columns, values).map_err(rule)
        })
        .collect::<Result<_, _>>()?;
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
