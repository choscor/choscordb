//! PostgreSQL transaction controls, deliberately narrower than arbitrary SQL.
use crate::{DriverError, ErrorKind, Result, TransactionCharacteristics, TransactionIsolation};
#[derive(Debug, PartialEq, Eq)]
pub enum Control {
    Sql,
    Begin(TransactionCharacteristics),
    Commit(bool),
    Rollback(bool),
}
fn unsupported() -> DriverError {
    DriverError::new(ErrorKind::InvalidInput, "Unsupported transaction control")
}
fn tokens(sql: &str) -> Result<Vec<String>> {
    let bytes = sql.as_bytes();
    let mut i = 0;
    let mut words = Vec::new();
    while i < bytes.len() {
        if bytes[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }
        if bytes[i..].starts_with(b"--") {
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            continue;
        }
        if bytes[i..].starts_with(b"/*") {
            i += 2;
            let mut depth = 1;
            while i < bytes.len() && depth > 0 {
                if bytes[i..].starts_with(b"/*") {
                    depth += 1;
                    i += 2;
                } else if bytes[i..].starts_with(b"*/") {
                    depth -= 1;
                    i += 2;
                } else {
                    i += 1;
                }
            }
            if depth != 0 {
                return Err(unsupported());
            }
            continue;
        }
        let start = i;
        if bytes[i].is_ascii_alphabetic() {
            while i < bytes.len() && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                i += 1;
            }
        } else {
            i += 1;
        }
        // All accepted controls consist of ASCII keywords. Ordinary SQL is passed through.
        words.push(String::from_utf8_lossy(&bytes[start..i]).to_ascii_uppercase());
        if words.len() == 1
            && !matches!(
                words[0].as_str(),
                "BEGIN" | "START" | "COMMIT" | "END" | "ROLLBACK" | "ABORT" | "PREPARE"
            )
        {
            return Ok(words);
        }
        if words.first().is_some_and(|word| word == "PREPARE")
            && words.len() == 2
            && words[1] != "TRANSACTION"
        {
            return Ok(words);
        }
        if words.first().is_some_and(|word| word == "ROLLBACK")
            && (words.get(1).is_some_and(|word| word == "TO")
                || words.get(2).is_some_and(|word| word == "TO"))
        {
            return Ok(words);
        }
    }
    if words.last().is_some_and(|word| word == ";") {
        words.pop();
    }
    Ok(words)
}
pub fn classify(sql: &str) -> Result<Control> {
    let words = tokens(sql)?;
    let text: Vec<&str> = words.iter().map(String::as_str).collect();
    let Some(first) = text.first().copied() else {
        return Ok(Control::Sql);
    };
    match first {
        "BEGIN" | "START" => {
            let mut rest = &text[1..];
            if first == "START" {
                if rest.first() != Some(&"TRANSACTION") {
                    return Err(unsupported());
                }
                rest = &rest[1..];
            } else if matches!(rest.first(), Some(&"WORK" | &"TRANSACTION")) {
                rest = &rest[1..];
            }
            Ok(Control::Begin(characteristics(rest)?))
        }
        "ROLLBACK"
            if text.get(1) == Some(&"TO")
                || (matches!(text.get(1), Some(&"WORK" | &"TRANSACTION"))
                    && text.get(2) == Some(&"TO")) =>
        {
            Ok(Control::Sql)
        }
        "COMMIT" | "END" | "ROLLBACK" | "ABORT" => {
            let mut rest = &text[1..];
            if matches!(rest.first(), Some(&"WORK" | &"TRANSACTION")) {
                rest = &rest[1..];
            }
            if !rest.is_empty() && rest != ["AND", "NO", "CHAIN"] && rest != ["AND", "CHAIN"] {
                return Err(unsupported());
            }
            Ok(if matches!(first, "COMMIT" | "END") {
                Control::Commit(rest == ["AND", "CHAIN"])
            } else {
                Control::Rollback(rest == ["AND", "CHAIN"])
            })
        }
        "PREPARE" if text.get(1) == Some(&"TRANSACTION") => Err(unsupported()),
        _ => Ok(Control::Sql),
    }
}
fn characteristics(mut words: &[&str]) -> Result<TransactionCharacteristics> {
    let mut value = TransactionCharacteristics::default();
    while !words.is_empty() {
        let consumed = match words {
            ["ISOLATION", "LEVEL", "READ", "UNCOMMITTED", ..] => {
                value.isolation = Some(TransactionIsolation::ReadUncommitted);
                4
            }
            ["ISOLATION", "LEVEL", "READ", "COMMITTED", ..] => {
                value.isolation = Some(TransactionIsolation::ReadCommitted);
                4
            }
            ["ISOLATION", "LEVEL", "REPEATABLE", "READ", ..] => {
                value.isolation = Some(TransactionIsolation::RepeatableRead);
                4
            }
            ["ISOLATION", "LEVEL", "SERIALIZABLE", ..] => {
                value.isolation = Some(TransactionIsolation::Serializable);
                3
            }
            ["READ", "ONLY", ..] => {
                value.read_only = Some(true);
                2
            }
            ["READ", "WRITE", ..] => {
                value.read_only = Some(false);
                2
            }
            ["DEFERRABLE", ..] => {
                value.deferrable = Some(true);
                1
            }
            ["NOT", "DEFERRABLE", ..] => {
                value.deferrable = Some(false);
                2
            }
            _ => return Err(unsupported()),
        };
        words = &words[consumed..];
        if words.first() == Some(&",") {
            words = &words[1..];
            if words.is_empty() {
                return Err(unsupported());
            }
        }
    }
    Ok(value)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn complete_controls_ignore_comments_but_do_not_swallow_richer_forms() {
        for sql in [
            "BEGIN",
            "/* nested /* x */ x */ BEGIN WORK; --end",
            "START TRANSACTION",
        ] {
            assert_eq!(classify(sql).unwrap(), Control::Begin(Default::default()));
        }
        for sql in [
            "COMMIT",
            "END WORK AND NO CHAIN;",
            "COMMIT /*x*/ TRANSACTION",
        ] {
            assert_eq!(classify(sql).unwrap(), Control::Commit(false));
        }
        for sql in ["ROLLBACK", "ABORT TRANSACTION AND NO CHAIN"] {
            assert_eq!(classify(sql).unwrap(), Control::Rollback(false));
        }
        for sql in [
            "ROLLBACK TO SAVEPOINT safe",
            "ROLLBACK WORK TO safe",
            "SELECT 'COMMIT'",
            "SAVEPOINT safe",
        ] {
            assert_eq!(classify(sql).unwrap(), Control::Sql);
        }
        for sql in [
            "COMMIT PREPARED 'x'",
            "ROLLBACK PREPARED 'x'",
            "PREPARE TRANSACTION 'x'",
            "BEGIN; SELECT 1",
            "END /*unfinished",
        ] {
            assert!(classify(sql).is_err(), "{sql}");
        }
    }
}
