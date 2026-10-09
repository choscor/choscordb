//! Typed grid edit policy and statement planning.
use crate::{EditStatement, Value};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridEditDriver {
    Sqlite,
    Postgres,
    Mysql,
}

#[derive(Clone, Debug)]
pub struct GridEditColumn {
    pub name: String,
    pub result_name: String,
    pub database_type: String,
    pub key: bool,
    pub generated: bool,
    pub enum_source_column: String,
    pub enum_choices: Vec<String>,
}

#[derive(Clone, Debug)]
pub struct GridEditRow {
    pub current: Vec<Value>,
    pub original: Vec<Value>,
    pub touched: Vec<bool>,
    pub inserted: bool,
    pub deleted: bool,
}

#[derive(Clone, Debug)]
pub struct GridEditRequest {
    pub driver: GridEditDriver,
    pub qualified_name: String,
    pub parameter_style: String,
    pub reason: String,
    pub object_read_only: bool,
    pub columns: Vec<GridEditColumn>,
    pub rows: Vec<GridEditRow>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct GridEditability {
    pub editable: Vec<bool>,
    pub insert_editable: Vec<bool>,
    pub key_columns: Vec<bool>,
    pub can_insert: bool,
    pub can_delete: bool,
    pub reason: Option<String>,
}

#[derive(Clone, Debug)]
pub struct PlannedGridEdit {
    pub statement: EditStatement,
    pub parameter_types: Vec<String>,
}

#[derive(Clone, Debug, Default)]
pub struct GridEditPlan {
    pub statements: Vec<PlannedGridEdit>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridEditError {
    InvalidShape,
    UnsupportedTarget,
    InvalidIdentifier,
    ColumnNotEditable,
    UnsafeOriginal,
    OpaqueMatch,
    DeferredParameter,
    OpaqueParameter,
    BinaryReviewLimit,
    NoStatements,
}

impl std::fmt::Display for GridEditError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::InvalidShape => "The grid edit rows do not match the result columns.",
            Self::UnsupportedTarget => "The selected result is not editable.",
            Self::InvalidIdentifier => "The edit target contains an invalid identifier.",
            Self::ColumnNotEditable => "The selected column cannot be edited.",
            Self::UnsafeOriginal => "Cannot safely compare a deferred or binary original value.",
            Self::OpaqueMatch => {
                "Cannot safely delete or match a row using a fallback or unavailable value."
            }
            Self::DeferredParameter => "Deferred values cannot be bound to grid edits.",
            Self::OpaqueParameter => "Fallback or unavailable values cannot be edited.",
            Self::BinaryReviewLimit => {
                "Binary parameter exceeds the 64 KiB review limit; narrow the edit."
            }
            Self::NoStatements => "There are no applicable grid changes.",
        })
    }
}

impl std::error::Error for GridEditError {}

fn opaque(value: &Value) -> bool {
    matches!(
        value,
        Value::FallbackText { .. } | Value::Unavailable { .. }
    )
}

fn unresolved(value: &Value) -> bool {
    matches!(
        value,
        Value::Deferred { .. } | Value::DeferredFallback { .. } | Value::Binary(_)
    )
}

fn comparable_postgres_type(database_type: &str) -> bool {
    let database_type = database_type.to_ascii_lowercase();
    matches!(
        database_type.as_str(),
        "integer"
            | "bigint"
            | "smallint"
            | "boolean"
            | "text"
            | "uuid"
            | "date"
            | "time without time zone"
            | "time with time zone"
            | "real"
            | "double precision"
            | "jsonb"
    ) || ["timestamp", "character", "varchar", "numeric", "decimal"]
        .iter()
        .any(|prefix| database_type.starts_with(prefix))
}

fn shaped(request: &GridEditRequest) -> bool {
    let width = request.columns.len();
    width > 0
        && request.rows.iter().all(|row| {
            row.current.len() == width
                && row.touched.len() == width
                && (row.inserted || row.original.len() == width)
        })
}

