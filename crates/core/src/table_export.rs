use crate::object_action::{ObjectAction, object_display_identity, prepare_object_action};
use choscordb_driver_api::ObjectId;
use choscordb_export::{CsvOptions, ExportFormat, SqlDialect};
use std::collections::HashSet;
use std::path::PathBuf;

pub const MAX_TABLE_EXPORT_OBJECTS: usize = 1_000;
pub const DEFAULT_TABLE_EXPORT_PATTERN: &str = "{table}";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TableExportObject {
    pub object_id: String,
    pub qualified_name: String,
    pub kind: String,
    pub relation_subtype: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TableExportFormat {
    Csv(CsvOptions),
    Json,
    JsonLines,
    Sql {
        structure: bool,
        drop: bool,
        data: bool,
    },
}

impl TableExportFormat {
    fn extension(&self) -> &'static str {
        match self {
            Self::Csv(_) => "csv",
            Self::Json => "json",
            Self::JsonLines => "jsonl",
            Self::Sql { .. } => "sql",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TableExportDestination {
    Folder { path: PathBuf, pattern: String },
    File(PathBuf),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TableExportRequest {
    pub driver: String,
    pub objects: Vec<TableExportObject>,
    pub format: TableExportFormat,
    pub destination: TableExportDestination,
}

#[derive(Clone, Debug)]
pub struct TableExportData {
    pub select: String,
    pub format: ExportFormat,
}

#[derive(Clone, Debug)]
pub struct TableExportTarget {
    pub object: ObjectId,
    pub label: String,
    pub destination: PathBuf,
    pub drop: Option<String>,
    pub structure: bool,
    pub data: Option<TableExportData>,
}

fn sql_dialect(driver: &str) -> Result<SqlDialect, String> {
    match driver {
        "sqlite" => Ok(SqlDialect::Sqlite),
        "postgres" => Ok(SqlDialect::Postgres),
        "mysql" | "mariadb" => Ok(SqlDialect::Mysql),
        _ => Err("This connection cannot export tables as SQL.".into()),
    }
}

fn file_name_part(value: &str) -> String {
    value
        .chars()
        .map(|c| {
            if c.is_control() || matches!(c, '/' | '\\' | ':' | '*' | '?' | '"' | '<' | '>' | '|') {
                '_'
            } else {
                c
            }
        })
        .collect()
}

pub fn plan_table_export(request: &TableExportRequest) -> Result<Vec<TableExportTarget>, String> {
    if request.objects.is_empty() {
        return Err("Choose one or more tables to export.".into());
    }
    if request.objects.len() > MAX_TABLE_EXPORT_OBJECTS {
        return Err(format!(
            "Export up to {MAX_TABLE_EXPORT_OBJECTS} tables at a time."
        ));
    }
    let (structure, drop, data) = match &request.format {
        TableExportFormat::Csv(options) => {
            options.validate().map_err(|error| error.message)?;
            (false, false, true)
        }
        TableExportFormat::Sql {
            structure,
            drop,
            data,
        } => {
            if !(*structure || *drop || *data) {
                return Err("Choose structure, drop or data for the SQL export.".into());
            }
            (*structure, *drop, *data)
        }
        TableExportFormat::Json | TableExportFormat::JsonLines => (false, false, true),
    };
    let sql = matches!(request.format, TableExportFormat::Sql { .. });
    if let TableExportDestination::File(_) = &request.destination
        && request.objects.len() > 1
        && !sql
    {
        return Err("Choose a folder to export several tables as CSV or JSON.".into());
    }
    if let TableExportDestination::Folder { pattern, .. } = &request.destination
        && (pattern.contains(['/', '\\']) || pattern.trim().is_empty())
    {
        return Err("The file name pattern cannot contain a folder.".into());
    }
    let mut destinations = HashSet::new();
    let mut targets = Vec::with_capacity(request.objects.len());
    for object in &request.objects {
        if !matches!(object.kind.as_str(), "table" | "view") {
            return Err("Only tables and views can be exported.".into());
        }
        let statement = prepare_object_action(
            ObjectAction::Drop,
            &request.driver,
            &object.kind,
            &object.object_id,
            &object.qualified_name,
            "",
            &object.relation_subtype,
        )?;
        let (schema, name) = object_display_identity(&object.object_id, &object.qualified_name);
        let label = match &schema {
            Some(schema) => format!("{schema}.{name}"),
            None => name.clone(),
        };
        let destination = match &request.destination {
            TableExportDestination::File(path) => path.clone(),
            TableExportDestination::Folder { path, pattern } => {
                let stem = pattern
                    .replace("{schema}", &file_name_part(schema.as_deref().unwrap_or("")))
                    .replace("{table}", &file_name_part(&name));
                let path = path.join(format!("{stem}.{}", request.format.extension()));
                if !destinations.insert(path.clone()) {
                    return Err("Two tables would write the same file. Add {table} or {schema} to the file name pattern.".into());
                }
                path
            }
        };
        let format = match &request.format {
            TableExportFormat::Csv(options) => ExportFormat::Csv(options.clone()),
            TableExportFormat::Json => ExportFormat::Json,
            TableExportFormat::JsonLines => ExportFormat::JsonLines,
            TableExportFormat::Sql { .. } => ExportFormat::SqlInsert {
                table: schema.iter().cloned().chain([name.clone()]).collect(),
                dialect: sql_dialect(&request.driver)?,
            },
        };
        targets.push(TableExportTarget {
            object: ObjectId(object.object_id.clone()),
            label,
            destination,
            drop: drop.then(|| statement.sql.clone()),
            structure,
            data: data.then(|| TableExportData {
                select: format!("SELECT * FROM {};", statement.new_qualified_name),
                format,
            }),
        });
    }
    Ok(targets)
}
