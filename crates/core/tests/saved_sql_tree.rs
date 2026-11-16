use choscordb_core::{
    SavedSqlSort, create_saved_sql_folder, saved_sql_tree, set_saved_sql_keyword,
};
use std::{
    fs,
    path::Path,
    time::{Duration, SystemTime},
};

fn touch(path: &Path, text: &str, age_seconds: u64) {
    fs::write(path, text).unwrap();
    let time = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000 - age_seconds);
    fs::File::options()
        .write(true)
        .open(path)
        .unwrap()
        .set_modified(time)
        .unwrap();
}

fn rows(root: &Path, sort: SavedSqlSort) -> Vec<String> {
    saved_sql_tree(root, sort)
        .unwrap()
        .nodes
        .iter()
        .map(|node| {
            format!(
                "{}{}",
                node.relative_path.to_string_lossy(),
                if node.folder { "/" } else { "" }
            )
        })
        .collect()
}

#[test]
fn folders_come_first_and_files_follow_the_sort() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    fs::create_dir_all(root.join("reports/empty")).unwrap();
    touch(&root.join("b.sql"), "SELECT 1;", 30);
    touch(&root.join("a.sql"), "SELECT 1;", 10);
    touch(&root.join("reports/old.sql"), "SELECT 1;", 50);
    touch(&root.join("reports/new.sql"), "SELECT 1;", 5);
    assert_eq!(
        rows(root, SavedSqlSort::Name),
        [
            "reports/",
            "reports/empty/",
            "reports/new.sql",
            "reports/old.sql",
            "a.sql",
            "b.sql"
        ]
    );
    assert_eq!(
        rows(root, SavedSqlSort::Modified),
        [
            "reports/",
            "reports/empty/",
            "reports/new.sql",
            "reports/old.sql",
            "a.sql",
            "b.sql"
        ]
    );
    touch(&root.join("a.sql"), "SELECT 1;", 60);
    assert_eq!(rows(root, SavedSqlSort::Modified)[4..], ["b.sql", "a.sql"]);
    assert_eq!(
        SavedSqlSort::from_code("modified"),
        Some(SavedSqlSort::Modified)
    );
    assert_eq!(SavedSqlSort::Modified.label(), "Sort by date modified");
}

#[test]
fn a_keyword_line_makes_a_snippet() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    fs::write(
        root.join("active.sql"),
        "-- keyword: active_users\nSELECT * FROM users\nWHERE active;\n",
    )
    .unwrap();
    fs::write(root.join("plain.sql"), "SELECT 1;").unwrap();
    fs::write(root.join("bad.sql"), "-- keyword: two words\nSELECT 2;").unwrap();
    let tree = saved_sql_tree(root, SavedSqlSort::Name).unwrap();
    let keywords: Vec<_> = tree
        .nodes
        .iter()
        .map(|node| node.keyword.clone().unwrap_or_default())
        .collect();
    assert_eq!(keywords, ["active_users", "", ""]);
    assert_eq!(tree.snippets.len(), 1);
    assert_eq!(tree.snippets[0].keyword, "active_users");
    assert_eq!(tree.snippets[0].sql, "SELECT * FROM users\nWHERE active;\n");
    assert_eq!(
        tree.errors,
        ["bad.sql: A keyword has 1 to 64 letters, digits or underscores."]
    );
}

#[test]
fn set_keyword_adds_replaces_and_removes_the_first_line() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let file = root.join("q.sql");
    fs::write(&file, "SELECT 1;\n").unwrap();
    set_saved_sql_keyword(root, &file, "one").unwrap();
    assert_eq!(
        fs::read_to_string(&file).unwrap(),
        "-- keyword: one\nSELECT 1;\n"
    );
    set_saved_sql_keyword(root, &file, "two").unwrap();
    assert_eq!(
        fs::read_to_string(&file).unwrap(),
        "-- keyword: two\nSELECT 1;\n"
    );
    set_saved_sql_keyword(root, &file, "").unwrap();
    assert_eq!(fs::read_to_string(&file).unwrap(), "SELECT 1;\n");
    assert_eq!(
        set_saved_sql_keyword(root, &file, "a-b").unwrap_err(),
        "A keyword has 1 to 64 letters, digits or underscores."
    );
    assert!(set_saved_sql_keyword(root, &directory.path().join("../x.sql"), "a").is_err());
}

#[test]
fn folders_are_created_inside_the_root_only() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    create_saved_sql_folder(root, Path::new(""), "reports").unwrap();
    create_saved_sql_folder(root, Path::new("reports"), "daily").unwrap();
    assert!(root.join("reports/daily").is_dir());
    assert_eq!(
        create_saved_sql_folder(root, Path::new(""), "reports").unwrap_err(),
        "A folder or file with this name exists."
    );
    for name in ["", "..", "a/b", ".hidden"] {
        assert_eq!(
            create_saved_sql_folder(root, Path::new(""), name).unwrap_err(),
            "Enter a folder name without slashes. It cannot start with a dot."
        );
    }
    assert_eq!(
        create_saved_sql_folder(root, Path::new("../outside"), "x").unwrap_err(),
        "The parent folder is not in the saved SQL folder."
    );
}

#[cfg(unix)]
#[test]
fn a_file_that_cannot_be_opened_stays_in_the_tree_and_reports_its_error() {
    use std::os::unix::fs::PermissionsExt;
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path();
    let locked = root.join("locked.sql");
    fs::write(&locked, "SELECT 1;").unwrap();
    fs::set_permissions(&locked, fs::Permissions::from_mode(0o000)).unwrap();
    let tree = saved_sql_tree(root, SavedSqlSort::Modified).unwrap();
    fs::set_permissions(&locked, fs::Permissions::from_mode(0o600)).unwrap();
    let names: Vec<_> = tree
        .nodes
        .iter()
        .map(|node| node.relative_path.to_string_lossy().into_owned())
        .collect();
    assert_eq!(names, ["locked.sql"]);
    assert_eq!(tree.errors.len(), 1);
    assert!(
        tree.errors[0].starts_with("locked.sql: "),
        "{:?}",
        tree.errors
    );
}
