//! Conservative local recognition; unknown statements never authorize idle rollback.
pub(super) fn is_write(sql: &str) -> bool {
    // Keep maintenance bookkeeping bounded even for unusually large public-driver SQL.
    if sql.len() > 64 * 1024 {
        return false;
    }
    let Some(tree) = choscordb_sql_language::parse(sql) else {
        return false;
    };
    if tree.root_node().has_error() {
        return false;
    }
    let mut cursor = tree.walk();
    let mut write = false;
    loop {
        let kind = cursor.node().kind();
        // EXPLAIN without ANALYZE does not execute its nested statement. Leave all
        // EXPLAIN forms conservative rather than guess at dialect options.
        if kind == "keyword_explain" {
            return false;
        }
        write |= matches!(kind, "insert" | "update" | "delete" | "keyword_truncate")
            || kind.starts_with("create_")
            || kind.starts_with("alter_")
            || kind.starts_with("drop_");
        if cursor.goto_first_child() {
            continue;
        }
        loop {
            if cursor.goto_next_sibling() {
                break;
            }
            if !cursor.goto_parent() {
                return write;
            }
        }
    }
}
