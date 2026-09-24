use choscordb_driver_api::{DriverError, ErrorKind, Value};
use std::cmp::Ordering;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FilterOperator {
    Contains,
    Like,
    NotLike,
    In,
    Sql,
    Equals,
    NotEquals,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
    IsNull,
    IsNotNull,
}

#[derive(Clone, Debug, PartialEq)]
pub struct FilterCondition {
    pub column: usize,
    pub operator: FilterOperator,
    pub value: Option<Value>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SortDirection {
    Ascending,
    Descending,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ResultSort {
    pub column: usize,
    pub direction: SortDirection,
}

fn invalid(message: &str) -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, message)
}

pub(crate) fn cmp(left: &Value, right: &Value) -> Result<Ordering, DriverError> {
    use Value::*;
    let ordering = match (left, right) {
        (Deferred { .. }, _) | (_, Deferred { .. }) => {
            return Err(invalid("Deferred values cannot be filtered or sorted"));
        }
        (Bool(a), Bool(b)) => a.cmp(b),
        (Bool(a), Integer(b)) => i64::from(*a).cmp(b),
        (Integer(a), Bool(b)) => a.cmp(&i64::from(*b)),
        (Integer(a), Integer(b)) => a.cmp(b),
        (Real(a), Real(b)) => a
            .partial_cmp(b)
            .ok_or_else(|| invalid("NaN values cannot be filtered or sorted"))?,
        (Integer(a), Real(b)) => integer_real_cmp(*a, *b)?,
        (Real(a), Integer(b)) => integer_real_cmp(*b, *a)?.reverse(),
        (Decimal(a), Decimal(b)) => decimal_cmp(a, b)?,
        (Decimal(a), Integer(b)) => decimal_cmp(a, &b.to_string())?,
        (Integer(a), Decimal(b)) => decimal_cmp(&a.to_string(), b)?,
        (Decimal(a), Real(b)) => decimal_cmp(a, &real_decimal(*b)?)?,
        (Real(a), Decimal(b)) => decimal_cmp(&real_decimal(*a)?, b)?,
        (Text(a), Text(b)) | (Uuid(a), Uuid(b)) => a.to_lowercase().cmp(&b.to_lowercase()),
        (Date(a), Text(b)) | (Date(a), Date(b)) => date_cmp(a, b)?,
        (Text(a), Date(b)) => date_cmp(a, b)?,
        (Time(a), Text(b)) | (Time(a), Time(b)) => time_cmp(a, b)?,
        (Text(a), Time(b)) => time_cmp(a, b)?,
        (Timestamp(a), Text(b)) | (Timestamp(a), Timestamp(b)) => timestamp_cmp(a, b)?,
        (Text(a), Timestamp(b)) => timestamp_cmp(a, b)?,
        (Binary(a), Binary(b)) => a.cmp(b),
        _ => return Err(invalid("Filter value does not match the column value type")),
    };
    Ok(ordering)
}

fn parse_date(value: &str) -> Option<i64> {
    let bytes = value.as_bytes();
    if bytes.len() != 10
        || bytes[4] != b'-'
        || bytes[7] != b'-'
        || !bytes[..4]
            .iter()
            .chain(&bytes[5..7])
            .chain(&bytes[8..])
            .all(u8::is_ascii_digit)
    {
        return None;
    }
    let year = value[0..4].parse::<i64>().ok()?;
    let month = value[5..7].parse::<u32>().ok()?;
    let day = value[8..10].parse::<u32>().ok()?;
    let leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    let days = match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if leap => 29,
        2 => 28,
        _ => return None,
    };
    if day == 0 || day > days {
        return None;
    }
    // Howard Hinnant's civil-date mapping, relative to 1970-01-01.
    let adjusted = year - i64::from(month <= 2);
    let era = adjusted.div_euclid(400);
    let year_of_era = adjusted - era * 400;
    let shifted_month = i64::from(month) + if month > 2 { -3 } else { 9 };
    let day_of_year = (153 * shifted_month + 2) / 5 + i64::from(day) - 1;
    let day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    Some(era * 146_097 + day_of_era - 719_468)
}

