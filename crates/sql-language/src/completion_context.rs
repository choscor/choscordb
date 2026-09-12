use std::ops::Range;

/// Locate the complete qualified identifier prefix at a UTF-8 cursor.
/// Returns none inside SQL strings/comments or outside editing limits.
pub fn completion_context(sql: &str, cursor: usize) -> Option<Range<usize>> {
    if sql.len() > crate::MAX_SEARCH_SOURCE_BYTES || !sql.is_char_boundary(cursor) {
        return None;
    }
    let sql = sql.get(..cursor)?;
    let bytes = sql.as_bytes();
    let mut i = 0;
    let mut start = None;
    let mut after_dot = false;
    while i < bytes.len() {
        if bytes[i..].starts_with(b"--") {
            i += 2;
            while i < bytes.len() && bytes[i] != b'\n' {
                i += 1;
            }
            if i == bytes.len() {
                return None;
            }
            start = None;
            after_dot = false;
        } else if bytes[i..].starts_with(b"/*") {
            i += 2;
            let mut depth = 1usize;
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
                return None;
            }
            start = None;
            after_dot = false;
        } else if bytes[i] == b'\'' {
            let escape = i > 0
                && matches!(bytes[i - 1], b'e' | b'E')
                && (i < 2
                    || !(bytes[i - 2].is_ascii_alphanumeric()
                        || bytes[i - 2] == b'_'
                        || bytes[i - 2] >= 128));
            i += 1;
            let mut closed = false;
            while i < bytes.len() {
                if escape && bytes[i] == b'\\' {
                    i = (i + 2).min(bytes.len());
                } else if bytes[i] == b'\'' {
                    i += 1;
                    if i < bytes.len() && bytes[i] == b'\'' {
                        i += 1;
                    } else {
                        closed = true;
                        break;
                    }
                } else {
                    i += 1;
                }
            }
            if !closed {
                return None;
            }
            start = None;
            after_dot = false;
        } else if bytes[i] == b'$' && dollar_end(bytes, i).is_some() {
            let end = dollar_end(bytes, i)?;
            let delimiter = &sql[i..=end];
            let offset = sql[end + 1..].find(delimiter)?;
            i = end + 1 + offset + delimiter.len();
            start = None;
            after_dot = false;
        } else if matches!(bytes[i], b'"' | b'`' | b'[') {
            if !after_dot {
                start = Some(i);
            }
            let close = if bytes[i] == b'[' { b']' } else { bytes[i] };
            i += 1;
            while i < bytes.len() {
                if bytes[i] == close {
                    i += 1;
                    if close != b']' && i < bytes.len() && bytes[i] == close {
                        i += 1;
                    } else {
                        break;
                    }
                } else {
                    i += 1;
                }
            }
            after_dot = false;
        } else if bytes[i].is_ascii_alphabetic() || bytes[i] == b'_' || bytes[i] >= 128 {
            if !after_dot {
                start = Some(i);
            }
            i += 1;
            while i < bytes.len()
                && (bytes[i].is_ascii_alphanumeric()
                    || matches!(bytes[i], b'_' | b'$')
                    || bytes[i] >= 128)
            {
                i += 1;
            }
            after_dot = false;
        } else if bytes[i] == b'.' && start.is_some() && !after_dot {
            after_dot = true;
            i += 1;
        } else {
            start = None;
            after_dot = false;
            i += 1;
        }
    }
    let start = start.unwrap_or(cursor);
    (cursor - start <= crate::MAX_COMPLETION_PREFIX_BYTES).then_some(start..cursor)
}

fn dollar_end(bytes: &[u8], start: usize) -> Option<usize> {
    let mut end = start + 1;
    if end < bytes.len()
        && (bytes[end].is_ascii_alphabetic() || bytes[end] == b'_' || bytes[end] >= 128)
    {
        end += 1;
        while end < bytes.len()
            && (bytes[end].is_ascii_alphanumeric() || bytes[end] == b'_' || bytes[end] >= 128)
        {
            end += 1;
        }
    }
    (bytes.get(end) == Some(&b'$')).then_some(end)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn qualified_unicode_prefix_and_empty_explicit_context() {
        for (sql, expected) in [
            ("SELECT public.us", "public.us"),
            (
                "SELECT \"odd.schema\".\"é space",
                "\"odd.schema\".\"é space",
            ),
            ("SELECT [odd name].co", "[odd name].co"),
            ("SELECT `odd name`.co", "`odd name`.co"),
            ("SELECT ", ""),
        ] {
            let range = completion_context(sql, sql.len()).unwrap();
            assert_eq!(&sql[range], expected);
        }
    }
    #[test]
    fn unicode_dollar_tags_suppress_literal_contents() {
        for sql in [
            "SELECT $é$ta",
            "SELECT $标签_2$ta",
            "SELECT $é$'x' -- still literal",
        ] {
            assert!(completion_context(sql, sql.len()).is_none(), "{sql}");
        }
        let sql = "SELECT $标签$body$标签$ col";
        let range = completion_context(sql, sql.len()).unwrap();
        assert_eq!(&sql[range], "col");
    }
    #[test]
    fn suppresses_strings_comments_and_invalid_or_excessive_ranges() {
        for sql in [
            "SELECT 'ta",
            "SELECT E'a\\'ta",
            "-- ta",
            "/* outer /* nested */ ta",
            "SELECT $$ta",
            "SELECT $tag$ta",
        ] {
            assert!(completion_context(sql, sql.len()).is_none(), "{sql}");
        }
        for sql in [
            "/* ta */ col",
            "SELECT 'ta' col",
            "-- ta\ncol",
            "SELECT $tag$ta$tag$ col",
        ] {
            let range = completion_context(sql, sql.len()).unwrap();
            assert_eq!(&sql[range], "col");
        }
        assert!(completion_context("é", 1).is_none());
        assert!(completion_context("abc", 4).is_none());
        let long = "x".repeat(257);
        assert!(completion_context(&long, long.len()).is_none());
    }
}
