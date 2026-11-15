use choscordb_sql_language::{MysqlSqlMode, ScriptChunk, split_script};

fn texts<'a>(sql: &'a str, chunk: &ScriptChunk) -> Vec<&'a str> {
    chunk
        .statements
        .iter()
        .map(|range| &sql[range.clone()])
        .collect()
}

#[test]
fn complete_statements_split_and_the_unfinished_tail_stays() {
    let sql = "CREATE TABLE t (a int);\n-- note\nINSERT INTO t VALUES (';');\nINSERT INTO t VAL";
    let chunk = split_script(sql, None, ";", false).unwrap();
    assert_eq!(
        texts(sql, &chunk),
        ["CREATE TABLE t (a int)", "INSERT INTO t VALUES (';')"]
    );
    assert_eq!(&sql[chunk.consumed..], "\nINSERT INTO t VAL");
    assert_eq!(chunk.delimiter, ";");
    let last = split_script(sql, None, ";", true).unwrap();
    assert_eq!(texts(sql, &last).last(), Some(&"INSERT INTO t VAL"));
    assert_eq!(last.consumed, sql.len());
}

#[test]
fn postgres_dollar_bodies_keep_their_semicolons() {
    let sql = "CREATE FUNCTION f() RETURNS int AS $$ SELECT 1; $$ LANGUAGE sql;\nSELECT f();";
    let chunk = split_script(sql, None, ";", true).unwrap();
    assert_eq!(
        texts(sql, &chunk),
        [
            "CREATE FUNCTION f() RETURNS int AS $$ SELECT 1; $$ LANGUAGE sql",
            "SELECT f()"
        ]
    );
}

#[test]
fn mysql_delimiter_lines_change_the_statement_end() {
    let sql = "SET a = 1;\nDELIMITER ;;\nCREATE TRIGGER x BEFORE INSERT ON t FOR EACH ROW BEGIN SET NEW.a = 1; END ;;\ndelimiter ;\nSELECT 2;";
    let mode = Some(MysqlSqlMode::default());
    let chunk = split_script(sql, mode, ";", true).unwrap();
    assert_eq!(
        texts(sql, &chunk),
        [
            "SET a = 1",
            "CREATE TRIGGER x BEFORE INSERT ON t FOR EACH ROW BEGIN SET NEW.a = 1; END",
            "SELECT 2"
        ]
    );
    assert_eq!(chunk.delimiter, ";");
    let open = split_script(sql, mode, ";", false).unwrap();
    assert_eq!(texts(sql, &open).len(), 3);
    let partial = &sql[..sql.rfind(";;").unwrap() + 1];
    let held = split_script(partial, mode, ";", false).unwrap();
    assert_eq!(texts(partial, &held), ["SET a = 1"]);
    assert_eq!(held.delimiter, ";;");
    assert!(partial[held.consumed..].starts_with("CREATE TRIGGER"));
}

#[test]
fn a_delimiter_line_without_a_delimiter_is_an_error() {
    let error = split_script(
        "SELECT 1;\nDELIMITER\nSELECT 2;",
        Some(MysqlSqlMode::default()),
        ";",
        true,
    )
    .unwrap_err();
    assert_eq!(error.offset, 10);
    assert_eq!(error.message, "DELIMITER needs a delimiter text.");
}