fn date_cmp(left: &str, right: &str) -> Result<Ordering, DriverError> {
    Ok(parse_date(left)
        .ok_or_else(|| invalid("Invalid date filter value"))?
        .cmp(&parse_date(right).ok_or_else(|| invalid("Invalid date filter value"))?))
}

fn fraction(value: Option<&str>) -> Option<String> {
    if value == Some("") {
        return None;
    }
    let value = value.unwrap_or("");
    if !value.bytes().all(|byte| byte.is_ascii_digit()) {
        return None;
    }
    Some(value.trim_end_matches('0').to_owned())
}

fn fraction_cmp(left: &str, right: &str) -> Ordering {
    let length = left.len().max(right.len());
    (0..length)
        .map(|index| left.as_bytes().get(index).copied().unwrap_or(b'0'))
        .cmp((0..length).map(|index| right.as_bytes().get(index).copied().unwrap_or(b'0')))
}

struct ParsedTime {
    negative: bool,
    seconds: i128,
    fraction: String,
}

fn parse_time(value: &str, duration: bool) -> Option<ParsedTime> {
    let (negative, value) = if duration {
        value
            .strip_prefix('-')
            .map_or((false, value), |value| (true, value))
    } else {
        (false, value)
    };
    let value = if negative {
        value
    } else {
        value.strip_prefix('+').unwrap_or(value)
    };
    let (clock, fractional) = value
        .split_once('.')
        .map_or((value, None), |(clock, value)| (clock, Some(value)));
    let mut fields = clock.split(':');
    let hours = fields.next()?.parse::<u64>().ok()?;
    let minutes = fields.next()?.parse::<u64>().ok()?;
    let seconds = fields.next()?.parse::<u64>().ok()?;
    if fields.next().is_some() || minutes > 59 || seconds > 59 || (!duration && hours > 23) {
        return None;
    }
    let fraction = fraction(fractional)?;
    let seconds = i128::from(hours)
        .checked_mul(3600)?
        .checked_add(i128::from(minutes) * 60)?
        .checked_add(i128::from(seconds))?;
    let zero = seconds == 0 && fraction.is_empty();
    Some(ParsedTime {
        negative: negative && !zero,
        seconds,
        fraction,
    })
}

fn time_cmp(left: &str, right: &str) -> Result<Ordering, DriverError> {
    let left = parse_time(left, true).ok_or_else(|| invalid("Invalid time filter value"))?;
    let right = parse_time(right, true).ok_or_else(|| invalid("Invalid time filter value"))?;
    if left.negative != right.negative {
        return Ok(if left.negative {
            Ordering::Less
        } else {
            Ordering::Greater
        });
    }
    let magnitude = left
        .seconds
        .cmp(&right.seconds)
        .then_with(|| fraction_cmp(&left.fraction, &right.fraction));
    Ok(if left.negative {
        magnitude.reverse()
    } else {
        magnitude
    })
}

fn parse_offset(value: &str) -> Option<i64> {
    let sign = if value.starts_with('-') {
        -1
    } else if value.starts_with('+') {
        1
    } else {
        return None;
    };
    let value = &value[1..];
    let (hours, minutes) = if let Some((hours, minutes)) = value.split_once(':') {
        (hours.parse::<i64>().ok()?, minutes.parse::<i64>().ok()?)
    } else if value.len() == 4 {
        (
            value[..2].parse::<i64>().ok()?,
            value[2..].parse::<i64>().ok()?,
        )
    } else {
        (value.parse::<i64>().ok()?, 0)
    };
    if hours > 23 || minutes > 59 {
        return None;
    }
    Some(sign * (hours * 3600 + minutes * 60))
}

