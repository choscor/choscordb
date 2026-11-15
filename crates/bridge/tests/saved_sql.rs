use choscordb_bridge::*;

#[test]
fn keyword_files_become_snippets_in_completion() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().to_str().unwrap();
    std::fs::write(
        directory.path().join("active.sql"),
        "-- keyword: active_users\nSELECT * FROM users WHERE active;\n",
    )
    .unwrap();
    let listing = saved_sql_list_directory(root, "name");
    assert_eq!(listing.error, "");
    assert_eq!(listing.entries[0].keyword, "active_users");
    let catalog = completion_catalog(listing.snippets, false);
    let reply = complete_sql(&catalog, "act", 3, true);
    assert_eq!(reply.items.len(), 1);
    assert_eq!(reply.items[0].label, "active_users");
    assert_eq!(
        reply.items[0].insert_text,
        "SELECT * FROM users WHERE active;\n"
    );
    assert_eq!(reply.items[0].kind, "snippet");
    assert_eq!(
        saved_sql_list_directory(root, "size").error,
        "Unknown saved SQL sort: size"
    );
    let codes: Vec<_> = saved_sql_sorts()
        .into_iter()
        .map(|sort| sort.code)
        .collect();
    assert_eq!(codes, ["name", "modified"]);
}

#[test]
fn folder_and_keyword_changes_report_errors_as_text() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().to_str().unwrap();
    assert_eq!(saved_sql_create_folder(root, "", "reports"), "");
    assert_eq!(
        saved_sql_create_folder(root, "", "reports"),
        "A folder or file with this name exists."
    );
    let file = directory.path().join("reports/q.sql");
    std::fs::write(&file, "SELECT 1;").unwrap();
    assert_eq!(saved_sql_set_keyword(root, file.to_str().unwrap(), "q"), "");
    assert_eq!(
        std::fs::read_to_string(&file).unwrap(),
        "-- keyword: q\nSELECT 1;"
    );
    assert_eq!(
        saved_sql_set_keyword(root, file.to_str().unwrap(), "no spaces"),
        "A keyword has 1 to 64 letters, digits or underscores."
    );
}
