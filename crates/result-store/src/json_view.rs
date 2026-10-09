use base64::{Engine as _, engine::general_purpose::STANDARD};
use choscordb_driver_api::{Column, Value};
use std::collections::{BTreeMap, HashSet};

pub const MAX_JSON_VIEW_BYTES: usize = 16 * 1024 * 1024;

#[derive(Clone, Debug)]
pub struct JsonViewRow {
    pub cells: Vec<Value>,
    pub inserted: bool,
    pub touched: Vec<bool>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JsonViewReadiness {
    Ready,
    NeedsDeferred,
    Invalid,
    Unavailable,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, thiserror::Error)]
pub enum JsonViewError {
    #[error("invalid JSON view input")]
    InvalidInput,
    #[error("invalid loaded value")]
    InvalidResolution,
    #[error("incomplete loaded value")]
    IncompleteResolution,
    #[error("invalid JSON value")]
    InvalidJson,
    #[error("unavailable value")]
    Unavailable,
    #[error("deferred value must be loaded")]
    Deferred,
    #[error("non-finite number cannot be represented as JSON")]
    NonFinite,
    #[error("JSON output exceeds the display limit")]
    DisplayLimit,
    #[error("JSON output exceeds 16 MiB of encoded text")]
    EncodedLimit,
}

struct Builder {
    text: String,
    units: usize,
    unit_limit: usize,
}

impl Builder {
    fn new(display_budget_bytes: usize) -> Self {
        Self {
            text: String::new(),
            units: 0,
            unit_limit: (display_budget_bytes / 2).min(MAX_JSON_VIEW_BYTES / 2),
        }
    }

    fn push(&mut self, part: &str) -> std::result::Result<(), JsonViewError> {
        let units = part.encode_utf16().count();
        self.units = self
            .units
            .checked_add(units)
            .filter(|units| *units <= self.unit_limit)
            .ok_or(JsonViewError::DisplayLimit)?;
        if self.text.len().saturating_add(part.len()) > MAX_JSON_VIEW_BYTES {
            return Err(JsonViewError::EncodedLimit);
        }
        self.text.push_str(part);
        Ok(())
    }

    fn quoted(&mut self, value: &str) -> std::result::Result<(), JsonViewError> {
        self.push("\"")?;
        for character in value.chars() {
            match character {
                '"' => self.push("\\\"")?,
                '\\' => self.push("\\\\")?,
                '\u{0000}'..='\u{001f}' => self.push(&format!("\\u{:04x}", character as u32))?,
                character => self.push(&character.to_string())?,
            }
        }
        self.push("\"")
    }

    fn indent(&mut self, depth: usize) -> std::result::Result<(), JsonViewError> {
        self.push("\n")?;
        self.push(&" ".repeat(depth * 2))
    }

    fn finish(self) -> String {
        self.text
    }
}

fn normalized_type(database_type: &str) -> String {
    database_type.trim().to_ascii_lowercase()
}

fn json_type(database_type: &str) -> bool {
    matches!(normalized_type(database_type).as_str(), "json" | "jsonb")
}

fn text_type(database_type: &str) -> bool {
    let normalized = normalized_type(database_type);
    if normalized.is_empty() {
        return true;
    }
    [
        "text",
        "varchar",
        "character varying",
        "char",
        "character",
        "nchar",
        "nvarchar",
        "clob",
        "citext",
        "string",
        "bpchar",
    ]
    .iter()
    .any(|prefix| normalized == *prefix || normalized.starts_with(&format!("{prefix}(")))
}

fn binary_type(database_type: &str) -> bool {
    let normalized = normalized_type(database_type);
    matches!(
        normalized.as_str(),
        "blob" | "bytea" | "binary" | "varbinary"
    ) || normalized.starts_with("varbinary(")
}

fn text_value(value: &Value) -> Option<&str> {
    match value {
        Value::Text(text)
        | Value::Date(text)
        | Value::Time(text)
        | Value::Timestamp(text)
        | Value::Uuid(text)
        | Value::Json(text) => Some(text),
        _ => None,
    }
}

fn valid_json_document(source: &str) -> bool {
    if source.len() > MAX_JSON_VIEW_BYTES || source.encode_utf16().count() > MAX_JSON_VIEW_BYTES / 2
    {
        return false;
    }
    // The Qt path parses `[source]`, so one nesting level belongs to the wrapper.
    let mut depth = 0usize;
    let mut in_string = false;
    let mut escaped = false;
    for byte in source.bytes() {
        if in_string {
            if escaped {
                escaped = false;
            } else if byte == b'\\' {
                escaped = true;
            } else if byte == b'"' {
                in_string = false;
            }
        } else {
            match byte {
                b'"' => in_string = true,
                b'[' | b'{' => {
                    depth += 1;
                    if depth > 1023 {
                        return false;
                    }
                }
                b']' | b'}' => depth = depth.saturating_sub(1),
                _ => {}
            }
        }
    }
    serde_json::from_str::<Box<serde_json::value::RawValue>>(source).is_ok()
}

