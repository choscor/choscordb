//! Complete, bounded grid copy rules. The caller supplies UI-selected coordinates.
use choscordb_driver_api::Value;

#[derive(Clone, Debug)]
pub struct CopyCell {
    pub original: Value,
    pub resolved: Option<Value>,
    pub inserted_omitted: bool,
}

#[derive(Clone, Debug)]
pub struct CopyRequest {
    /// Rectangular output in display order. `None` represents an unselected position.
    pub rows: Vec<Vec<Option<CopyCell>>>,
    /// All loaded overrides, including those outside the copied rectangle.
    pub resolutions: Vec<(Option<Value>, Value)>,
    pub byte_budget: usize,
}

#[derive(Clone, Debug, PartialEq, Eq, thiserror::Error)]
pub enum CopyError {
    #[error("invalid copied value")]
    InvalidInput,
    #[error("invalid loaded copy value")]
    InvalidResolution,
    #[error("loaded copy value has the wrong type")]
    WrongResolutionType,
    #[error("loaded copy value is incomplete")]
    IncompleteResolution,
    #[error("unavailable value: {database_type}: {reason}")]
    Unavailable {
        database_type: String,
        reason: String,
    },
    #[error("deferred value must be loaded")]
    Deferred,
    #[error("copied selection exceeds the clipboard size limit")]
    Limit,
}

fn validate_resolution(original: Option<&Value>, loaded: &Value) -> Result<(), CopyError> {
    let Some(original) = original else {
        return Err(CopyError::InvalidResolution);
    };
    let (expected_bytes, expected_type, fallback) = match original {
        Value::Deferred {
            byte_length,
            database_type,
            ..
        } => (*byte_length, database_type, false),
        Value::DeferredFallback {
            byte_length,
            database_type,
            ..
        } => (*byte_length, database_type, true),
        _ => return Err(CopyError::InvalidResolution),
    };
    let actual = match loaded {
        Value::FallbackText {
            text,
            database_type,
        } if fallback && database_type == expected_type => text.len(),
        Value::Text(text) if !fallback => text.len(),
        Value::Binary(bytes) if !fallback => bytes.len(),
        _ => return Err(CopyError::WrongResolutionType),
    };
    if actual as u64 != expected_bytes {
        return Err(CopyError::IncompleteResolution);
    }
    Ok(())
}

struct Output {
    text: String,
    units: usize,
    limit: usize,
}

impl Output {
    fn push(&mut self, value: &str) -> Result<(), CopyError> {
        self.units = self
            .units
            .checked_add(value.encode_utf16().count())
            .filter(|units| *units <= self.limit)
            .ok_or(CopyError::Limit)?;
        self.text.push_str(value);
        Ok(())
    }

    fn escaped(&mut self, value: &str) -> Result<(), CopyError> {
        let quote = value.contains(['\t', '\n', '\r', '"']);
        if !quote {
            return self.push(value);
        }
        self.push("\"")?;
        let mut parts = value.split('"').peekable();
        while let Some(part) = parts.next() {
            self.push(part)?;
            if parts.peek().is_some() {
                self.push("\"\"")?;
            }
        }
        self.push("\"")?;
        Ok(())
    }

    fn binary(&mut self, bytes: &[u8]) -> Result<(), CopyError> {
        let length = bytes
            .len()
            .checked_mul(2)
            .and_then(|length| length.checked_add(2))
            .ok_or(CopyError::Limit)?;
        self.units = self
            .units
            .checked_add(length)
            .filter(|units| *units <= self.limit)
            .ok_or(CopyError::Limit)?;
        const HEX: &[u8; 16] = b"0123456789abcdef";
        self.text.push_str("0x");
        for byte in bytes {
            self.text.push(HEX[(byte >> 4) as usize] as char);
            self.text.push(HEX[(byte & 0x0f) as usize] as char);
        }
        Ok(())
    }
}

