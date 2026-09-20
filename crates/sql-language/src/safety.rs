use crate::scanner::scan_with_mode;
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Safety {
    Ordinary,
    ConfirmationRequired,
}

/// Parse with the shipped Tree-sitter SQL grammar. Unsupported dialect syntax
/// remains an error and therefore requires confirmation rather than bypassing it.
pub fn parse(sql: &str) -> Option<tree_sitter::Tree> {
    let mut parser = tree_sitter::Parser::new();
    parser
        .set_language(&tree_sitter_sequel::LANGUAGE.into())
        .ok()?;
    parser.parse(sql, None)
}

pub fn classify(sql: &str) -> Safety {
    classify_dialect(sql, false)
}
pub(crate) fn classify_dialect(sql: &str, mysql: bool) -> Safety {
    classify_with_mode(sql, mysql.then_some(crate::MysqlSqlMode::default()))
}
pub(crate) fn classify_with_mode(sql: &str, mode: Option<crate::MysqlSqlMode>) -> Safety {
    let (tokens, mut ends, valid) = scan_with_mode(sql, mode);
    if !valid {
        return Safety::ConfirmationRequired;
    }
    ends.push(sql.len());
    let mut start = 0;
    for end in ends {
        let range = start..end;
        start = end;
        let source = &sql[range.clone()];
        if parse(source).is_none_or(|tree| tree.root_node().has_error())
            && !(mode.is_some() && mysql_metadata_statement(source))
        {
            return Safety::ConfirmationRequired;
        }
        let statement: Vec<_> = tokens.iter().filter(|t| range.contains(&t.start)).collect();
        for (i, token) in statement.iter().enumerate() {
            if matches!(token.word.as_str(), "DROP" | "TRUNCATE") {
                return Safety::ConfirmationRequired;
            }
            if matches!(token.word.as_str(), "UPDATE" | "DELETE") {
                let has_where = statement[i + 1..]
                    .iter()
                    .take_while(|next| {
                        next.depth >= token.depth
                            && !(next.word == ")" && next.depth == token.depth)
                    })
                    .any(|next| next.depth == token.depth && next.word == "WHERE");
                if !has_where {
                    return Safety::ConfirmationRequired;
                }
            }
        }
    }
    Safety::Ordinary
}

// The shared grammar does not cover several MySQL metadata commands. Recognize
// a closed, read-only subset rather than trusting a statement's first keyword.
// Backslashes in literals and ANSI double-quoted identifiers intentionally stay
// on the conservative parser path; lexical validity alone is not grammar proof.
fn mysql_metadata_statement(sql: &str) -> bool {
    use std::sync::OnceLock;
    static METADATA: OnceLock<regex::Regex> = OnceLock::new();
    METADATA.get_or_init(|| {
        let identifier = r"(?:[a-z_][a-z0-9_$]*|`(?:[^`]|``)+`)";
        let qualified = format!(r"{identifier}(?:\s*\.\s*{identifier})?");
        let database = format!(r"(?:\s+(?:FROM|IN)\s+{identifier})?");
        let like = r"(?:\s+LIKE\s+'(?:[^'\\]|'')*')?";
        let pattern = format!(
            r"(?ix)\A\s*(?:
                SHOW\s+(?:FULL\s+)?TABLES{database}{like}
                | SHOW\s+(?:DATABASES|SCHEMAS){like}
                | SHOW\s+(?:(?:GLOBAL|SESSION)\s+)?(?:VARIABLES|STATUS){like}
                | SHOW\s+CREATE\s+(?:TABLE|VIEW|DATABASE|SCHEMA|PROCEDURE|FUNCTION|TRIGGER|EVENT)\s+{qualified}
                | SHOW\s+(?:FULL\s+)?(?:COLUMNS|FIELDS)\s+(?:FROM|IN)\s+{qualified}{database}{like}
                | SHOW\s+(?:INDEX|INDEXES|KEYS)\s+(?:FROM|IN)\s+{qualified}{database}
                | (?:DESCRIBE|DESC)\s+{qualified}
                | SHOW\s+(?:FULL\s+)?PROCESSLIST
                | SHOW\s+(?:ENGINES|PLUGINS|PRIVILEGES|WARNINGS|ERRORS)
            )\s*;?\s*\z"
        );
        regex::Regex::new(&pattern).expect("constant MySQL metadata grammar")
    }).is_match(sql)
}
