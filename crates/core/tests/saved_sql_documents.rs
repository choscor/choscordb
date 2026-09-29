#[cfg(unix)]
use choscordb_core::read_sql_document;
use choscordb_core::{
    ensure_saved_sql_directory, list_saved_sql_documents, read_saved_sql_document,
    saved_sql_document_identity,
};
use std::{fs, path::PathBuf};

#[test]
fn lists_nested_sql_files_in_name_order_without_other_files() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    fs::create_dir_all(root.join("reports/weekly")).unwrap();
    fs::write(root.join("z.sql"), b"SELECT 'z';").unwrap();
    fs::write(root.join("reports/weekly/a.sql"), b"SELECT 'a';").unwrap();
    fs::write(root.join("reports/b.sql"), b"SELECT 'b';").unwrap();
    fs::write(root.join("reports/notes.txt"), b"not sql").unwrap();

    let listing = list_saved_sql_documents(&root).unwrap();
    let paths: Vec<PathBuf> = listing
        .documents
        .iter()
        .map(|document| document.relative_path.clone())
        .collect();
    assert_eq!(
        paths,
        [
            PathBuf::from("reports/b.sql"),
            PathBuf::from("reports/weekly/a.sql"),
            PathBuf::from("z.sql"),
        ]
    );
    assert_eq!(listing.documents[0].path, root.join("reports/b.sql"));
    assert_eq!(listing.documents[0].size_bytes, 11);
    assert!(!listing.has_more);
}

#[test]
fn missing_directory_is_empty_and_listing_is_bounded() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    let missing = list_saved_sql_documents(&root).unwrap();
    assert!(missing.documents.is_empty());
    assert!(!missing.has_more);

    fs::create_dir(&root).unwrap();
    for index in 0..1001 {
        fs::write(root.join(format!("{index:04}.sql")), b"SELECT 1;").unwrap();
    }
    let listing = list_saved_sql_documents(&root).unwrap();
    assert_eq!(listing.documents.len(), 1000);
    assert_eq!(
        listing.documents.first().unwrap().relative_path,
        PathBuf::from("0000.sql")
    );
    assert_eq!(
        listing.documents.last().unwrap().relative_path,
        PathBuf::from("0999.sql")
    );
    assert!(listing.has_more);
}

#[test]
fn creates_saved_sql_directory_without_replacing_an_existing_file() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("nested/sql");
    ensure_saved_sql_directory(&root).unwrap();
    assert!(root.is_dir());
    ensure_saved_sql_directory(&root).unwrap();

    let file = directory.path().join("existing");
    fs::write(&file, b"unchanged").unwrap();
    assert!(ensure_saved_sql_directory(&file).is_err());
    assert_eq!(fs::read(file).unwrap(), b"unchanged");
}

#[test]
fn document_identity_falls_back_to_absolute_path_for_new_save_destinations() {
    let directory = tempfile::tempdir().unwrap();
    let destination = directory.path().join("new.sql");
    assert!(!destination.exists());
    assert_eq!(
        saved_sql_document_identity(&destination).unwrap(),
        destination
    );
    assert_eq!(
        saved_sql_document_identity(&directory.path().join("missing/../new.sql")).unwrap(),
        destination
    );
    fs::write(&destination, b"SELECT 1;").unwrap();
    assert_eq!(
        saved_sql_document_identity(&destination).unwrap(),
        fs::canonicalize(&destination).unwrap()
    );
}

#[cfg(unix)]
#[test]
fn document_identity_resolves_existing_symlink_aliases() {
    use std::os::unix::fs::symlink;

    let directory = tempfile::tempdir().unwrap();
    let target = directory.path().join("query.sql");
    let alias = directory.path().join("alias.sql");
    fs::write(&target, b"SELECT 1;").unwrap();
    symlink(&target, &alias).unwrap();
    assert_eq!(
        saved_sql_document_identity(&alias).unwrap(),
        saved_sql_document_identity(&target).unwrap()
    );
}

#[test]
fn saved_reader_rejects_paths_outside_root_and_reads_listed_file() {
    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    fs::create_dir(&root).unwrap();
    let inside = root.join("query.sql");
    fs::write(&inside, b"SELECT 42;").unwrap();
    let outside = directory.path().join("outside.sql");
    fs::write(&outside, b"SELECT secret;").unwrap();

    assert_eq!(
        read_saved_sql_document(&root, &inside).unwrap(),
        b"SELECT 42;"
    );
    assert!(read_saved_sql_document(&root, &outside).is_err());
    assert!(read_saved_sql_document(&root, &root.join("../outside.sql")).is_err());
    assert!(read_saved_sql_document(&root, &root.join("query.txt")).is_err());

    fs::write(&inside, vec![b'x'; 16 * 1024 * 1024 + 1]).unwrap();
    assert_eq!(
        read_saved_sql_document(&root, &inside)
            .unwrap_err()
            .to_string(),
        "SQL files are limited to 16 MiB."
    );
}