fn valid_resolved(original: &Value, resolved: &Value, column: &Column) -> bool {
    match original {
        Value::DeferredFallback {
            byte_length,
            database_type,
            ..
        } => matches!(resolved, Value::FallbackText { text, database_type: kind }
            if kind == database_type && text.len() as u64 == *byte_length),
        Value::Deferred {
            byte_length,
            database_type,
            ..
        } => {
            let expected_text = json_type(&column.database_type)
                || (!column.database_type.trim().is_empty() && text_type(&column.database_type))
                || (!database_type.trim().is_empty() && text_type(database_type));
            let expected_binary = binary_type(&column.database_type) || binary_type(database_type);
            if expected_text == expected_binary {
                return false;
            }
            if expected_text {
                text_value(resolved).is_some_and(|value| value.len() as u64 == *byte_length)
            } else {
                matches!(resolved, Value::Binary(value) if value.len() as u64 == *byte_length)
            }
        }
        _ => false,
    }
}

fn pretty_json(
    output: &mut Builder,
    source: &str,
    mut depth: usize,
) -> std::result::Result<(), JsonViewError> {
    let bytes = source.as_bytes();
    let mut position = 0;
    while position < bytes.len() {
        if bytes[position].is_ascii_whitespace() {
            position += 1;
            continue;
        }
        match bytes[position] {
            b'"' => {
                let start = position;
                position += 1;
                while position < bytes.len() {
                    if bytes[position] == b'\\' {
                        position += 2;
                    } else if bytes[position] == b'"' {
                        position += 1;
                        break;
                    } else {
                        position += 1;
                    }
                }
                output.push(&source[start..position])?;
            }
            b'{' | b'[' => {
                let open = bytes[position];
                output.push(&source[position..position + 1])?;
                let mut next = position + 1;
                while next < bytes.len() && bytes[next].is_ascii_whitespace() {
                    next += 1;
                }
                if bytes.get(next) != Some(&(if open == b'{' { b'}' } else { b']' })) {
                    depth += 1;
                    output.indent(depth)?;
                }
                position += 1;
            }
            b'}' | b']' => {
                let close = bytes[position];
                let mut previous = position;
                while previous > 0 && bytes[previous - 1].is_ascii_whitespace() {
                    previous -= 1;
                }
                if previous > 0 && bytes[previous - 1] != if close == b'}' { b'{' } else { b'[' } {
                    depth -= 1;
                    output.indent(depth)?;
                }
                output.push(&source[position..position + 1])?;
                position += 1;
            }
            b',' => {
                output.push(",")?;
                output.indent(depth)?;
                position += 1;
            }
            b':' => {
                output.push(": ")?;
                position += 1;
            }
            _ => {
                let start = position;
                while position < bytes.len()
                    && !bytes[position].is_ascii_whitespace()
                    && !matches!(bytes[position], b',' | b'}' | b']')
                {
                    position += 1;
                }
                output.push(&source[start..position])?;
            }
        }
    }
    Ok(())
}

fn real_literal(value: f64) -> std::result::Result<String, JsonViewError> {
    if !value.is_finite() {
        return Err(JsonViewError::NonFinite);
    }
    if value == 0.0 {
        return Ok("0".into());
    }
    let encoded = format!("{value:.16e}");
    let (mantissa, exponent) = encoded.split_once('e').ok_or(JsonViewError::InvalidInput)?;
    let exponent = exponent
        .parse::<i32>()
        .map_err(|_| JsonViewError::InvalidInput)?;
    let negative = mantissa.starts_with('-');
    let mut digits: String = mantissa.chars().filter(char::is_ascii_digit).collect();
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
    Ok(if negative {
        format!("-{rendered}")
    } else {
        rendered
    })
}

