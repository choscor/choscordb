//! Local SQL predicates run only against bound materialized rows, never the source connection.
use crate::{FilterCondition, FilterOperator, value_matches};
use choscordb_driver_api::{Column, DriverError, ErrorKind, Value};
use rusqlite::{Connection, params_from_iter, types::Value as SqlValue};

fn invalid(message: impl ToString) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message.to_string())
}

pub(crate) struct Predicates {
    database: Connection,
    expressions: Vec<Option<String>>,
    referenced: Vec<Vec<usize>>,
    lists: Vec<Option<Vec<Value>>>,
    steps: std::sync::Arc<std::sync::atomic::AtomicUsize>,
}

impl Predicates {
    pub(crate) fn new(
        columns: &[Column],
        filters: &[FilterCondition],
        max_value_bytes: usize,
    ) -> Result<Self, DriverError> {
        let database = Connection::open_in_memory().map_err(invalid)?;
        use rusqlite::limits::Limit;
        for (limit, value) in [
            (
                Limit::SQLITE_LIMIT_LENGTH,
                i32::try_from(max_value_bytes).unwrap_or(i32::MAX),
            ),
            (Limit::SQLITE_LIMIT_SQL_LENGTH, 65_536),
            (Limit::SQLITE_LIMIT_EXPR_DEPTH, 100),
            (Limit::SQLITE_LIMIT_VDBE_OP, 100_000),
            (Limit::SQLITE_LIMIT_COMPOUND_SELECT, 10),
        ] {
            database.set_limit(limit, value).map_err(invalid)?;
        }
        let steps = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let progress = steps.clone();
        database
            .progress_handler(
                1000,
                Some(move || progress.fetch_add(1, std::sync::atomic::Ordering::Relaxed) >= 100),
            )
            .map_err(invalid)?;

        let reads = std::sync::Arc::new(std::sync::Mutex::new(Vec::<String>::new()));
        if filters
            .iter()
            .any(|filter| filter.operator == FilterOperator::Sql)
        {
            let definitions = columns
                .iter()
                .map(|column| format!("\"{}\"", column.name.replace('"', "\"\"")))
                .collect::<Vec<_>>()
                .join(", ");
            database
                .execute_batch(&format!("CREATE TABLE __filter_schema ({definitions})"))
                .map_err(invalid)?;
        }
        let tracked = reads.clone();
        database
            .authorizer(Some(
                move |context: rusqlite::hooks::AuthContext<'_>| match context.action {
                    rusqlite::hooks::AuthAction::Select
                    | rusqlite::hooks::AuthAction::Function { .. } => {
                        rusqlite::hooks::Authorization::Allow
                    }
                    rusqlite::hooks::AuthAction::Read {
                        table_name: "__filter_schema",
                        column_name,
                    } => {
                        tracked
                            .lock()
                            .expect("read tracking lock")
                            .push(column_name.to_owned());
                        rusqlite::hooks::Authorization::Allow
                    }
                    _ => rusqlite::hooks::Authorization::Deny,
                },
            ))
            .map_err(invalid)?;
        database
            .set_db_config(rusqlite::config::DbConfig::SQLITE_DBCONFIG_DQS_DML, false)
            .map_err(invalid)?;
        let projection = columns
            .iter()
            .enumerate()
            .map(|(index, column)| {
                format!("?{} AS \"{}\"", index + 1, column.name.replace('"', "\"\""))
            })
            .collect::<Vec<_>>()
            .join(", ");
        let mut expressions = Vec::new();
        let mut lists = Vec::new();
        let mut referenced = Vec::new();
        for filter in filters {
            if filter.operator != FilterOperator::Sql && filter.column >= columns.len() {
                return Err(invalid("Filter column is out of range"));
            }
            lists.push(if filter.operator == FilterOperator::In {
                let Some(Value::Text(text)) = &filter.value else {
                    return Err(invalid("IN requires SQL literals"));
                };
                Some(parse_literal_list(text)?)
            } else {
                None
            });
            let mut used_columns = vec![filter.column];
            let expression = match filter.operator {
                FilterOperator::Sql | FilterOperator::Like | FilterOperator::NotLike => {
                    let Some(Value::Text(text)) = &filter.value else {
                        return Err(invalid("SQL, LIKE and IN filters require text"));
                    };
                    if filter.operator == FilterOperator::Sql && text.trim().is_empty() {
                        return Err(invalid("Filter value is required"));
                    }
                    let predicate = match filter.operator {
                        FilterOperator::Sql => {
                            validate_expression(text)?;
                            reads.lock().expect("read tracking lock").clear();
                            database.prepare(&format!("SELECT CASE WHEN ({text}) THEN 1 ELSE 0 END FROM __filter_schema")).map_err(invalid)?;
                            let names = reads.lock().expect("read tracking lock");
                            used_columns = columns
                                .iter()
                                .enumerate()
                                .filter_map(|(index, column)| {
                                    names
                                        .iter()
                                        .any(|name| name.eq_ignore_ascii_case(&column.name))
                                        .then_some(index)
                                })
                                .collect();
                            text.clone()
                        }
                        operator => format!(
                            "?{} {}LIKE '{}'",
                            filter.column + 1,
                            if operator == FilterOperator::NotLike {
                                "NOT "
                            } else {
                                ""
                            },
                            text.replace('\'', "''")
                        ),
                    };
                    let sql = format!(
                        "SELECT CASE WHEN ({predicate}\n) THEN 1 ELSE 0 END FROM (SELECT {projection})"
                    );
                    let statement = database.prepare(&sql).map_err(invalid)?;
                    if statement.parameter_count() != columns.len()
                        || statement.column_count() != 1
                        || !statement.readonly()
                    {
                        return Err(invalid(
                            "Enter a SQL condition without additional statements or parameters",
                        ));
                    }
                    Some(sql)
                }
                _ => None,
            };
            expressions.push(expression);
            referenced.push(used_columns);
        }
        Ok(Self {
            database,
            expressions,
            referenced,
            lists,
            steps,
        })
    }

    pub(crate) fn row_matches(
        &self,
        row: &[Value],
        filters: &[FilterCondition],
    ) -> Result<bool, DriverError> {
        let mut matched = true;
        for (((filter, expression), list), referenced) in filters
            .iter()
            .zip(&self.expressions)
            .zip(&self.lists)
            .zip(&self.referenced)
        {
            let current = if let Some(list) = list {
                let value = &row[filter.column];
                let mut found = false;
                for operand in list {
                    if !matches!(operand, Value::Null) {
                        let uuid;
                        let operand = if let (Value::Uuid(_), Value::Text(text)) = (value, operand)
                        {
                            uuid = Value::Uuid(text.clone());
                            &uuid
                        } else {
                            operand
                        };
                        found |= value_matches(value, FilterOperator::Equals, Some(operand))?;
                    }
                }
                found
            } else if let Some(sql) = expression {
                let values = row
                    .iter()
                    .enumerate()
                    .map(|(index, value)| {
                        if referenced.contains(&index) {
                            sql_value(value)
                        } else {
                            Ok(SqlValue::Null)
                        }
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                self.steps.store(0, std::sync::atomic::Ordering::Relaxed);
                self.database
                    .prepare_cached(sql)
                    .map_err(invalid)?
                    .query_row(params_from_iter(values.iter()), |row| row.get::<_, bool>(0))
                    .map_err(invalid)?
            } else {
                value_matches(&row[filter.column], filter.operator, filter.value.as_ref())?
            };
            matched &= current;
        }
        Ok(matched)
    }
}

fn sql_value(value: &Value) -> Result<SqlValue, DriverError> {
    Ok(match value {
        Value::Null => SqlValue::Null,
        Value::Bool(value) => SqlValue::Integer(i64::from(*value)),
        Value::Integer(value) => SqlValue::Integer(*value),
        Value::Real(value) => SqlValue::Real(*value),
        Value::Decimal(value) => {
            if let Ok(integer) = value.parse::<i64>() {
                SqlValue::Integer(integer)
            } else {
                let real = value
                    .parse::<f64>()
                    .map_err(|_| invalid("Invalid decimal value"))?;
                if !real.is_finite()
                    || crate::result_view::cmp(&Value::Decimal(value.clone()), &Value::Real(real))?
                        != std::cmp::Ordering::Equal
                {
                    return Err(invalid(
                        "SQL filtering would lose decimal precision; use manual decimal filters",
                    ));
                }
                SqlValue::Real(real)
            }
        }
        Value::Text(value)
        | Value::Date(value)
        | Value::Time(value)
        | Value::Timestamp(value)
        | Value::Uuid(value)
        | Value::Json(value) => SqlValue::Text(value.clone()),
        Value::Binary(value) => SqlValue::Blob(value.clone()),
        Value::Deferred { .. } => {
            return Err(invalid("Deferred values cannot be filtered with SQL"));
        }
    })
}

// Accept SQL literals, including escaped quotes and commas inside quoted strings.
// Subqueries and expressions are reserved for SQL mode.
fn parse_literal_list(text: &str) -> Result<Vec<Value>, DriverError> {
    let mut rest = text.trim();
    let mut values = Vec::new();
    loop {
        if rest.starts_with("X'") || rest.starts_with("x'") {
            let end = rest[2..]
                .find('\'')
                .ok_or_else(|| invalid("Unterminated IN blob literal"))?
                + 2;
            let hex = &rest[2..end];
            if !hex.len().is_multiple_of(2) || !hex.bytes().all(|byte| byte.is_ascii_hexdigit()) {
                return Err(invalid(
                    "IN blob literals require pairs of hexadecimal digits",
                ));
            }
            values.push(Value::Binary(
                (0..hex.len())
                    .step_by(2)
                    .map(|index| {
                        u8::from_str_radix(&hex[index..index + 2], 16).expect("validated hex")
                    })
                    .collect(),
            ));
            rest = &rest[end + 1..];
        } else if let Some(quoted) = rest.strip_prefix('\'') {
            let mut chars = quoted.char_indices().peekable();
            let mut end = None;
            while let Some((index, character)) = chars.next() {
                if character == '\'' {
                    if chars.peek().is_some_and(|(_, next)| *next == '\'') {
                        chars.next();
                    } else {
                        end = Some(index + 2);
                        break;
                    }
                }
            }
            let end = end.ok_or_else(|| invalid("Unterminated IN string literal"))?;
            values.push(Value::Text(rest[1..end - 1].replace("''", "'")));
            rest = &rest[end..];
        } else {
            let end = rest.find(',').unwrap_or(rest.len());
            let literal = rest[..end].trim();
            if !["null", "true", "false"]
                .iter()
                .any(|word| literal.eq_ignore_ascii_case(word))
                && (literal.is_empty()
                    || literal.parse::<f64>().is_err()
                    || !literal
                        .bytes()
                        .all(|byte| byte.is_ascii_digit() || b"+-.eE".contains(&byte)))
            {
                return Err(invalid(
                    "IN requires a comma-separated list of SQL literals",
                ));
            }
            values.push(if literal.eq_ignore_ascii_case("null") {
                Value::Null
            } else if literal.eq_ignore_ascii_case("true") {
                Value::Bool(true)
            } else if literal.eq_ignore_ascii_case("false") {
                Value::Bool(false)
            } else if let Ok(integer) = literal.parse::<i64>() {
                Value::Integer(integer)
            } else {
                Value::Decimal(literal.into())
            });
            rest = &rest[end..];
        }
        rest = rest.trim_start();
        if rest.is_empty() {
            return Ok(values);
        }
        rest = rest
            .strip_prefix(',')
            .ok_or_else(|| invalid("Separate IN literals with commas"))?
            .trim_start();
        if rest.is_empty() {
            return Err(invalid("IN list cannot end with a comma"));
        }
    }
}

fn validate_expression(text: &str) -> Result<(), DriverError> {
    let mut quote = None;
    let mut chars = text.chars().peekable();
    while let Some(character) = chars.next() {
        if let Some(delimiter) = quote {
            if character == delimiter {
                if chars.peek() == Some(&delimiter) {
                    chars.next();
                } else {
                    quote = None;
                }
            }
        } else if matches!(character, '\'' | '"' | '`' | '[') {
            quote = Some(if character == '[' { ']' } else { character });
        } else if matches!(character, ';' | '?' | ':' | '@' | '$')
            || (character == '-' && chars.peek() == Some(&'-'))
            || (character == '/' && chars.peek() == Some(&'*'))
        {
            return Err(invalid(
                "Use a SQL condition without statements, comments or parameters",
            ));
        }
    }
    Ok(())
}
