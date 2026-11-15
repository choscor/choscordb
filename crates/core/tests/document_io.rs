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

#[test]
fn cell_files_round_trip_up_to_the_grid_binary_limit() {
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("cell.bin");
    choscordb_core::write_cell_file(&path, &[0, 1, 2, 255]).unwrap();
    assert_eq!(
        choscordb_core::read_cell_file(&path).unwrap(),
        [0, 1, 2, 255]
    );
    let limit = choscordb_driver_api::MAX_GRID_BINARY_BYTES;
    fs::write(&path, vec![7; limit]).unwrap();
    assert_eq!(choscordb_core::read_cell_file(&path).unwrap().len(), limit);
    fs::write(&path, vec![7; limit + 1]).unwrap();
    assert_eq!(
        choscordb_core::read_cell_file(&path)
            .unwrap_err()
            .to_string(),
        "Cell files are limited to 64 KiB."
    );
    assert!(choscordb_core::read_cell_file(directory.path()).is_err());
}
