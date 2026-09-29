use choscordb_sql_language::{MAX_TEMPLATE_BYTES, TemplateKind, template_from_qualified};

#[test]
fn editable_insert_and_update_include_placeholder_guidance() {
    let insert = template_from_qualified(TemplateKind::Insert, "\"s\".\"t\"", &["id"])
        .expect("valid insert template");
    assert_eq!(
        insert,
        "-- Replace numbered placeholders with values before running.\nINSERT INTO \"s\".\"t\" (\"id\") VALUES ($1);"
    );
    let update = template_from_qualified(TemplateKind::Update, "`t`", &["value"])
        .expect("valid update template");
    assert_eq!(
        update,
        "-- Replace numbered placeholders with values before running.\nUPDATE `t` SET `value` = ? WHERE /* predicate */;"
    );
    assert_eq!(
        template_from_qualified(TemplateKind::Insert, "\"t\"", &[]).unwrap(),
        "INSERT INTO \"t\" DEFAULT VALUES;"
    );
}

#[test]
fn guidance_counts_toward_the_output_limit() {
    let name = format!("\"{}\"", "a".repeat(MAX_TEMPLATE_BYTES - 90));
    assert!(template_from_qualified(TemplateKind::Select, &name, &[]).is_ok());
    let error = template_from_qualified(TemplateKind::Insert, &name, &["c"])
        .expect_err("the guidance and SQL together exceed the output limit");
    assert_eq!(
        error.to_string(),
        "Generated SQL exceeds the template size limit."
    );
}
