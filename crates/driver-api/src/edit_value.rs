use crate::Value;

/// Parse an explicit cell draft and preserve an actionable type rejection.
pub fn parse_grid_edit_value_checked(database_type: &str, text: &str) -> Result<Value, String> {
    let database_type = database_type.to_ascii_lowercase();
    const INTEGER_ERROR: &str =
        "Enter a whole number from -9223372036854775808 to 9223372036854775807.";
    const BOOLEAN_ERROR: &str = "Enter true, false, 1, or 0 without surrounding whitespace.";
    const REAL_ERROR: &str = "Enter a representable real number, NaN, inf, or -inf.";
    let value = match database_type.as_str() {
        "integer" | "int" | "bigint" | "smallint" | "int2" | "int4" | "int8" => Value::Integer(
            text.trim()
                .parse::<i64>()
                .map_err(|_| INTEGER_ERROR.to_owned())?,
        ),
        "boolean" | "bool" if text.eq_ignore_ascii_case("true") || text == "1" => Value::Bool(true),
        "boolean" | "bool" if text.eq_ignore_ascii_case("false") || text == "0" => {
            Value::Bool(false)
        }
        "boolean" | "bool" => return Err(BOOLEAN_ERROR.to_owned()),
        "real" | "double precision" | "float4" | "float8" => {
            let trimmed = text.trim();
            let parsed = trimmed.parse::<f64>().map_err(|_| REAL_ERROR.to_owned())?;
            let significand = trimmed.split(['e', 'E']).next().unwrap_or(trimmed);
            if parsed == 0.0
                && significand
                    .bytes()
                    .any(|digit| (b'1'..=b'9').contains(&digit))
            {
                return Err(REAL_ERROR.to_owned());
            }
            if !parsed.is_finite()
                && !matches!(
                    trimmed.to_ascii_lowercase().as_str(),
                    "nan" | "inf" | "+inf" | "-inf"
                )
            {
                return Err(REAL_ERROR.to_owned());
            }
            Value::Real(parsed)
        }
        _ if database_type.starts_with("numeric") || database_type.starts_with("decimal") => {
            Value::Decimal(text.into())
        }
        _ => Value::Text(text.into()),
    };
    Ok(value)
}

/// Parse a user-entered grid cell using its database type.
/// Unknown types remain text; invalid input for recognized types returns `None`.
pub fn parse_grid_edit_value(database_type: &str, text: &str) -> Option<Value> {
    parse_grid_edit_value_checked(database_type, text).ok()
}