fn parse_timestamp(value: &str) -> Option<(i128, String, bool)> {
    if value.len() < 19 || !matches!(value.as_bytes().get(10), Some(b' ' | b'T')) {
        return None;
    }
    let days = i128::from(parse_date(value.get(..10)?)?);
    let mut time = value.get(11..)?;
    let (offset, aware) = if let Some(stripped) = time.strip_suffix(['Z', 'z']) {
        time = stripped;
        (0, true)
    } else if let Some(at) = time
        .char_indices()
        .skip(1)
        .find_map(|(index, character)| matches!(character, '+' | '-').then_some(index))
    {
        let offset = parse_offset(&time[at..])?;
        time = &time[..at];
        (offset, true)
    } else {
        (0, false)
    };
    let time = parse_time(time, false)?;
    let seconds = days
        .checked_mul(86_400)?
        .checked_add(time.seconds)?
        .checked_sub(i128::from(offset))?;
    Some((seconds, time.fraction, aware))
}

fn timestamp_cmp(left: &str, right: &str) -> Result<Ordering, DriverError> {
    let left = parse_timestamp(left).ok_or_else(|| invalid("Invalid timestamp filter value"))?;
    let right = parse_timestamp(right).ok_or_else(|| invalid("Invalid timestamp filter value"))?;
    if left.2 != right.2 {
        return Err(invalid(
            "Timestamp values must both include timezone offsets or both omit them",
        ));
    }
    Ok(left
        .0
        .cmp(&right.0)
        .then_with(|| fraction_cmp(&left.1, &right.1)))
}

pub(crate) fn sort_key(value: &Value, database_type: &str) -> Result<Value, DriverError> {
    let declared = database_type.trim().to_ascii_uppercase();
    match value {
        Value::Text(value) if declared.contains("TIMESTAMP") || declared.contains("DATETIME") => {
            parse_timestamp(value).ok_or_else(|| invalid("Invalid timestamp value"))?;
            Ok(Value::Timestamp(value.clone()))
        }
        Value::Text(value) if declared == "DATE" || declared.starts_with("DATE(") => {
            parse_date(value).ok_or_else(|| invalid("Invalid date value"))?;
            Ok(Value::Date(value.clone()))
        }
        Value::Text(value) if declared == "TIME" || declared.starts_with("TIME(") => {
            parse_time(value, true).ok_or_else(|| invalid("Invalid time value"))?;
            Ok(Value::Time(value.clone()))
        }
        _ => Ok(value.clone()),
    }
}

fn real_decimal(value: f64) -> Result<String, DriverError> {
    if value.is_finite() {
        Ok(value.to_string())
    } else {
        Err(invalid("Non-finite values cannot be compared as decimals"))
    }
}

fn json_equal(left: &str, right: &str) -> Result<bool, DriverError> {
    fn equal(left: &serde_json::Value, right: &serde_json::Value) -> Result<bool, DriverError> {
        Ok(match (left, right) {
            (serde_json::Value::Null, serde_json::Value::Null) => true,
            (serde_json::Value::Bool(left), serde_json::Value::Bool(right)) => left == right,
            (serde_json::Value::String(left), serde_json::Value::String(right)) => left == right,
            (serde_json::Value::Number(left), serde_json::Value::Number(right)) => {
                decimal_cmp(&left.to_string(), &right.to_string())? == Ordering::Equal
            }
            (serde_json::Value::Array(left), serde_json::Value::Array(right)) => {
                if left.len() != right.len() {
                    false
                } else {
                    left.iter()
                        .zip(right)
                        .try_fold(true, |same, (left, right)| Ok(same && equal(left, right)?))?
                }
            }
            (serde_json::Value::Object(left), serde_json::Value::Object(right)) => {
                if left.len() != right.len() {
                    false
                } else {
                    left.iter().try_fold(true, |same, (key, left)| {
                        if !same {
                            return Ok(false);
                        }
                        match right.get(key) {
                            Some(right) => equal(left, right),
                            None => Ok(false),
                        }
                    })?
                }
            }
            _ => false,
        })
    }
    let left = serde_json::from_str(left).map_err(|_| invalid("Invalid JSON filter value"))?;
    let right = serde_json::from_str(right).map_err(|_| invalid("Invalid JSON filter value"))?;
    equal(&left, &right)
}

