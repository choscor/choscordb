use crate::Value;

/// Parse a user-entered grid cell using its database type.
/// Unknown types remain text; invalid input for recognized types returns `None`.
pub fn parse_grid_edit_value(database_type: &str, text: &str) -> Option<Value> {
    let database_type = database_type.to_ascii_lowercase();
    let value = match database_type.as_str() {
        "integer" | "int" | "bigint" | "smallint" | "int2" | "int4" | "int8" => {
            Value::Integer(text.trim().parse::<i64>().ok()?)
        }
        "boolean" | "bool" if text.eq_ignore_ascii_case("true") || text == "1" => Value::Bool(true),
        "boolean" | "bool" if text.eq_ignore_ascii_case("false") || text == "0" => {
            Value::Bool(false)
        }
        "boolean" | "bool" => return None,
        "real" | "double precision" | "float4" | "float8" => {
            let trimmed = text.trim();
            let parsed = trimmed.parse::<f64>().ok()?;
            let significand = trimmed.split(['e', 'E']).next().unwrap_or(trimmed);
            if parsed == 0.0
                && significand
                    .bytes()
                    .any(|digit| (b'1'..=b'9').contains(&digit))
            {
                return None;
            }
            if !parsed.is_finite()
                && !matches!(
                    trimmed.to_ascii_lowercase().as_str(),
                    "nan" | "inf" | "+inf" | "-inf"
                )
            {
                return None;
            }
            Value::Real(parsed)
        }
        _ if database_type.starts_with("numeric") || database_type.starts_with("decimal") => {
            Value::Decimal(text.into())
        }
        _ => Value::Text(text.into()),
    };
    Some(value)
}
