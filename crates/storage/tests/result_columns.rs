use choscordb_storage::{
    ColumnLayoutError, MAX_HIDDEN_COLUMN_CONTEXTS, Storage, StorageError, result_columns_context,
    toggle_hidden_column,
};

fn names(values: &[&str]) -> Vec<String> {
    values.iter().map(|value| value.to_string()).collect()
}

#[test]
fn hidden_columns_round_trip_per_context() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("columns.sqlite");
    let mut storage = Storage::open(&path).unwrap();
    let orders = result_columns_context("profile", "[\"main\",\"orders\"]");
    let users = result_columns_context("profile", "[\"main\",\"users\"]");
    assert_ne!(orders, users);
    assert!(storage.hidden_result_columns(&orders).unwrap().is_empty());
    storage
        .set_hidden_result_columns(&orders, &names(&["note", "created"]))
        .unwrap();
    storage
        .set_hidden_result_columns(&users, &names(&["email"]))
        .unwrap();
    drop(storage);
    let mut storage = Storage::open(&path).unwrap();
    assert_eq!(
        storage.hidden_result_columns(&orders).unwrap(),
        names(&["note", "created"])
    );
    storage.set_hidden_result_columns(&orders, &[]).unwrap();
    assert!(storage.hidden_result_columns(&orders).unwrap().is_empty());
    assert_eq!(
        storage.hidden_result_columns(&users).unwrap(),
        names(&["email"])
    );
    assert!(matches!(
        storage.set_setting("result_column_layouts", &1),
        Err(StorageError::ReservedSetting)
    ));
}

#[test]
fn invalid_input_is_rejected_and_old_contexts_are_dropped() {
    let mut storage = Storage::in_memory().unwrap();
    assert!(
        storage
            .set_hidden_result_columns("", &names(&["a"]))
            .is_err()
    );
    assert!(
        storage
            .set_hidden_result_columns("c", &names(&["a", "a"]))
            .is_err()
    );
    assert!(
        storage
            .set_hidden_result_columns("c", &names(&[""]))
            .is_err()
    );
    for index in 0..=MAX_HIDDEN_COLUMN_CONTEXTS {
        storage
            .set_hidden_result_columns(&format!("context {index}"), &names(&["a"]))
            .unwrap();
    }
    assert!(
        storage
            .hidden_result_columns("context 0")
            .unwrap()
            .is_empty()
    );
    assert_eq!(
        storage
            .hidden_result_columns(&format!("context {MAX_HIDDEN_COLUMN_CONTEXTS}"))
            .unwrap(),
        names(&["a"])
    );
}

#[test]
fn toggling_keeps_one_visible_column() {
    let columns = names(&["id", "name", "note"]);
    assert_eq!(
        toggle_hidden_column(&[], "note", true, &columns).unwrap(),
        names(&["note"])
    );
    assert_eq!(
        toggle_hidden_column(&names(&["note"]), "note", false, &columns).unwrap(),
        Vec::<String>::new()
    );
    assert_eq!(
        toggle_hidden_column(&names(&["id", "note"]), "name", true, &columns),
        Err(ColumnLayoutError::LastVisibleColumn)
    );
    assert_eq!(
        toggle_hidden_column(&[], "missing", true, &columns),
        Err(ColumnLayoutError::UnknownColumn)
    );
    assert_eq!(
        toggle_hidden_column(&names(&["gone"]), "note", true, &columns).unwrap(),
        names(&["gone", "note"])
    );
}