fn integer_real_cmp(integer: i64, real: f64) -> Result<Ordering, DriverError> {
    if real.is_nan() {
        return Err(invalid("NaN values cannot be filtered or sorted"));
    }
    if real >= 2_f64.powi(63) {
        return Ok(Ordering::Less);
    }
    if real < -2_f64.powi(63) {
        return Ok(Ordering::Greater);
    }
    let whole = real.trunc() as i64;
    Ok(integer.cmp(&whole).then_with(|| {
        if real.fract() > 0.0 {
            Ordering::Less
        } else if real.fract() < 0.0 {
            Ordering::Greater
        } else {
            Ordering::Equal
        }
    }))
}

pub(crate) fn sort_compare(
    left: &Value,
    right: &Value,
    direction: SortDirection,
) -> Result<Ordering, DriverError> {
    match (matches!(left, Value::Null), matches!(right, Value::Null)) {
        (true, true) => Ok(Ordering::Equal),
        (true, false) => Ok(Ordering::Greater),
        (false, true) => Ok(Ordering::Less),
        (false, false) => cmp(left, right).map(|ordering| {
            if direction == SortDirection::Descending {
                ordering.reverse()
            } else {
                ordering
            }
        }),
    }
}

fn decimal_cmp(left: &str, right: &str) -> Result<Ordering, DriverError> {
    struct Decimal {
        negative: bool,
        digits: Vec<u8>,
        point: i64,
    }
    fn parse(value: &str) -> Option<Decimal> {
        let (negative, value) = value
            .strip_prefix('-')
            .map_or((false, value), |v| (true, v));
        let value = if negative {
            value
        } else {
            value.strip_prefix('+').unwrap_or(value)
        };
        let (mantissa, exponent) = value.find(['e', 'E']).map_or(Some((value, 0)), |at| {
            let exponent = value[at + 1..].parse::<i64>().ok()?;
            Some((&value[..at], exponent))
        })?;
        let (whole, fraction) = mantissa.split_once('.').unwrap_or((mantissa, ""));
        if (whole.is_empty() && fraction.is_empty())
            || !whole
                .bytes()
                .chain(fraction.bytes())
                .all(|byte| byte.is_ascii_digit())
        {
            return None;
        }
        let mut digits = whole.bytes().chain(fraction.bytes()).collect::<Vec<_>>();
        let leading = digits
            .iter()
            .position(|digit| *digit != b'0')
            .unwrap_or(digits.len());
        let mut point = i64::try_from(whole.len())
            .ok()?
            .checked_add(exponent)?
            .checked_sub(i64::try_from(leading).ok()?)?;
        digits.drain(..leading);
        while digits.last() == Some(&b'0') {
            digits.pop();
        }
        if digits.is_empty() {
            point = 0;
        }
        Some(Decimal {
            negative: negative && !digits.is_empty(),
            digits,
            point,
        })
    }
    let a = parse(left).ok_or_else(|| invalid("Invalid decimal filter value"))?;
    let b = parse(right).ok_or_else(|| invalid("Invalid decimal filter value"))?;
    if a.negative != b.negative {
        return Ok(if a.negative {
            Ordering::Less
        } else {
            Ordering::Greater
        });
    }
    let magnitude = a.point.cmp(&b.point).then_with(|| {
        let length = a.digits.len().max(b.digits.len());
        (0..length)
            .map(|index| a.digits.get(index).copied().unwrap_or(b'0'))
            .cmp((0..length).map(|index| b.digits.get(index).copied().unwrap_or(b'0')))
    });
    Ok(if a.negative {
        magnitude.reverse()
    } else {
        magnitude
    })
}

