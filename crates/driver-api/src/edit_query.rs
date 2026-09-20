//! Deliberately narrow shape recognition for editable query results.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Token {
    text: String,
    quoted: bool,
}
fn lex(sql: &str) -> Option<Vec<Token>> {
    let mut out = Vec::new();
    let mut chars = sql.chars().peekable();
    while let Some(c) = chars.next() {
        if c.is_whitespace() {
            continue;
        }
        if c == '-' && chars.peek() == Some(&'-') || c == '/' && chars.peek() == Some(&'*') {
            return None;
        }
        if c == '"' {
            let mut value = String::new();
            loop {
                match chars.next()? {
                    '"' if chars.peek() == Some(&'"') => {
                        chars.next();
                        value.push('"');
                    }
                    '"' => break,
                    ch => value.push(ch),
                }
            }
            out.push(Token {
                text: value,
                quoted: true,
            });
        } else if c == '\'' {
            loop {
                match chars.next()? {
                    '\'' if chars.peek() == Some(&'\'') => {
                        chars.next();
                    }
                    '\'' => break,
                    _ => {}
                }
            }
            out.push(Token {
                text: "<literal>".into(),
                quoted: true,
            });
        } else if c.is_alphanumeric() || c == '_' {
            let mut value = c.to_string();
            while chars
                .peek()
                .is_some_and(|ch| ch.is_alphanumeric() || *ch == '_' || *ch == '$')
            {
                value.push(chars.next()?);
            }
            out.push(Token {
                text: value,
                quoted: false,
            });
        } else if ".,*();=<>+-/!?$".contains(c) {
            out.push(Token {
                text: c.to_string(),
                quoted: false,
            });
        } else {
            return None;
        }
    }
    Some(out)
}
fn keyword(token: &Token, value: &str) -> bool {
    !token.quoted && token.text.eq_ignore_ascii_case(value)
}
fn identifier(token: &Token) -> bool {
    token.quoted
        || token
            .text
            .chars()
            .next()
            .is_some_and(|c| c.is_alphabetic() || c == '_')
            && token
                .text
                .chars()
                .all(|c| c.is_alphanumeric() || c == '_' || c == '$')
}

#[derive(Clone, Debug)]
pub struct SimpleSelect {
    pub schema: Option<String>,
    pub table: String,
    /// Empty entries represent computed result columns.
    pub source_columns: Vec<String>,
    pub wildcard: bool,
}

pub fn simple_select(sql: &str, result_columns: &[String]) -> Option<SimpleSelect> {
    let tokens = lex(sql)?;
    if tokens.len() < 4 || !keyword(&tokens[0], "SELECT") {
        return None;
    }
    if tokens[1..].iter().any(|token| keyword(token, "SELECT")) {
        return None;
    }
    let from = tokens.iter().position(|t| keyword(t, "FROM"))?;
    if from < 2 || from + 1 >= tokens.len() {
        return None;
    }
    let table_start = from + 1;
    if !identifier(&tokens[table_start]) {
        return None;
    }
    let (schema, table, mut tail) = if tokens.get(table_start + 1).is_some_and(|t| t.text == ".") {
        let table = tokens.get(table_start + 2)?;
        if !identifier(table) {
            return None;
        }
        (
            Some(tokens[table_start].text.clone()),
            table.text.clone(),
            table_start + 3,
        )
    } else {
        (None, tokens[table_start].text.clone(), table_start + 1)
    };
    let mut alias = table.clone();
    if tokens.get(tail).is_some_and(|t| keyword(t, "AS")) {
        tail += 1;
        alias = tokens.get(tail)?.text.clone();
        if !identifier(&tokens[tail]) {
            return None;
        }
        tail += 1;
    } else if tokens.get(tail).is_some_and(|t| {
        identifier(t)
            && !["WHERE", "ORDER", "LIMIT", "OFFSET"]
                .iter()
                .any(|k| keyword(t, k))
    }) {
        alias = tokens[tail].text.clone();
        tail += 1;
    }
    if tokens[tail..].iter().any(|t| {
        [
            "JOIN",
            "FROM",
            "SELECT",
            "UNION",
            "INTERSECT",
            "EXCEPT",
            "GROUP",
            "HAVING",
            "WINDOW",
            "INTO",
        ]
        .iter()
        .any(|k| keyword(t, k))
    }) {
        return None;
    }
    if tokens[tail..].iter().filter(|t| t.text == ";").count() > 1
        || tokens[tail..]
            .iter()
            .position(|t| t.text == ";")
            .is_some_and(|i| i + 1 != tokens.len() - tail)
    {
        return None;
    }
    if tokens.get(tail).is_some_and(|t| {
        !["WHERE", "ORDER", "LIMIT", "OFFSET"]
            .iter()
            .any(|k| keyword(t, k))
            && t.text != ";"
    }) {
        return None;
    }
    let mut groups = Vec::new();
    let mut start = 1;
    let mut depth = 0i32;
    for i in 1..=from {
        if i < from && tokens[i].text == "(" {
            depth += 1;
        }
        if i < from && tokens[i].text == ")" {
            depth -= 1;
        }
        if depth < 0 {
            return None;
        }
        if i == from || depth == 0 && tokens[i].text == "," {
            if start == i {
                return None;
            }
            groups.push(&tokens[start..i]);
            start = i + 1;
        }
    }
    if depth != 0 {
        return None;
    }
    let wildcard = groups.len() == 1
        && (groups[0].len() == 1 && groups[0][0].text == "*"
            || groups[0].len() == 3
                && groups[0][1].text == "."
                && groups[0][2].text == "*"
                && [alias.as_str(), table.as_str()].contains(&groups[0][0].text.as_str()));
    if wildcard {
        return Some(SimpleSelect {
            schema,
            table,
            source_columns: result_columns.to_vec(),
            wildcard: true,
        });
    }
    if groups.len() != result_columns.len() {
        return None;
    }
    let mut source_columns = Vec::new();
    for (group, output) in groups.into_iter().zip(result_columns) {
        let mut end = group.len();
        let explicit_alias =
            if end >= 3 && keyword(&group[end - 2], "AS") && identifier(&group[end - 1]) {
                end -= 2;
                Some(&group[group.len() - 1].text)
            } else if matches!(end, 2 | 4) && identifier(&group[end - 1]) {
                end -= 1;
                Some(&group[group.len() - 1].text)
            } else {
                None
            };
        let direct = if end == 1 && identifier(&group[0]) {
            Some(group[0].text.as_str())
        } else if end == 3
            && identifier(&group[0])
            && group[1].text == "."
            && identifier(&group[2])
            && [alias.as_str(), table.as_str()].contains(&group[0].text.as_str())
        {
            Some(group[2].text.as_str())
        } else {
            None
        };
        if let Some(name) = direct {
            if explicit_alias.map_or(output == name, |a| output == a) {
                source_columns.push(name.into());
            } else {
                return None;
            }
        } else {
            source_columns.push(String::new());
        }
    }
    Some(SimpleSelect {
        schema,
        table,
        source_columns,
        wildcard: false,
    })
}
