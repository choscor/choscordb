use std::time::{SystemTime, UNIX_EPOCH};

// Gregorian 400-year era conversion.
pub(crate) fn civil_from_days(days: i64) -> (i32, u32, u32) {
    let z = days + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1_460 + doe / 36_524 - doe / 146_096) / 365;
    let mut year = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = mp + if mp < 10 { 3 } else { -9 };
    year += i64::from(month <= 2);
    (year as i32, month as u32, day as u32)
}

pub(crate) fn days_from_civil(year: i32, month: u32, day: u32) -> Option<i64> {
    if !(1..=12).contains(&month) || !(1..=31).contains(&day) || !(1..=9999).contains(&year) {
        return None;
    }
    let y = i64::from(year) - i64::from(month <= 2);
    let era = y.div_euclid(400);
    let yoe = y - era * 400;
    let m = i64::from(month);
    let d = i64::from(day);
    let mp = m + if m > 2 { -3 } else { 9 };
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    let result = era * 146_097 + doe - 719_468;
    (civil_from_days(result) == (year, month, day)).then_some(result)
}

pub(crate) fn today() -> i64 {
    let seconds = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64;
    seconds.div_euclid(86_400)
}

pub(crate) fn day_name(days: i64) -> String {
    let (year, month, day) = civil_from_days(days);
    format!("{year:04}-{month:02}-{day:02}")
}

pub(crate) fn parse_day(value: &str) -> Option<i64> {
    let bytes = value.as_bytes();
    if bytes.len() != 10
        || bytes[4] != b'-'
        || bytes[7] != b'-'
        || !bytes
            .iter()
            .enumerate()
            .all(|(index, byte)| index == 4 || index == 7 || byte.is_ascii_digit())
    {
        return None;
    }
    days_from_civil(
        value.get(0..4)?.parse().ok()?,
        value.get(5..7)?.parse().ok()?,
        value.get(8..10)?.parse().ok()?,
    )
}

pub(crate) fn now_utc() -> String {
    let duration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    let seconds = duration.as_secs() as i64;
    let (year, month, day) = civil_from_days(seconds.div_euclid(86_400));
    let second_of_day = seconds.rem_euclid(86_400);
    let hour = second_of_day / 3600;
    let minute = second_of_day % 3600 / 60;
    let second = second_of_day % 60;
    let millis = duration.subsec_millis();
    format!("{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}.{millis:03}Z")
}

pub(crate) fn timestamp_day(value: &str) -> Option<i64> {
    let bytes = value.as_bytes();
    if bytes.len() < 20 || bytes.get(10) != Some(&b'T') {
        return None;
    }
    let day = parse_day(value.get(..10)?)?;
    if bytes.get(13) != Some(&b':') || bytes.get(16) != Some(&b':') {
        return None;
    }
    let hour: u8 = value.get(11..13)?.parse().ok()?;
    let minute: u8 = value.get(14..16)?.parse().ok()?;
    let second: u8 = value.get(17..19)?.parse().ok()?;
    if hour > 23 || minute > 59 || second > 59 {
        return None;
    }
    let tail = value.get(19..)?;
    let valid_tail = tail == "Z"
        || tail == "+00:00"
        || tail.strip_prefix('.').is_some_and(|fraction| {
            let fraction = fraction
                .strip_suffix('Z')
                .or_else(|| fraction.strip_suffix("+00:00"));
            fraction.is_some_and(|digits| {
                !digits.is_empty()
                    && digits.len() <= 3
                    && digits.bytes().all(|byte| byte.is_ascii_digit())
            })
        });
    valid_tail.then_some(day)
}