fn display_real(value: f64) -> String {
    if !value.is_finite() {
        return value.to_string().to_ascii_lowercase();
    }
    if value == 0.0 {
        return "0".into();
    }
    // Qt's QString::number(value, 'g', 17) uses 17 significant digits and
    // scientific notation outside the usual %g decimal exponent range.
    let scientific = format!("{value:.16e}");
    let (mantissa, exponent) = scientific.split_once('e').expect("scientific number");
    let exponent: i32 = exponent.parse().expect("scientific exponent");
    let negative = mantissa.starts_with('-');
    let digits = mantissa
        .trim_start_matches('-')
        .chars()
        .filter(|character| *character != '.')
        .collect::<String>();
    let digits = digits.trim_end_matches('0');
    let digits = if digits.is_empty() { "0" } else { digits };
    let sign = if negative { "-" } else { "" };
    if !(-4..17).contains(&exponent) {
        let fraction = &digits[1..];
        let mantissa = if fraction.is_empty() {
            digits[..1].to_string()
        } else {
            format!("{}.{}", &digits[..1], fraction)
        };
        return format!("{sign}{mantissa}e{exponent:+03}");
    }
    let decimal_position = exponent + 1;
    if decimal_position <= 0 {
        format!(
            "{sign}0.{}{}",
            "0".repeat((-decimal_position) as usize),
            digits
        )
    } else if decimal_position as usize >= digits.len() {
        format!(
            "{sign}{digits}{}",
            "0".repeat(decimal_position as usize - digits.len())
        )
    } else {
        let split = decimal_position as usize;
        format!("{sign}{}.{}", &digits[..split], &digits[split..])
    }
}

fn copy_value(output: &mut Output, cell: &CopyCell) -> Result<(), CopyError> {
    if let Some(loaded) = cell.resolved.as_ref() {
        validate_resolution(Some(&cell.original), loaded)?;
    }
    let value = cell.resolved.as_ref().unwrap_or(&cell.original);
    match value {
        Value::Unavailable {
            database_type,
            reason,
        } => Err(CopyError::Unavailable {
            database_type: database_type.clone(),
            reason: reason.clone(),
        }),
        Value::Deferred { .. } | Value::DeferredFallback { .. } => Err(CopyError::Deferred),
        Value::Binary(bytes) => output.binary(bytes),
        Value::FallbackText { text, .. }
        | Value::Text(text)
        | Value::Date(text)
        | Value::Time(text)
        | Value::Timestamp(text)
        | Value::Uuid(text)
        | Value::Json(text) => output.escaped(text),
        Value::Null => output.escaped(if cell.inserted_omitted { "" } else { "NULL" }),
        Value::Bool(value) => output.escaped(if cell.inserted_omitted {
            ""
        } else if *value {
            "true"
        } else {
            "false"
        }),
        Value::Integer(value) => output.escaped(&if cell.inserted_omitted {
            String::new()
        } else {
            value.to_string()
        }),
        Value::Real(value) => output.escaped(&if cell.inserted_omitted {
            String::new()
        } else {
            display_real(*value)
        }),
        Value::Decimal(value) => output.escaped(if cell.inserted_omitted { "" } else { value }),
    }
}

pub fn render_copy_tsv(request: CopyRequest) -> Result<String, CopyError> {
    let Some(first) = request.rows.first() else {
        return Ok(String::new());
    };
    let width = first.len();
    if width == 0 || request.rows.iter().any(|row| row.len() != width) {
        return Err(CopyError::InvalidInput);
    }
    for (original, resolved) in &request.resolutions {
        validate_resolution(original.as_ref(), resolved)?;
    }
    let limit = request.byte_budget / 2;
    if request.rows.len() > limit / width {
        return Err(CopyError::Limit);
    }
    let mut output = Output {
        text: String::new(),
        units: 0,
        limit,
    };
    for (row_index, row) in request.rows.iter().enumerate() {
        if row_index > 0 {
            output.push("\n")?;
        }
        for (column_index, cell) in row.iter().enumerate() {
            if column_index > 0 {
                output.push("\t")?;
            }
            if let Some(cell) = cell {
                copy_value(&mut output, cell)?;
            }
        }
    }
    Ok(output.text)
}
