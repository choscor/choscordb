use choscordb_driver_api::{
    DatabaseTargetField, ErrorKind, MAX_DATABASE_CHOICES, database_target_field,
    named_database_list, sqlite_sibling_databases,
};

#[test]
fn sqlite_lists_sibling_database_files_in_name_order() {
    let folder = tempfile::tempdir().unwrap();
    for name in ["b.sqlite3", "a.db", "c.sqlite", "notes.txt", "d.DB"] {
        std::fs::write(folder.path().join(name), b"").unwrap();
    }
    std::fs::create_dir(folder.path().join("dir.db")).unwrap();
    let current = folder.path().join("c.sqlite");
    let list = sqlite_sibling_databases(&current).unwrap();
    let labels: Vec<_> = list
        .choices
        .iter()
        .map(|choice| choice.label.as_str())
        .collect();
    assert_eq!(labels, ["a.db", "b.sqlite3", "c.sqlite"]);
    assert_eq!(list.current, current.to_str().unwrap());
    assert_eq!(
        list.choices[0].target,
        folder.path().join("a.db").to_str().unwrap()
    );
}

#[test]
fn sqlite_keeps_the_current_file_with_another_extension() {
    let folder = tempfile::tempdir().unwrap();
    std::fs::write(folder.path().join("main.data"), b"").unwrap();
    std::fs::write(folder.path().join("other.db"), b"").unwrap();
    let current = folder.path().join("main.data");
    let list = sqlite_sibling_databases(&current).unwrap();
    let labels: Vec<_> = list
        .choices
        .iter()
        .map(|choice| choice.label.as_str())
        .collect();
    assert_eq!(labels, ["main.data", "other.db"]);
}

#[test]
fn sqlite_memory_and_missing_folders_fail_loudly() {
    let memory = sqlite_sibling_databases(std::path::Path::new(":memory:")).unwrap_err();
    assert_eq!(memory.kind, ErrorKind::Unsupported);
    let missing = sqlite_sibling_databases(std::path::Path::new("/definitely/missing/folder/x.db"))
        .unwrap_err();
    assert_eq!(missing.kind, ErrorKind::Io);
}

#[test]
fn named_lists_keep_server_order_and_cap_the_count() {
    let list = named_database_list(vec!["app".into(), "logs".into()], "logs".into()).unwrap();
    assert_eq!(list.choices[1].label, "logs");
    assert_eq!(list.choices[1].target, "logs");
    assert_eq!(list.current, "logs");
    let names = (0..=MAX_DATABASE_CHOICES).map(|i| i.to_string()).collect();
    assert_eq!(
        named_database_list(names, String::new()).unwrap_err().kind,
        ErrorKind::ResourceLimit
    );
}

#[test]
fn each_driver_names_the_profile_field_to_switch() {
    assert_eq!(
        database_target_field("sqlite"),
        Some(DatabaseTargetField::Path)
    );
    for driver in ["postgres", "mysql", "mariadb"] {
        assert_eq!(
            database_target_field(driver),
            Some(DatabaseTargetField::Database)
        );
    }
    assert_eq!(database_target_field("oracle"), None);
}