pub fn grid_editability(request: &GridEditRequest) -> GridEditability {
    let width = request.columns.len();
    let mut result = GridEditability {
        editable: vec![false; width],
        insert_editable: vec![false; width],
        key_columns: request.columns.iter().map(|column| column.key).collect(),
        ..Default::default()
    };
    let valid_style = match request.driver {
        GridEditDriver::Postgres => request.parameter_style == "$",
        GridEditDriver::Sqlite | GridEditDriver::Mysql => request.parameter_style == "?",
    };
    let delimiter = if request.driver == GridEditDriver::Mysql {
        b'`'
    } else {
        b'"'
    };
    if !valid_qualified(&request.qualified_name, delimiter)
        || request
            .columns
            .iter()
            .any(|column| column.name.len() > 1024 * 1024 || column.name.contains('\0'))
        || !valid_style
        || !shaped(request)
    {
        return result;
    }
    if !request.reason.is_empty()
        && !(request.object_read_only
            && request.reason.to_ascii_lowercase().contains("inserts only"))
    {
        result.reason = Some(request.reason.clone());
        return result;
    }
    let mut opaque_columns = vec![false; width];
    for row in &request.rows {
        for (index, value) in row.current.iter().enumerate() {
            opaque_columns[index] |= opaque(value);
        }
    }
    if request.object_read_only
        && request
            .columns
            .iter()
            .any(|column| column.result_name != column.name)
    {
        result.reason = Some("Result columns do not match table metadata.".into());
        return result;
    }
    let mut keyed = request.columns.iter().any(|column| column.key);
    for (index, column) in request.columns.iter().enumerate() {
        let eligible = !opaque_columns[index] && !column.name.is_empty() && !column.generated;
        result.insert_editable[index] = eligible;
        result.editable[index] = eligible && !column.key;
        if column.key && opaque_columns[index] {
            keyed = false;
        }
        if request.driver == GridEditDriver::Postgres
            && !opaque_columns[index]
            && !column.name.is_empty()
            && !comparable_postgres_type(&column.database_type)
            && !(column.enum_source_column == column.name && !column.enum_choices.is_empty())
        {
            keyed = false;
        }
    }
    if request
        .rows
        .iter()
        .flat_map(|row| &row.current)
        .any(unresolved)
    {
        keyed = false;
        result.reason = Some(
            "Binary or deferred original values prevent safe conflict checks; inserts remain available."
                .into(),
        );
    }
    if !keyed {
        result.editable.fill(false);
    }
    result.can_insert = true;
    result.can_delete = keyed;
    result
}

fn valid_qualified(name: &str, delimiter: u8) -> bool {
    if name.is_empty() || name.len() > 1024 * 1024 || name.contains('\0') {
        return false;
    }
    let bytes = name.as_bytes();
    let mut position = 0;
    loop {
        if bytes.get(position) != Some(&delimiter) {
            return false;
        }
        position += 1;
        let start = position;
        loop {
            match bytes.get(position) {
                None => return false,
                Some(byte) if *byte == delimiter && bytes.get(position + 1) == Some(&delimiter) => {
                    position += 2;
                }
                Some(byte) if *byte == delimiter => {
                    if position == start {
                        return false;
                    }
                    position += 1;
                    break;
                }
                Some(_) => position += 1,
            }
        }
        if position == bytes.len() {
            return true;
        }
        if bytes[position] != b'.' {
            return false;
        }
        position += 1;
    }
}

fn quoted(name: &str, delimiter: char) -> Result<String, GridEditError> {
    if name.is_empty() || name.len() > 1024 * 1024 || name.contains('\0') {
        return Err(GridEditError::InvalidIdentifier);
    }
    Ok(format!(
        "{delimiter}{}{delimiter}",
        name.replace(delimiter, &format!("{delimiter}{delimiter}"))
    ))
}

