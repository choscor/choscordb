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

#[test]
fn navigator_templates_wait_for_the_columns_they_list() {
    use choscordb_sql_language::{TemplateKind, template_unavailable_reason};
    for kind in [TemplateKind::Select, TemplateKind::Delete] {
        assert_eq!(template_unavailable_reason(kind, false, false), None);
    }
    let unloaded = template_unavailable_reason(TemplateKind::Insert, false, false);
    assert!(unloaded.is_some_and(|reason| reason.contains("Load the object's columns")));
    assert_eq!(
        template_unavailable_reason(TemplateKind::Insert, true, false),
        None
    );
    assert!(template_unavailable_reason(TemplateKind::Update, false, true).is_some());
    let columnless = template_unavailable_reason(TemplateKind::Update, true, false);
    assert!(columnless.is_some_and(|reason| reason.contains("column")));
    assert_eq!(
        template_unavailable_reason(TemplateKind::Update, true, true),
        None
    );
}

#[test]
fn navigator_templates_list_columns_only_where_they_assign_values() {
    use choscordb_sql_language::navigator_template;
    let columns = ["id", "name"];
    let select = navigator_template(TemplateKind::Select, "\"t\"", &columns, true).unwrap();
    assert_eq!(select, "SELECT * FROM \"t\";");
    let insert = navigator_template(TemplateKind::Insert, "\"t\"", &columns, true).unwrap();
    assert!(insert.contains("(\"id\", \"name\") VALUES"), "{insert}");
    let unloaded = navigator_template(TemplateKind::Update, "\"t\"", &[], false).unwrap_err();
    assert!(unloaded.contains("Load the object's columns"), "{unloaded}");
}
