//! Typed cell predicates for captured result columns.
use choscordb_driver_api::{DriverError, ErrorKind, Value};
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operator {
    Equals,
    NotEquals,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    In,
    Like,
    IsNull,
    IsNotNull,
}
#[derive(Debug)]
pub struct OptionItem {
    pub operator: Operator,
    pub template: String,
    pub expression: Option<String>,
    pub reason: String,
}
impl Operator {
    fn sql(self) -> &'static str {
        match self {
            Self::Equals => "=",
            Self::NotEquals => "!=",
            Self::Less => "<",
            Self::LessEqual => "<=",
            Self::Greater => ">",
            Self::GreaterEqual => ">=",
            Self::In => "IN",
            Self::Like => "LIKE",
            Self::IsNull => "IS NULL",
            Self::IsNotNull => "IS NOT NULL",
        }
    }
}
fn invalid(message: impl ToString) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message.to_string())
}
pub fn options(column: &str, value: &Value) -> Vec<OptionItem> {
    use Operator::*;
    (if matches!(value, Value::Null) {
        vec![IsNull, IsNotNull]
    } else {
        vec![
            Equals,
            NotEquals,
            Less,
            LessEqual,
            Greater,
            GreaterEqual,
            In,
            Like,
        ]
    })
    .into_iter()
    .map(|operator| {
        let result = predicate(column, value, operator);
        OptionItem {
            operator,
            template: if matches!(operator, IsNull | IsNotNull) {
                format!("{column} {}", operator.sql())
            } else {
                format!(
                    "{column} {} {}",
                    operator.sql(),
                    if operator == In { "(value)" } else { "value" }
                )
            },
            reason: result
                .as_ref()
                .err()
                .map(|error| error.message.clone())
                .unwrap_or_default(),
            expression: result.ok(),
        }
    })
    .collect()
}
pub fn predicate(column: &str, value: &Value, operator: Operator) -> Result<String, DriverError> {
    if column.is_empty() || column.contains('\0') {
        return Err(invalid("Invalid result column name"));
    }
    let name = format!("\"{}\"", column.replace('"', "\"\""));
    if matches!(value, Value::Null) {
        return if matches!(operator, Operator::IsNull | Operator::IsNotNull) {
            Ok(format!("{name} {}", operator.sql()))
        } else {
            Err(invalid("Use IS NULL or IS NOT NULL for a NULL cell"))
        };
    }
    if matches!(operator, Operator::IsNull | Operator::IsNotNull) {
        return Err(invalid("This operator requires a NULL cell"));
    }
    let bound = crate::result_predicate::sql_value(value)?;
    use rusqlite::types::Value as SqlValue;
    let literal = match bound {
        SqlValue::Integer(value) => value.to_string(),
        SqlValue::Real(value) if value.is_finite() => {
            let text = value.to_string();
            if text.contains(['.', 'e', 'E']) {
                text
            } else {
                format!("{text}.0")
            }
        }
        SqlValue::Text(value) if !value.contains('\0') => {
            format!("'{}'", value.replace('\'', "''"))
        }
        SqlValue::Blob(value) => {
            use std::fmt::Write;
            let mut hex = String::from("X'");
            for byte in value {
                write!(hex, "{byte:02x}").expect("string writing");
            }
            hex.push('\'');
            hex
        }
        _ => {
            return Err(invalid(
                "This cell cannot be represented safely in a SQL filter",
            ));
        }
    };
    if matches!(value, Value::Bool(_) | Value::Binary(_))
        && !matches!(
            operator,
            Operator::Equals | Operator::NotEquals | Operator::In
        )
    {
        return Err(invalid(
            "Boolean and binary cells support only =, != and IN",
        ));
    }
    if operator == Operator::Like {
        let text = match value {
            Value::Text(text)
            | Value::Date(text)
            | Value::Time(text)
            | Value::Timestamp(text)
            | Value::Uuid(text)
            | Value::Json(text) => text,
            _ => return Err(invalid("LIKE requires a text cell")),
        };
        let escaped = text
            .replace('\\', "\\\\")
            .replace('%', "\\%")
            .replace('_', "\\_")
            .replace('\'', "''");
        return Ok(format!("{name} LIKE '%{escaped}%' ESCAPE '\\'"));
    }
    Ok(if operator == Operator::In {
        format!("{name} IN ({literal})")
    } else {
        format!("{name} {} {literal}", operator.sql())
    })
}