fn push_param(
    statement: &mut EditStatement,
    parameter_types: &mut Vec<String>,
    value: &Value,
    column: &GridEditColumn,
    driver: GridEditDriver,
) -> Result<String, GridEditError> {
    match value {
        Value::Deferred { .. } | Value::DeferredFallback { .. } => {
            return Err(GridEditError::DeferredParameter);
        }
        Value::FallbackText { .. } | Value::Unavailable { .. } => {
            return Err(GridEditError::OpaqueParameter);
        }
        Value::Binary(bytes) if bytes.len() > 65536 => {
            return Err(GridEditError::BinaryReviewLimit);
        }
        _ => {}
    }
    let bound = match value {
        Value::Text(text) => {
            let kind = column.database_type.to_ascii_lowercase();
            if kind.starts_with("numeric") || kind.starts_with("decimal") {
                Value::Decimal(text.clone())
            } else if kind == "date" {
                Value::Date(text.clone())
            } else if kind == "time" || kind.starts_with("time ") {
                Value::Time(text.clone())
            } else if kind.starts_with("timestamp") {
                Value::Timestamp(text.clone())
            } else if kind == "uuid" {
                Value::Uuid(text.clone())
            } else if kind == "json" || kind == "jsonb" {
                Value::Json(text.clone())
            } else {
                value.clone()
            }
        }
        _ => value.clone(),
    };
    statement.params.push(bound);
    parameter_types.push(column.database_type.clone());
    Ok(if driver == GridEditDriver::Postgres {
        format!("${}", statement.params.len())
    } else {
        "?".into()
    })
}

pub fn plan_grid_edits(request: &GridEditRequest) -> Result<GridEditPlan, GridEditError> {
    if !shaped(request) {
        return Err(GridEditError::InvalidShape);
    }
    let delimiter = if request.driver == GridEditDriver::Mysql {
        '`'
    } else {
        '"'
    };
    if !valid_qualified(&request.qualified_name, delimiter as u8)
        || request
            .columns
            .iter()
            .any(|column| column.name.len() > 1024 * 1024 || column.name.contains('\0'))
    {
        return Err(GridEditError::InvalidIdentifier);
    }
    let eligibility = grid_editability(request);
    if !eligibility.can_insert {
        return Err(GridEditError::UnsupportedTarget);
    }
    let mut plan = GridEditPlan::default();
    for row in &request.rows {
        if row.inserted && row.deleted {
            continue;
        }
        if !row.inserted && !row.deleted && !row.touched.iter().any(|touched| *touched) {
            continue;
        }
        let mut statement = EditStatement {
            sql: String::new(),
            params: vec![],
            expected_rows: (!row.inserted).then_some(1),
        };
        let mut parameter_types = Vec::new();
        if row.inserted {
            let mut names = Vec::new();
            let mut values = Vec::new();
            for (index, column) in request.columns.iter().enumerate() {
                if !row.touched[index] || column.generated {
                    continue;
                }
                if !eligibility.insert_editable[index] {
                    return Err(GridEditError::ColumnNotEditable);
                }
                names.push(quoted(&column.name, delimiter)?);
                values.push(push_param(
                    &mut statement,
                    &mut parameter_types,
                    &row.current[index],
                    column,
                    request.driver,
                )?);
            }
            statement.sql = if names.is_empty() {
                if request.driver == GridEditDriver::Mysql {
                    format!("INSERT INTO {} () VALUES ()", request.qualified_name)
                } else {
                    format!("INSERT INTO {} DEFAULT VALUES", request.qualified_name)
                }
            } else {
                format!(
                    "INSERT INTO {} ({}) VALUES ({})",
                    request.qualified_name,
                    names.join(", "),
                    values.join(", ")
                )
            };
        } else {
            if !eligibility.can_delete {
                return Err(GridEditError::UnsupportedTarget);
            }
            let mut assignments = Vec::new();
            if !row.deleted {
                for (index, column) in request.columns.iter().enumerate() {
                    if !row.touched[index] {
                        continue;
                    }
                    if !eligibility.editable[index] {
                        return Err(GridEditError::ColumnNotEditable);
                    }
                    let placeholder = push_param(
                        &mut statement,
                        &mut parameter_types,
                        &row.current[index],
                        column,
                        request.driver,
                    )?;
                    assignments.push(format!(
                        "{} = {placeholder}",
                        quoted(&column.name, delimiter)?
                    ));
                }
            }
            if assignments.is_empty() && !row.deleted {
                continue;
            }
            let operator = match request.driver {
                GridEditDriver::Sqlite => " IS ",
                GridEditDriver::Postgres => " IS NOT DISTINCT FROM ",
                GridEditDriver::Mysql => " <=> ",
            };
            let mut predicates = Vec::new();
            for (index, column) in request.columns.iter().enumerate() {
                if column.name.is_empty() {
                    continue;
                }
                match &row.original[index] {
                    Value::Deferred { .. } | Value::DeferredFallback { .. } | Value::Binary(_) => {
                        return Err(GridEditError::UnsafeOriginal);
                    }
                    Value::FallbackText { .. } | Value::Unavailable { .. } => {
                        if row.deleted || column.key {
                            return Err(GridEditError::OpaqueMatch);
                        }
                        continue;
                    }
                    value => {
                        let placeholder = push_param(
                            &mut statement,
                            &mut parameter_types,
                            value,
                            column,
                            request.driver,
                        )?;
                        predicates.push(format!(
                            "{}{operator}{placeholder}",
                            quoted(&column.name, delimiter)?
                        ));
                    }
                }
            }
            if predicates.is_empty() {
                return Err(GridEditError::OpaqueMatch);
            }
            statement.sql = if row.deleted {
                format!(
                    "DELETE FROM {} WHERE {}",
                    request.qualified_name,
                    predicates.join(" AND ")
                )
            } else {
                format!(
                    "UPDATE {} SET {} WHERE {}",
                    request.qualified_name,
                    assignments.join(", "),
                    predicates.join(" AND ")
                )
            };
        }
        plan.statements.push(PlannedGridEdit {
            statement,
            parameter_types,
        });
    }
    if plan.statements.is_empty() {
        return Err(GridEditError::NoStatements);
    }
    Ok(plan)
}

