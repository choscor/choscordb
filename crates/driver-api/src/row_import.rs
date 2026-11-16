use crate::grid_edit::{identifier_delimiter, placeholder, quoted, typed_text, valid_qualified};
use crate::{EditStatement, GridEditDriver, Value};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImportConflict {
    Stop,
    Skip,
    Update,
    Replace,
}

impl ImportConflict {
    pub const ALL: [Self; 4] = [Self::Stop, Self::Skip, Self::Update, Self::Replace];

    pub fn code(self) -> &'static str {
        match self {
            Self::Stop => "stop",
            Self::Skip => "skip",
            Self::Update => "update",
            Self::Replace => "replace",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Stop => "Stop at the first conflict",
            Self::Skip => "Skip rows that conflict",
            Self::Update => "Update rows with the same key",
            Self::Replace => "Replace rows that conflict",
        }
    }

    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL
            .into_iter()
            .find(|conflict| conflict.code() == code)
    }

    pub fn supported(self, driver: GridEditDriver) -> bool {
        !(self == Self::Replace && driver == GridEditDriver::Postgres)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ImportColumn {
    pub name: String,
    pub database_type: String,
    pub key: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RowImportError {
    InvalidIdentifier,
    NoColumns,
    UnsupportedConflict,
    KeyNotMapped,
    RowShape,
}

impl std::fmt::Display for RowImportError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::InvalidIdentifier => "The import target contains an invalid identifier.",
            Self::NoColumns => "Choose one or more columns to import.",
            Self::UnsupportedConflict => "This connection does not support the conflict policy.",
            Self::KeyNotMapped => {
                "Import all primary key columns to update rows with the same key."
            }
            Self::RowShape => "The import row does not match the import columns.",
        })
    }
}

impl std::error::Error for RowImportError {}

pub fn quote_import_identifier(
    driver: GridEditDriver,
    name: &str,
) -> Result<String, RowImportError> {
    quoted(name, identifier_delimiter(driver)).map_err(|_| RowImportError::InvalidIdentifier)
}

fn checked_target(driver: GridEditDriver, qualified_name: &str) -> Result<(), RowImportError> {
    if valid_qualified(qualified_name, identifier_delimiter(driver) as u8) {
        Ok(())
    } else {
        Err(RowImportError::InvalidIdentifier)
    }
}

fn quoted_names(
    driver: GridEditDriver,
    columns: &[ImportColumn],
) -> Result<Vec<String>, RowImportError> {
    if columns.is_empty() {
        return Err(RowImportError::NoColumns);
    }
    columns
        .iter()
        .map(|column| quote_import_identifier(driver, &column.name))
        .collect()
}

pub fn row_import_sql(
    driver: GridEditDriver,
    qualified_name: &str,
    columns: &[ImportColumn],
    key_columns: &[String],
    conflict: ImportConflict,
) -> Result<String, RowImportError> {
    checked_target(driver, qualified_name)?;
    if !conflict.supported(driver) {
        return Err(RowImportError::UnsupportedConflict);
    }
    let names = quoted_names(driver, columns)?;
    let values: Vec<String> = (1..=columns.len())
        .map(|position| placeholder(driver, position))
        .collect();
    let mysql = driver == GridEditDriver::Mysql;
    let verb = match conflict {
        ImportConflict::Replace => "REPLACE",
        ImportConflict::Skip if mysql => MYSQL_SKIP_VERB,
        _ => "INSERT",
    };
    let insert = format!(
        "{verb} INTO {qualified_name} ({}) VALUES ({})",
        names.join(", "),
        values.join(", ")
    );
    let suffix = match conflict {
        ImportConflict::Stop | ImportConflict::Replace => String::new(),
        ImportConflict::Skip if mysql => String::new(),
        ImportConflict::Skip => " ON CONFLICT DO NOTHING".into(),
        ImportConflict::Update => {
            if key_columns.is_empty()
                || !key_columns
                    .iter()
                    .all(|key| columns.iter().any(|column| &column.name == key))
            {
                return Err(RowImportError::KeyNotMapped);
            }
            let updates: Vec<&String> = columns
                .iter()
                .zip(&names)
                .filter(|(column, _)| !key_columns.contains(&column.name))
                .map(|(_, name)| name)
                .collect();
            if mysql {
                let assignments: Vec<String> = if updates.is_empty() {
                    vec![format!("{0} = {0}", names[0])]
                } else {
                    updates
                        .iter()
                        .map(|name| format!("{name} = VALUES({name})"))
                        .collect()
                };
                format!(" ON DUPLICATE KEY UPDATE {}", assignments.join(", "))
            } else {
                let keys = key_columns
                    .iter()
                    .map(|key| quote_import_identifier(driver, key))
                    .collect::<Result<Vec<_>, _>>()?;
                if updates.is_empty() {
                    format!(" ON CONFLICT ({}) DO NOTHING", keys.join(", "))
                } else {
                    let assignments: Vec<String> = updates
                        .iter()
                        .map(|name| format!("{name} = excluded.{name}"))
                        .collect();
                    format!(
                        " ON CONFLICT ({}) DO UPDATE SET {}",
                        keys.join(", "),
                        assignments.join(", ")
                    )
                }
            }
        }
    };
    Ok(insert + &suffix)
}

const MYSQL_SKIP_VERB: &str = "INSERT IGNORE";

pub fn mysql_ignores_duplicates(sql: &str) -> bool {
    sql.strip_prefix(MYSQL_SKIP_VERB)
        .is_some_and(|rest| rest.starts_with(' '))
}

pub fn row_import_statement(
    sql: &str,
    columns: &[ImportColumn],
    row: Vec<Option<String>>,
) -> Result<EditStatement, RowImportError> {
    if row.len() != columns.len() {
        return Err(RowImportError::RowShape);
    }
    Ok(EditStatement {
        sql: sql.to_owned(),
        params: row
            .into_iter()
            .zip(columns)
            .map(|(value, column)| match value {
                None => Value::Null,
                Some(text) => typed_text(text, &column.database_type),
            })
            .collect(),
        expected_rows: None,
    })
}

pub fn create_import_table_sql(
    driver: GridEditDriver,
    qualified_name: &str,
    columns: &[ImportColumn],
) -> Result<String, RowImportError> {
    checked_target(driver, qualified_name)?;
    let names = quoted_names(driver, columns)?;
    let definitions: Vec<String> = names
        .iter()
        .zip(columns)
        .map(|(name, column)| format!("{name} {}", column.database_type))
        .collect();
    Ok(format!(
        "CREATE TABLE {qualified_name} ({})",
        definitions.join(", ")
    ))
}

pub fn drop_import_table_sql(
    driver: GridEditDriver,
    qualified_name: &str,
) -> Result<String, RowImportError> {
    checked_target(driver, qualified_name)?;
    Ok(format!("DROP TABLE {qualified_name}"))
}
