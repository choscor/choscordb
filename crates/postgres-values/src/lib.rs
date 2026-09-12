//! PostgreSQL binary scalar decoding. Inputs must come from a UTF-8 connection.
//! Protocol reference: PostgreSQL numeric_send and datatype/timestamp.h.
use choscordb_driver_api::{DriverError, ErrorKind, Result, Value};
use std::fmt::Write;
fn malformed() -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, "Malformed PostgreSQL binary value")
}
fn bound(n: usize, max: usize) -> Result<()> {
    if n > max {
        Err(DriverError::new(
            ErrorKind::ResourceLimit,
            "Decoded value exceeds byte budget",
        ))
    } else {
        Ok(())
    }
}
fn array<const N: usize>(b: &[u8]) -> Result<[u8; N]> {
    b.try_into().map_err(|_| malformed())
}
/// Decode without an allocation budget. Prefer `decode_bounded` in result pipelines.
pub fn decode(oid: u32, bytes: Option<&[u8]>) -> Result<Value> {
    decode_bounded(oid, bytes, usize::MAX)
}
/// `max_bytes` bounds owned payload capacity (excluding the fixed `Value` structure).
/// No payload allocation occurs before its conservative capacity check.
pub fn decode_bounded(oid: u32, bytes: Option<&[u8]>, max_bytes: usize) -> Result<Value> {
    let Some(b) = bytes else {
        return Ok(Value::Null);
    };
    if oid == 1700 {
        return numeric(b, max_bytes).map(Value::Decimal);
    }
    match oid {
        16 => match b {
            [0] => Ok(Value::Bool(false)),
            [1] => Ok(Value::Bool(true)),
            _ => Err(malformed()),
        },
        21 => Ok(Value::Integer(i16::from_be_bytes(array(b)?) as i64)),
        23 => Ok(Value::Integer(i32::from_be_bytes(array(b)?) as i64)),
        20 => Ok(Value::Integer(i64::from_be_bytes(array(b)?))),
        700 => Ok(Value::Real(f32::from_be_bytes(array(b)?) as f64)),
        701 => Ok(Value::Real(f64::from_be_bytes(array(b)?))),
        17 => {
            bound(b.len(), max_bytes)?;
            Ok(Value::Binary(b.to_vec()))
        }
        18 | 19 | 25 | 1042 | 1043 | 114 | 142 | 3802 => {
            if oid == 18 && b.len() != 1 {
                return Err(malformed());
            }
            let b = if oid == 3802 {
                if b.first() != Some(&1) {
                    return Err(malformed());
                }
                &b[1..]
            } else {
                b
            };
            bound(b.len(), max_bytes)?;
            let s = std::str::from_utf8(b).map_err(|_| malformed())?.to_owned();
            Ok(if oid == 114 || oid == 3802 {
                Value::Json(s)
            } else {
                Value::Text(s)
            })
        }
        1082 | 1083 | 1114 | 1184 | 1266 => temporal(oid, b, max_bytes),
        2950 => {
            let b = array::<16>(b)?;
            bound(36, max_bytes)?;
            let mut s = String::with_capacity(36);
            for (i, v) in b.iter().enumerate() {
                if [4, 6, 8, 10].contains(&i) {
                    s.push('-')
                }
                write!(s, "{v:02x}").unwrap();
            }
            Ok(Value::Uuid(s))
        }
        _ => Err(DriverError::new(
            ErrorKind::Unsupported,
            "Unsupported PostgreSQL binary type",
        )),
    }
}
fn numeric(b: &[u8], max: usize) -> Result<String> {
    if b.len() < 8 {
        return Err(malformed());
    }
    let n = u16::from_be_bytes(array(&b[..2])?) as usize;
    let weight = i16::from_be_bytes(array(&b[2..4])?) as i32;
    let sign = u16::from_be_bytes(array(&b[4..6])?);
    let scale = u16::from_be_bytes(array(&b[6..8])?) as usize;
    if b.len() != 8 + n * 2 || scale > 16383 {
        return Err(malformed());
    }
    let digit = |idx: usize| u16::from_be_bytes([b[8 + idx * 2], b[9 + idx * 2]]);
    for i in 0..n {
        if digit(i) >= 10000 {
            return Err(malformed());
        }
    }
    if matches!(sign, 0xc000 | 0xd000 | 0xf000) {
        // PostgreSQL validates the wire fields, but special values use only sign.
        // In particular numeric_send emits dscale=32 for infinity on PostgreSQL 17.
        let s = match sign {
            0xc000 => "NaN",
            0xd000 => "Infinity",
            _ => "-Infinity",
        };
        bound(s.len(), max)?;
        return Ok(s.into());
    }
    if !matches!(sign, 0 | 0x4000) {
        return Err(malformed());
    }
    // Reject fractional precision that the declared scale would silently lose.
    for i in 0..n {
        let position = weight - i as i32;
        if position < 0 {
            let first_decimal = (-position - 1) as usize * 4;
            let kept = scale.saturating_sub(first_decimal).min(4);
            let divisor = [10000, 1000, 100, 10, 1][kept];
            if digit(i) % divisor != 0 {
                return Err(malformed());
            }
        }
    }
    let cap = (weight + 1).max(1) as usize * 4 + scale + 2;
    bound(cap, max)?;
    let mut s = String::with_capacity(cap);
    if sign == 0x4000 && (0..n).any(|i| digit(i) != 0) {
        s.push('-')
    }
    let group = |position: i32| {
        let i = weight - position;
        if i >= 0 && (i as usize) < n {
            digit(i as usize)
        } else {
            0
        }
    };
    if weight < 0 {
        s.push('0')
    } else {
        write!(s, "{}", group(weight)).unwrap();
        for p in (0..weight).rev() {
            write!(s, "{:04}", group(p)).unwrap();
        }
    }
    if scale > 0 {
        s.push('.');
        for i in 0..scale {
            let g = group(-1 - (i / 4) as i32);
            let divisor = [1000, 100, 10, 1][i % 4];
            s.push(char::from(b'0' + ((g / divisor) % 10) as u8));
        }
    }
    Ok(s)
}

