use crate::Value;

fn normalized_decimal(text: &str) -> Option<(String, i32)> {
    if text.len() > 1024 * 1024 {
        return None;
    }
    let bytes = text.as_bytes();
    let mut position = 0;
    let negative = bytes.first() == Some(&b'-');
    if matches!(bytes.first(), Some(b'-' | b'+')) {
        position += 1;
    }
    let mut digits = String::new();
    let mut fractional = 0i32;
    let mut dot = false;
    while let Some(byte) = bytes.get(position) {
        if *byte == b'.' && !dot {
            dot = true;
            position += 1;
            continue;
        }
        if !byte.is_ascii_digit() {
            break;
        }
        digits.push(*byte as char);
        if dot {
            fractional += 1;
        }
        position += 1;
    }
    if digits.is_empty() {
        return None;
    }
    let mut exponent = -fractional;
    if matches!(bytes.get(position), Some(b'e' | b'E')) {
        let parsed = text.get(position + 1..)?.parse::<i32>().ok()?;
        if !(-10000..=10000).contains(&parsed) {
            return None;
        }
        exponent = exponent.checked_add(parsed)?;
        position = bytes.len();
    }
    if position != bytes.len() {
        return None;
    }
    let first = digits
        .bytes()
        .position(|digit| digit != b'0')
        .unwrap_or(digits.len() - 1);
    digits.drain(..first);
    if digits == "0" {
        return Some(("0".into(), 0));
    }
    while digits.ends_with('0') {
        digits.pop();
        exponent += 1;
    }
    if negative {
        digits.insert(0, '-');
    }
    Some((digits, exponent))
}

fn exact_decimal_for_navigation(text: &str) -> bool {
    let Some(normalized) = normalized_decimal(text) else {
        return false;
    };
    if let Ok(integer) = text.parse::<i64>()
        && normalized_decimal(&integer.to_string()) == Some(normalized.clone())
    {
        return true;
    }
    let Ok(real) = text.parse::<f64>() else {
        return false;
    };
    real.is_finite() && normalized_decimal(&real.to_string()) == Some(normalized)
}

// QString::number(real, 'g', 17): seventeen significant digits, fixed notation for
// decimal exponents -4..16 and scientific notation otherwise.
fn real_literal(value: f64) -> Option<String> {
    if !value.is_finite() {
        return None;
    }
    if value == 0.0 {
        return Some("0".into());
    }
    let encoded = format!("{value:.16e}");
    let (mantissa, exponent) = encoded.split_once('e')?;
    let exponent = exponent.parse::<i32>().ok()?;
    let negative = mantissa.starts_with('-');
    let mut digits: String = mantissa
        .chars()
        .filter(|character| character.is_ascii_digit())
        .collect();
    while digits.len() > 1 && digits.ends_with('0') {
        digits.pop();
    }
    let rendered = if !(-4..17).contains(&exponent) {
        let (first, rest) = digits.split_at(1);
        let significand = if rest.is_empty() {
            first.to_owned()
        } else {
            format!("{first}.{rest}")
        };
        format!(
            "{significand}e{}{abs:02}",
            if exponent < 0 { '-' } else { '+' },
            abs = exponent.abs()
        )
    } else {
        let point = exponent + 1;
        if point <= 0 {
            format!("0.{}{}", "0".repeat((-point) as usize), digits)
        } else if point as usize >= digits.len() {
            format!("{}{}", digits, "0".repeat(point as usize - digits.len()))
        } else {
            format!(
                "{}.{}",
                &digits[..point as usize],
                &digits[point as usize..]
            )
        }
    };
    Some(if negative {
        format!("-{rendered}")
    } else {
        rendered
    })
}

fn literal(value: &Value) -> Option<String> {
    match value {
        Value::Bool(value) => Some((if *value { "1" } else { "0" }).into()),
        Value::Integer(value) => Some(value.to_string()),
        Value::Real(value) => real_literal(*value),
        Value::Decimal(value) if exact_decimal_for_navigation(value) => Some(value.clone()),
        Value::Text(value)
        | Value::Date(value)
        | Value::Time(value)
        | Value::Timestamp(value)
        | Value::Uuid(value)
        | Value::Json(value) => Some(format!("'{}'", value.replace('\'', "''"))),
        _ => None,
    }
}

/// Whether a result value can be used for a foreign-key navigation predicate.
pub fn foreign_key_value_filterable(value: &Value) -> bool {
    match value {
        Value::Bool(_)
        | Value::Integer(_)
        | Value::Text(_)
        | Value::Date(_)
        | Value::Time(_)
        | Value::Timestamp(_)
        | Value::Uuid(_)
        | Value::Json(_) => true,
        Value::Real(value) => value.is_finite(),
        Value::Decimal(value) => exact_decimal_for_navigation(value),
        _ => false,
    }
}

/// Build a quoted-column equality predicate for a foreign-key navigation target.
pub fn foreign_key_predicate(target_column: &str, value: &Value) -> Option<String> {
    if target_column.is_empty() || target_column.len() > 1024 * 1024 || target_column.contains('\0')
    {
        return None;
    }
    let literal = literal(value)?;
    Some(format!(
        "\"{}\" = {literal}",
        target_column.replace('"', "\"\"")
    ))
}
