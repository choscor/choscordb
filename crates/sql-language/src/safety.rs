use crate::scanner::{scan, statement_ranges};
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
    let (tokens, _, valid) = scan(sql);
    if !valid || parse(sql).is_none_or(|tree| tree.root_node().has_error()) {
        return Safety::ConfirmationRequired;
    }
    for range in statement_ranges(sql) {
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