pub fn value_matches(
    value: &Value,
    operator: FilterOperator,
    operand: Option<&Value>,
) -> Result<bool, DriverError> {
    if operator == FilterOperator::Sql {
        return Err(invalid("SQL predicates require a result row"));
    }
    if matches!(
        operator,
        FilterOperator::Like | FilterOperator::NotLike | FilterOperator::In
    ) {
        let filters = [FilterCondition {
            column: 0,
            operator,
            value: operand.cloned(),
        }];
        let columns = [choscordb_driver_api::Column {
            name: "value".into(),
            database_type: String::new(),
            precision: None,
            scale: None,
            timezone: None,
            nullable: None,
        }];
        return crate::result_predicate::Predicates::new(&columns, &filters, 8 * 1024 * 1024)?
            .row_matches(std::slice::from_ref(value), &filters);
    }
    if operator == FilterOperator::IsNull {
        return Ok(matches!(value, Value::Null));
    }
    if operator == FilterOperator::IsNotNull {
        return Ok(!matches!(value, Value::Null));
    }
    if matches!(value, Value::Deferred { .. }) {
        return Err(invalid("Deferred values cannot be filtered or sorted"));
    }
    if matches!(value, Value::Null) {
        return Ok(false);
    }
    let operand = operand.ok_or_else(|| invalid("Filter operator requires a value"))?;
    if matches!(operand, Value::Null | Value::Deferred { .. }) {
        return Err(invalid("Filter value is invalid"));
    }
    if operator == FilterOperator::Contains {
        return match (value, operand) {
            (Value::Text(value), Value::Text(needle)) => {
                Ok(value.to_lowercase().contains(&needle.to_lowercase()))
            }
            _ => Err(invalid("Contains requires text values")),
        };
    }
    if matches!((value, operand), (Value::Json(_), _) | (_, Value::Json(_))) {
        let (left, right) = match (value, operand) {
            (Value::Json(left), Value::Json(right) | Value::Text(right))
            | (Value::Text(left), Value::Json(right)) => (left, right),
            _ => return Err(invalid("JSON equality requires JSON text values")),
        };
        let equal = json_equal(left, right)?;
        return match operator {
            FilterOperator::Equals => Ok(equal),
            FilterOperator::NotEquals => Ok(!equal),
            _ => Err(invalid("JSON values support equality filters only")),
        };
    }
    if matches!(
        operator,
        FilterOperator::LessThan
            | FilterOperator::LessThanOrEqual
            | FilterOperator::GreaterThan
            | FilterOperator::GreaterThanOrEqual
    ) && !matches!(
        (value, operand),
        (
            Value::Integer(_) | Value::Real(_) | Value::Decimal(_),
            Value::Integer(_) | Value::Real(_) | Value::Decimal(_)
        )
    ) {
        return Err(invalid("Ordered filter comparisons require numeric values"));
    }
    let ordering = cmp(value, operand)?;
    Ok(match operator {
        FilterOperator::Equals => ordering == Ordering::Equal,
        FilterOperator::NotEquals => ordering != Ordering::Equal,
        FilterOperator::LessThan => ordering == Ordering::Less,
        FilterOperator::LessThanOrEqual => ordering != Ordering::Greater,
        FilterOperator::GreaterThan => ordering == Ordering::Greater,
        FilterOperator::GreaterThanOrEqual => ordering != Ordering::Less,
        _ => unreachable!(),
    })
}

pub(crate) fn validate_sort_value(row: &[Value], sort: ResultSort) -> Result<(), DriverError> {
    let value = row
        .get(sort.column)
        .ok_or_else(|| invalid("Sort column is out of range"))?;
    if matches!(value, Value::Deferred { .. }) {
        Err(invalid("Deferred values cannot be filtered or sorted"))
    } else {
        Ok(())
    }
}

pub(crate) fn page_estimated_bytes(rows: &Vec<Vec<Value>>) -> usize {
    std::mem::size_of::<choscordb_driver_api::ResultPage>()
        + rows.capacity() * std::mem::size_of::<Vec<Value>>()
        + rows
            .iter()
            .map(|row| {
                (row.capacity() - row.len()) * std::mem::size_of::<Value>()
                    + row.iter().map(Value::estimated_bytes).sum::<usize>()
            })
            .sum::<usize>()
}