fn append_cell(
    output: &mut Builder,
    cell: &Value,
    column: &Column,
    depth: usize,
) -> std::result::Result<(), JsonViewError> {
    match cell {
        Value::Null => output.push("null"),
        Value::Bool(value) => output.push(if *value { "true" } else { "false" }),
        Value::Integer(value) => output.push(&value.to_string()),
        Value::Real(value) => output.push(&real_literal(*value)?),
        // Preserve exact database decimals, including scale, without float conversion.
        Value::Decimal(value) => output.quoted(value),
        Value::Unavailable { .. } => Err(JsonViewError::Unavailable),
        Value::FallbackText {
            text,
            database_type,
        } => {
            output.push("{\"fallback_text\": ")?;
            output.quoted(text)?;
            output.push(", \"database_type\": ")?;
            output.quoted(database_type)?;
            output.push("}")
        }
        Value::Binary(bytes) => {
            if bytes.len() > output.unit_limit.saturating_mul(3) / 4 {
                return Err(JsonViewError::DisplayLimit);
            }
            output.push("{\"$binary\": \"")?;
            output.push(&STANDARD.encode(bytes))?;
            output.push("\"}")
        }
        Value::Text(text)
        | Value::Date(text)
        | Value::Time(text)
        | Value::Timestamp(text)
        | Value::Uuid(text)
        | Value::Json(text) => {
            if json_type(&column.database_type) {
                if !valid_json_document(text) {
                    return Err(JsonViewError::InvalidJson);
                }
                pretty_json(output, text, depth)
            } else {
                output.quoted(text)
            }
        }
        Value::Deferred { .. } | Value::DeferredFallback { .. } => Err(JsonViewError::Deferred),
    }
}

fn row_shape(columns: &[Column], row: &JsonViewRow) -> bool {
    row.cells.len() == columns.len() && row.touched.len() == columns.len()
}

fn row_impl(
    columns: &[Column],
    row: &JsonViewRow,
    resolved: &BTreeMap<usize, Value>,
    display_budget_bytes: usize,
    allow_deferred: bool,
) -> std::result::Result<(String, bool), JsonViewError> {
    if !row_shape(columns, row) {
        return Err(JsonViewError::InvalidInput);
    }
    for (&column, value) in resolved {
        let Some(original) = row.cells.get(column) else {
            return Err(JsonViewError::InvalidResolution);
        };
        if !matches!(
            original,
            Value::Deferred { .. } | Value::DeferredFallback { .. }
        ) {
            return Err(JsonViewError::InvalidResolution);
        }
        if !valid_resolved(original, value, &columns[column]) {
            return Err(JsonViewError::IncompleteResolution);
        }
    }
    let original_names: HashSet<&str> = columns
        .iter()
        .map(|column| column.name.as_str())
        .filter(|name| !name.is_empty())
        .collect();
    let mut used_names = HashSet::new();
    let mut output = Builder::new(display_budget_bytes);
    let mut unresolved = false;
    output.push("{")?;
    for (index, column) in columns.iter().enumerate() {
        output.push(if index == 0 { "\n  " } else { ",\n  " })?;
        let base = if column.name.is_empty() {
            format!("column {}", index + 1)
        } else {
            column.name.clone()
        };
        let mut key = base.clone();
        if used_names.contains(&key)
            || (column.name.is_empty() && original_names.contains(key.as_str()))
        {
            let mut suffix = 2;
            loop {
                key = format!("{base} ({suffix})");
                suffix += 1;
                if !used_names.contains(&key) && !original_names.contains(key.as_str()) {
                    break;
                }
            }
        }
        used_names.insert(key.clone());
        output.quoted(&key)?;
        output.push(": ")?;
        if row.inserted && !row.touched[index] {
            output.push("{\"$omitted\": true}")?;
            continue;
        }
        let value = resolved.get(&index).unwrap_or(&row.cells[index]);
        if allow_deferred
            && matches!(
                value,
                Value::Deferred { .. } | Value::DeferredFallback { .. }
            )
        {
            unresolved = true;
            output.push("null")?;
            continue;
        }
        append_cell(&mut output, value, column, 1)?;
    }
    output.push(if columns.is_empty() { "}" } else { "\n}" })?;
    Ok((output.finish(), unresolved))
}

pub fn json_cell_readiness(column: &Column, cell: &Value) -> JsonViewReadiness {
    if matches!(cell, Value::Null)
        || (!json_type(&column.database_type) && !text_type(&column.database_type))
    {
        return JsonViewReadiness::Unavailable;
    }
    match cell {
        Value::DeferredFallback { .. } => JsonViewReadiness::Unavailable,
        Value::Deferred { .. } => JsonViewReadiness::NeedsDeferred,
        value if text_value(value).is_some() => {
            if valid_json_document(text_value(value).unwrap_or_default()) {
                JsonViewReadiness::Ready
            } else if json_type(&column.database_type) {
                JsonViewReadiness::Invalid
            } else {
                JsonViewReadiness::Unavailable
            }
        }
        _ if json_type(&column.database_type) => JsonViewReadiness::Invalid,
        _ => JsonViewReadiness::Unavailable,
    }
}

pub fn json_row_readiness(
    columns: &[Column],
    row: &JsonViewRow,
    display_budget_bytes: usize,
) -> JsonViewReadiness {
    match row_impl(columns, row, &BTreeMap::new(), display_budget_bytes, true) {
        Ok((_, true)) => JsonViewReadiness::NeedsDeferred,
        Ok((_, false)) => JsonViewReadiness::Ready,
        Err(_) => JsonViewReadiness::Invalid,
    }
}

