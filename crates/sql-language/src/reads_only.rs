use crate::MysqlSqlMode;
use crate::format::{Kind, Token, lex};

const READ_STARTS: &[&str] = &[
    "SELECT", "WITH", "SHOW", "EXPLAIN", "DESCRIBE", "DESC", "VALUES", "TABLE", "BEGIN", "START",
    "COMMIT", "ROLLBACK", "END",
];

const WRITE_WORDS: &[&str] = &[
    "INSERT", "UPDATE", "DELETE", "MERGE", "UPSERT", "REPLACE", "CREATE", "ALTER", "DROP",
    "TRUNCATE", "GRANT", "REVOKE", "RENAME", "CALL", "EXEC", "EXECUTE", "DO", "COPY", "LOAD",
    "VACUUM", "ANALYZE", "REINDEX", "ATTACH", "DETACH", "PRAGMA", "SET", "LOCK", "INTO", "COMMENT",
    "CLUSTER", "REFRESH", "IMPORT", "HANDLER", "OPTIMIZE", "REPAIR", "FLUSH", "KILL",
];

fn statement_reads_only(tokens: &[&Token<'_>]) -> bool {
    let Some(first) = tokens.first() else {
        return true;
    };
    if first.kind != Kind::Word || !READ_STARTS.contains(&first.text.to_ascii_uppercase().as_str())
    {
        return false;
    }
    !tokens.iter().enumerate().any(|(index, token)| {
        token.kind == Kind::Word
            && WRITE_WORDS.contains(&token.text.to_ascii_uppercase().as_str())
            && tokens
                .get(index + 1)
                .is_none_or(|next| next.kind != Kind::Open)
    })
}

pub fn reads_only(sql: &str, mode: Option<MysqlSqlMode>) -> bool {
    let Ok(tokens) = lex(sql, mode) else {
        return false;
    };
    let code: Vec<&Token<'_>> = tokens
        .iter()
        .filter(|token| !matches!(token.kind, Kind::LineComment | Kind::BlockComment))
        .collect();
    if code.iter().all(|token| token.kind == Kind::Semicolon) {
        return false;
    }
    code.split(|token| token.kind == Kind::Semicolon)
        .all(statement_reads_only)
}