#[derive(Debug)]
pub struct Composition {
    pub expression: String,
    pub validation_error: String,
}
/// Compose a generated predicate with the visible draft. Invalid drafts remain editable
/// and carry the evaluator's error; callers must not submit them as a repaired expression.
pub fn compose(
    columns: &[choscordb_driver_api::Column],
    draft: &str,
    predicate: &str,
) -> Result<Composition, DriverError> {
    validate(columns, predicate)?;
    let mut draft = draft.trim();
    if draft.is_empty() {
        return Ok(Composition {
            expression: predicate.into(),
            validation_error: String::new(),
        });
    }
    if let Err(error) = validate(columns, draft) {
        return Ok(Composition {
            expression: format!("({draft}\n) AND {predicate}"),
            validation_error: error.message,
        });
    }
    // Only discard whole-expression wrappers when SQLite accepts the unwrapped
    // expression too. In particular, scalar subqueries must keep their parentheses.
    loop {
        let tokens = tokens(draft);
        let mut depth = 0usize;
        let enclosed = tokens.first().is_some_and(|token| *token == "(")
            && tokens.iter().enumerate().all(|(index, token)| {
                match *token {
                    "(" => depth += 1,
                    ")" => depth -= 1,
                    _ => {}
                }
                depth != 0 || index == tokens.len() - 1
            });
        if !enclosed {
            break;
        }
        let inner = draft[1..draft.len() - 1].trim();
        if validate(columns, inner).is_err() {
            break;
        }
        draft = inner;
    }
    let mut depth = 0usize;
    let mut cases = 0usize;
    let has_or = tokens(draft).iter().any(|token| {
        match *token {
            "(" => depth += 1,
            ")" => depth -= 1,
            word if word.eq_ignore_ascii_case("CASE") => cases += 1,
            word if word.eq_ignore_ascii_case("END") && cases > 0 => cases -= 1,
            _ => {}
        }
        depth == 0 && cases == 0 && token.eq_ignore_ascii_case("OR")
    });
    let expression = if has_or {
        format!("({draft}) AND {predicate}")
    } else {
        format!("{draft} AND {predicate}")
    };
    validate(columns, &expression)?;
    Ok(Composition {
        expression,
        validation_error: String::new(),
    })
}
fn validate(columns: &[choscordb_driver_api::Column], expression: &str) -> Result<(), DriverError> {
    crate::result_predicate::Predicates::new(
        columns,
        &[crate::FilterCondition {
            column: 0,
            operator: crate::FilterOperator::Sql,
            value: Some(Value::Text(expression.into())),
        }],
        64 * 1024 * 1024,
    )
    .map(|_| ())
}
// Tokenize only after the existing SQLite evaluator has validated the entire
// expression. Quoted tokens are opaque, including doubled delimiters. Keywords
// inside function arguments, nested expressions or CASE do not govern root precedence.
fn tokens(text: &str) -> Vec<&str> {
    let mut result = Vec::new();
    let mut chars = text.char_indices().peekable();
    while let Some((start, character)) = chars.next() {
        if character.is_whitespace() {
            continue;
        }
        if matches!(character, '\'' | '"' | '`' | '[') {
            let end_quote = if character == '[' { ']' } else { character };
            while let Some((_, next)) = chars.next() {
                if next == end_quote {
                    if character != '[' && chars.peek().is_some_and(|(_, c)| *c == end_quote) {
                        chars.next();
                    } else {
                        break;
                    }
                }
            }
        } else if character.is_alphanumeric() || character == '_' {
            while chars
                .peek()
                .is_some_and(|(_, c)| c.is_alphanumeric() || *c == '_')
            {
                chars.next();
            }
        }
        let end = chars.peek().map_or(text.len(), |(index, _)| *index);
        result.push(&text[start..end]);
    }
    result
}