fn temporal(oid: u32, b: &[u8], max: usize) -> Result<Value> {
    // Fixed upper bound includes seven-digit years, BC marker and UTC suffix.
    bound(48, max)?;
    let mut s = String::with_capacity(48);
    if oid == 1082 {
        let days = i32::from_be_bytes(array(b)?);
        match days {
            i32::MIN => s.push_str("-infinity"),
            i32::MAX => s.push_str("infinity"),
            _ => {
                if !(-2_451_545..2_145_031_949).contains(&days) {
                    return Err(malformed());
                }
                let bc = date(&mut s, i64::from(days));
                if bc {
                    s.push_str(" BC")
                }
            }
        }
        return Ok(Value::Date(s));
    }
    if oid == 1266 {
        let b = array::<12>(b)?;
        let micros = i64::from_be_bytes(array(&b[..8])?);
        let west = i32::from_be_bytes(array(&b[8..])?);
        if !(0..=86_400_000_000).contains(&micros) || !(-57_599..=57_599).contains(&west) {
            return Err(malformed());
        }
        time(&mut s, micros);
        let offset = west.unsigned_abs();
        write!(
            s,
            "{}{:02}:{:02}",
            if west > 0 { '-' } else { '+' },
            offset / 3600,
            offset / 60 % 60
        )
        .unwrap();
        if offset % 60 != 0 {
            write!(s, ":{:02}", offset % 60).unwrap();
        }
        return Ok(Value::Time(s));
    }
    let micros = i64::from_be_bytes(array(b)?);
    if oid == 1083 {
        if !(0..=86_400_000_000).contains(&micros) {
            return Err(malformed());
        }
        time(&mut s, micros);
        return Ok(Value::Time(s));
    }
    match micros {
        i64::MIN => s.push_str("-infinity"),
        i64::MAX => s.push_str("infinity"),
        _ => {
            if !(-211_813_488_000_000_000..9_223_371_331_200_000_000).contains(&micros) {
                return Err(malformed());
            }
            let bc = date(&mut s, micros.div_euclid(86_400_000_000));
            s.push(' ');
            time(&mut s, micros.rem_euclid(86_400_000_000));
            if oid == 1184 {
                s.push_str("+00:00")
            }
            if bc {
                s.push_str(" BC")
            }
        }
    }
    Ok(Value::Timestamp(s))
}
fn time(s: &mut String, micros: i64) {
    let seconds = micros / 1_000_000;
    write!(
        s,
        "{:02}:{:02}:{:02}",
        seconds / 3600,
        seconds / 60 % 60,
        seconds % 60
    )
    .unwrap();
    let fraction = micros % 1_000_000;
    if fraction != 0 {
        write!(s, ".{fraction:06}").unwrap();
        while s.ends_with('0') {
            s.pop();
        }
    }
}
fn date(s: &mut String, days: i64) -> bool {
    // Gregorian 400-year eras, measured from March 1 of astronomical year zero.
    let z = days + 10_957 + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let mut year = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = mp + if mp < 10 { 3 } else { -9 };
    year += i64::from(month <= 2);
    let bc = year <= 0;
    write!(
        s,
        "{:04}-{month:02}-{day:02}",
        if bc { 1 - year } else { year }
    )
    .unwrap();
    bc
}