pub fn json_page_readiness(
    columns: &[Column],
    rows: &[JsonViewRow],
    display_budget_bytes: usize,
) -> JsonViewReadiness {
    if rows.is_empty() {
        return JsonViewReadiness::Invalid;
    }
    let mut deferred = false;
    for row in rows {
        match json_row_readiness(columns, row, display_budget_bytes) {
            JsonViewReadiness::Invalid => return JsonViewReadiness::Invalid,
            JsonViewReadiness::NeedsDeferred => deferred = true,
            _ => {}
        }
    }
    if deferred {
        JsonViewReadiness::NeedsDeferred
    } else {
        JsonViewReadiness::Ready
    }
}

pub fn render_json_cell(
    column: &Column,
    original: &Value,
    resolved: Option<&Value>,
    display_budget_bytes: usize,
) -> std::result::Result<String, JsonViewError> {
    if matches!(original, Value::Null)
        || (!json_type(&column.database_type) && !text_type(&column.database_type))
    {
        return Err(JsonViewError::InvalidInput);
    }
    if matches!(original, Value::DeferredFallback { .. }) {
        return Err(JsonViewError::Unavailable);
    }
    if let Some(value) = resolved
        && !valid_resolved(original, value, column)
    {
        return Err(JsonViewError::IncompleteResolution);
    }
    let value = resolved.unwrap_or(original);
    if matches!(
        value,
        Value::Deferred { .. } | Value::DeferredFallback { .. }
    ) {
        return Err(JsonViewError::Deferred);
    }
    let source = text_value(value).ok_or(JsonViewError::InvalidJson)?;
    if !valid_json_document(source) {
        return Err(JsonViewError::InvalidJson);
    }
    let mut output = Builder::new(display_budget_bytes);
    pretty_json(&mut output, source, 0)?;
    Ok(output.finish())
}

pub fn render_json_row(
    columns: &[Column],
    row: &JsonViewRow,
    resolved: &BTreeMap<usize, Value>,
    display_budget_bytes: usize,
) -> std::result::Result<String, JsonViewError> {
    row_impl(columns, row, resolved, display_budget_bytes, false).map(|(json, _)| json)
}

pub fn render_json_page(
    columns: &[Column],
    rows: &[JsonViewRow],
    resolved: &BTreeMap<(usize, usize), Value>,
    display_budget_bytes: usize,
) -> std::result::Result<String, JsonViewError> {
    if rows.is_empty() {
        return Err(JsonViewError::InvalidInput);
    }
    for &(row, column) in resolved.keys() {
        if row >= rows.len() || column >= columns.len() {
            return Err(JsonViewError::InvalidResolution);
        }
    }
    let mut output = Builder::new(display_budget_bytes);
    output.push("[\n")?;
    for (row_index, row) in rows.iter().enumerate() {
        let row_resolved: BTreeMap<usize, Value> = resolved
            .range((row_index, 0)..=(row_index, usize::MAX))
            .map(|(&(_, column), value)| (column, value.clone()))
            .collect();
        let (row_json, _) = row_impl(columns, row, &row_resolved, display_budget_bytes, false)?;
        if row_index > 0 {
            output.push(",\n")?;
        }
        output.push("  ")?;
        for (index, part) in row_json.split('\n').enumerate() {
            if index > 0 {
                output.push("\n  ")?;
            }
            output.push(part)?;
        }
    }
    output.push("\n]")?;
    Ok(output.finish())
}

/// The message the desktop shows when a JSON view cannot render.
pub fn json_view_error_message(
    error: JsonViewError,
    display_budget_bytes: usize,
    cell: bool,
) -> String {
    let scope = if cell { "cell" } else { "row" };
    match error {
        JsonViewError::Deferred => {
            "Load every deferred value before viewing this row as JSON.".into()
        }
        JsonViewError::InvalidResolution => "A loaded row value is invalid.".into(),
        JsonViewError::IncompleteResolution => {
            "A loaded row value is invalid or incomplete.".into()
        }
        JsonViewError::NonFinite => {
            "This row contains a non-finite number that JSON cannot represent.".into()
        }
        JsonViewError::DisplayLimit => format!(
            "The JSON output exceeds {} bytes of display storage (16 MiB maximum).",
            (display_budget_bytes / 2 * 2).min(MAX_JSON_VIEW_BYTES)
        ),
        JsonViewError::EncodedLimit => "The JSON output exceeds 16 MiB of encoded text.".into(),
        JsonViewError::InvalidJson => {
            "This JSON/JSONB value is invalid or exceeds the 16 MiB JSON limit.".into()
        }
        JsonViewError::Unavailable => "This row contains an unavailable value.".into(),
        JsonViewError::InvalidInput => format!("This {scope} contains invalid JSON view input."),
    }
}
