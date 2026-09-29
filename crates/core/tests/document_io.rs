use choscordb_core::{read_sql_document, write_sql_document};
use std::fs;

const EXPECTED_LIMIT: usize = 16 * 1024 * 1024;

#[test]
fn reads_regular_sql_file_and_rejects_oversized_file() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("query.sql");
    fs::write(&path, b"SELECT 42;\n").unwrap();
    assert_eq!(read_sql_document(&path).unwrap(), b"SELECT 42;\n");

    assert!(read_sql_document(directory.path()).is_err());
    fs::write(&path, vec![b'x'; EXPECTED_LIMIT]).unwrap();
    assert_eq!(read_sql_document(&path).unwrap().len(), EXPECTED_LIMIT);
    fs::write(&path, vec![b'x'; EXPECTED_LIMIT + 1]).unwrap();
    let error = read_sql_document(&path).unwrap_err();
    assert_eq!(error.to_string(), "SQL files are limited to 16 MiB.");
}

#[test]
fn writes_atomically_and_leaves_existing_document_on_failure() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("query.sql");
    fs::write(&path, b"SELECT old;").unwrap();
    write_sql_document(&path, b"SELECT new;").unwrap();
    assert_eq!(fs::read(&path).unwrap(), b"SELECT new;");

    let boundary_path = directory.path().join("boundary.sql");
    write_sql_document(&boundary_path, &vec![b'x'; EXPECTED_LIMIT]).unwrap();
    assert_eq!(
        fs::metadata(&boundary_path).unwrap().len(),
        EXPECTED_LIMIT as u64
    );

    let error = write_sql_document(&path, &vec![b'x'; EXPECTED_LIMIT + 1]).unwrap_err();
    assert_eq!(error.to_string(), "SQL files are limited to 16 MiB.");
    assert_eq!(fs::read(&path).unwrap(), b"SELECT new;");

    assert!(write_sql_document(&directory.path().join("missing/query.sql"), b"SELECT 1;").is_err());
    assert!(!directory.path().join("missing").exists());
}

#[cfg(unix)]
#[test]
fn replacing_document_preserves_existing_permissions() {
    use std::os::unix::fs::PermissionsExt;

    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("shared.sql");
    fs::write(&path, b"SELECT old;").unwrap();
    fs::set_permissions(&path, fs::Permissions::from_mode(0o640)).unwrap();
    write_sql_document(&path, b"SELECT new;").unwrap();
    assert_eq!(
        fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o640
    );
}

#[test]
fn destination_probe_distinguishes_missing_existing_and_invalid_paths() {
    use choscordb_core::document_path_exists;
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("export.csv");
    assert!(!document_path_exists(&path).unwrap());
    fs::write(&path, b"existing").unwrap();
    assert!(document_path_exists(&path).unwrap());
    assert!(document_path_exists(directory.path()).unwrap());
    assert!(document_path_exists(&path.join("child")).is_err());
}
