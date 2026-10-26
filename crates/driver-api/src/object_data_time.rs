//! Which rows an object data read returns from a system-versioned relation.
use crate::{DriverError, ErrorKind, Result};

/// A validated `YYYY-MM-DD HH:MM:SS[.ffffff]` time. It holds only ASCII
/// digits and `-`, ` `, `:`, `.`, so drivers may embed it in a quoted literal.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct HistoryTimestamp(String);

impl HistoryTimestamp {
    pub fn parse(text: &str) -> Result<Self> {
        let invalid = || {
            DriverError::new(
                ErrorKind::InvalidInput,
                "Enter a time as YYYY-MM-DD HH:MM:SS with up to six fraction digits",
            )
        };
        let bytes = text.as_bytes();
        let (seconds, fraction) = bytes.split_at(bytes.len().min(19));
        let shape = b"0000-00-00 00:00:00";
        if seconds.len() != shape.len()
            || !seconds.iter().zip(shape).all(|(byte, expected)| {
                if *expected == b'0' {
                    byte.is_ascii_digit()
                } else {
                    byte == expected
                }
            })
            || !(fraction.is_empty()
                || (fraction[0] == b'.'
                    && (2..=7).contains(&fraction.len())
                    && fraction[1..].iter().all(u8::is_ascii_digit)))
        {
            return Err(invalid());
        }
        let field = |range: std::ops::Range<usize>| {
            seconds[range]
                .iter()
                .fold(0u32, |total, digit| total * 10 + u32::from(digit - b'0'))
        };
        let (year, month, day) = (field(0..4), field(5..7), field(8..10));
        let leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        let days = match month {
            2 if leap => 29,
            2 => 28,
            4 | 6 | 9 | 11 => 30,
            _ => 31,
        };
        if !(1..=12).contains(&month)
            || !(1..=days).contains(&day)
            || field(11..13) > 23
            || field(14..16) > 59
            || field(17..19) > 59
        {
            return Err(invalid());
        }
        Ok(Self(text.to_owned()))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub enum ObjectDataTime {
    #[default]
    Current,
    AsOf(HistoryTimestamp),
    All,
}

impl ObjectDataTime {
    /// Rejects history modes on connections that cannot browse history.
    pub fn require_current(&self) -> Result<()> {
        if *self == Self::Current {
            return Ok(());
        }
        Err(DriverError::new(
            ErrorKind::Unsupported,
            "History browsing is unavailable for this connection",
        ))
    }
}

/// Whether objects of this relation subtype offer history browsing.
pub fn relation_subtype_has_history(subtype: &str) -> bool {
    subtype == "system_versioned"
}