#[cfg(unix)]
#[test]
fn symlinked_files_and_folders_are_never_listed_or_opened() {
    use std::os::unix::fs::symlink;

    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    let real = directory.path().join("other");
    fs::create_dir(&root).unwrap();
    fs::create_dir(&real).unwrap();
    fs::write(real.join("outside.sql"), b"SELECT secret;").unwrap();
    fs::write(root.join("good.sql"), b"SELECT 1;").unwrap();
    symlink(real.join("outside.sql"), root.join("linked.sql")).unwrap();
    symlink(&real, root.join("linked_folder")).unwrap();

    let listing = list_saved_sql_documents(&root).unwrap();
    assert_eq!(listing.documents.len(), 1);
    assert_eq!(
        listing.documents[0].relative_path,
        PathBuf::from("good.sql")
    );
    assert!(read_saved_sql_document(&root, &root.join("linked.sql")).is_err());
    assert!(read_saved_sql_document(&root, &root.join("linked_folder/outside.sql")).is_err());
}

#[cfg(unix)]
#[test]
fn ordinary_sql_reader_allows_an_explicitly_selected_symlink() {
    use std::os::unix::fs::symlink;

    let directory = tempfile::tempdir().unwrap();
    let target = directory.path().join("target.sql");
    let alias = directory.path().join("alias.sql");
    fs::write(&target, b"private contents").unwrap();
    symlink(&target, &alias).unwrap();
    assert_eq!(read_sql_document(&alias).unwrap(), b"private contents");
}

#[cfg(unix)]
#[test]
fn saved_reader_never_reads_a_file_swapped_to_a_symlink() {
    use std::{
        os::unix::fs::symlink,
        sync::{
            Arc,
            atomic::{AtomicBool, Ordering},
        },
        thread,
    };

    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    fs::create_dir(&root).unwrap();
    let target = root.join("query.sql");
    let parked = root.join("parked.sql");
    let outside = directory.path().join("outside.sql");
    fs::write(&target, b"safe").unwrap();
    fs::write(&outside, b"outside secret").unwrap();
    let stop = Arc::new(AtomicBool::new(false));
    let worker_stop = Arc::clone(&stop);
    let worker_target = target.clone();
    let worker_parked = parked.clone();
    let worker_outside = outside.clone();
    let worker = thread::spawn(move || {
        while !worker_stop.load(Ordering::Relaxed) {
            fs::rename(&worker_target, &worker_parked).unwrap();
            symlink(&worker_outside, &worker_target).unwrap();
            fs::remove_file(&worker_target).unwrap();
            fs::rename(&worker_parked, &worker_target).unwrap();
        }
    });
    for _ in 0..2000 {
        if let Ok(bytes) = read_saved_sql_document(&root, &target) {
            assert_eq!(bytes, b"safe");
        }
    }
    stop.store(true, Ordering::Relaxed);
    worker.join().unwrap();
}

#[cfg(unix)]
#[test]
fn listing_never_enumerates_a_folder_swapped_to_an_outside_symlink() {
    use std::{
        os::unix::fs::symlink,
        sync::{
            Arc,
            atomic::{AtomicBool, Ordering},
        },
        thread,
    };

    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    let nested = root.join("nested");
    let parked = root.join("parked");
    let outside = directory.path().join("outside");
    fs::create_dir(&root).unwrap();
    fs::create_dir(&nested).unwrap();
    fs::create_dir(&outside).unwrap();
    fs::write(nested.join("good.sql"), b"safe").unwrap();
    fs::write(outside.join("private.sql"), b"outside secret").unwrap();
    let stop = Arc::new(AtomicBool::new(false));
    let worker_stop = Arc::clone(&stop);
    let worker_outside = outside.clone();
    let worker = thread::spawn(move || {
        while !worker_stop.load(Ordering::Relaxed) {
            fs::rename(&nested, &parked).unwrap();
            symlink(&worker_outside, &nested).unwrap();
            fs::remove_file(&nested).unwrap();
            fs::rename(&parked, &nested).unwrap();
        }
    });
    for _ in 0..2000 {
        let listing = list_saved_sql_documents(&root).unwrap();
        assert!(
            listing.documents.iter().all(
                |document| document.relative_path != std::path::Path::new("nested/private.sql")
            )
        );
    }
    stop.store(true, Ordering::Relaxed);
    worker.join().unwrap();
}

#[cfg(windows)]
#[test]
fn windows_saved_reader_rejects_file_and_directory_reparse_links() {
    use std::os::windows::fs::{symlink_dir, symlink_file};

    let directory = tempfile::tempdir().unwrap();
    let root = directory.path().join("sql");
    let outside = directory.path().join("outside");
    fs::create_dir(&root).unwrap();
    fs::create_dir(&outside).unwrap();
    fs::write(root.join("good.sql"), b"safe").unwrap();
    fs::write(outside.join("private.sql"), b"outside secret").unwrap();
    let file_link = root.join("linked.sql");
    let dir_link = root.join("linked_folder");
    // Windows hosts without Developer Mode may prohibit test symlink creation.
    if symlink_file(outside.join("private.sql"), &file_link).is_err()
        || symlink_dir(&outside, &dir_link).is_err()
    {
        return;
    }
    assert_eq!(
        read_saved_sql_document(&root, &root.join("good.sql")).unwrap(),
        b"safe"
    );
    assert!(read_saved_sql_document(&root, &file_link).is_err());
    assert!(read_saved_sql_document(&root, &dir_link.join("private.sql")).is_err());
}