fn review_literal(kind: &str, text: &str) -> String {
    let escaped = text
        .replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n");
    format!("{kind} \"{escaped}\"")
}

fn review_value(value: &Value) -> String {
    match value {
        Value::Null => "NULL".into(),
        Value::Bool(value) => value.to_string(),
        Value::Integer(value) => value.to_string(),
        Value::Real(value) => value.to_string(),
        Value::Decimal(text) => text.clone(),
        Value::Text(text) => review_literal("text", text),
        Value::Date(text) => review_literal("date", text),
        Value::Time(text) => review_literal("time", text),
        Value::Timestamp(text) => review_literal("timestamp", text),
        Value::Uuid(text) => review_literal("uuid", text),
        Value::Json(text) => review_literal("json", text),
        Value::Binary(bytes) => {
            let hex: String = bytes.iter().map(|byte| format!("{byte:02x}")).collect();
            format!("binary 0x{hex} ({} bytes)", bytes.len())
        }
        // Planning rejects these as parameters; show them without their contents.
        _ => "unavailable".into(),
    }
}

/// The statements and bound values a user reviews before applying grid edits.
pub fn review_text(plan: &GridEditPlan) -> String {
    let mut review = String::new();
    for planned in &plan.statements {
        review.push_str(&planned.statement.sql);
        review.push('\n');
        for (index, value) in planned.statement.params.iter().enumerate() {
            review.push_str(&format!(
                "  Parameter {}: {}\n",
                index + 1,
                review_value(value)
            ));
        }
        review.push('\n');
    }
    review
}

/// How a staged result cell is represented in the grid.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GridCellKind {
    Value,
    Binary,
    Deferred,
    FallbackText,
    Unavailable,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct GridCellKindPolicy {
    pub inline_editable: bool,
    pub blocks_row_delete: bool,
    pub blocks_row_duplicate: bool,
    pub duplicate_requires_load: bool,
}
impl GridCellKind {
    /// Only complete scalar values edit inline. Rows holding values the grid could
    /// not represent exactly cannot be matched for delete or copied.
    pub fn policy(self) -> GridCellKindPolicy {
        let partial = matches!(self, Self::FallbackText | Self::Unavailable);
        GridCellKindPolicy {
            inline_editable: self == Self::Value,
            blocks_row_delete: partial,
            blocks_row_duplicate: partial,
            duplicate_requires_load: self == Self::Deferred,
        }
    }
}
/// Why another staged row cannot be added to a page holding `rows` rows.
pub fn grid_row_insert_error(rows: usize) -> Option<&'static str> {
    (rows >= crate::MAX_PAGE_SIZE as usize)
        .then_some("The visible page already has the maximum number of rows.")
}
