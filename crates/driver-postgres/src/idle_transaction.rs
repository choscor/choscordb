pub(super) fn is_write(sql: &str) -> bool {
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
