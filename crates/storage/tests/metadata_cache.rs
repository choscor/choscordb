use choscordb_driver_api::{ObjectId, ObjectKind};
use choscordb_storage::{
    CachedChild, ConnectionProfile, METADATA_CACHE_SECONDS, ProfileConfiguration, Storage,
};

fn child(name: &str) -> CachedChild {
    CachedChild {
        id: ObjectId(format!("pg:schema:{name}")),
        name: name.into(),
        qualified_name: format!("\"{name}\""),
        kind: ObjectKind::Schema,
        has_children: true,
    }
}

#[test]
fn saved_children_read_back_until_they_expire() {
    let mut store = Storage::in_memory().unwrap();
    let parent = ObjectId("pg:database:5".into());
    let children = vec![child("public"), child("sales")];
    store
        .cache_children("p1", Some(&parent), &children, 1_000)
        .unwrap();
    store
        .cache_children("p1", None, &[child("root")], 1_000)
        .unwrap();
    assert_eq!(
        store.cached_children("p1", Some(&parent), 1_000).unwrap(),
        Some(children.clone())
    );
    assert_eq!(
        store.cached_children("p1", None, 1_000).unwrap(),
        Some(vec![child("root")])
    );
    // Another profile and another parent have no saved children.
    assert_eq!(
        store.cached_children("p2", Some(&parent), 1_000).unwrap(),
        None
    );
    let last = 1_000 + METADATA_CACHE_SECONDS;
    assert_eq!(
        store.cached_children("p1", Some(&parent), last).unwrap(),
        Some(children)
    );
    assert_eq!(
        store
            .cached_children("p1", Some(&parent), last + 1)
            .unwrap(),
        None
    );
}

#[test]
fn a_new_child_list_replaces_the_saved_one() {
    let mut store = Storage::in_memory().unwrap();
    store
        .cache_children("p1", None, &[child("old")], 1_000)
        .unwrap();
    store
        .cache_children("p1", None, &[child("new")], 2_000)
        .unwrap();
    assert_eq!(
        store.cached_children("p1", None, 2_000).unwrap(),
        Some(vec![child("new")])
    );
}

#[test]
fn deleting_a_profile_clears_its_saved_children() {
    let mut store = Storage::in_memory().unwrap();
    store
        .cache_children("p1", None, &[child("a")], 1_000)
        .unwrap();
    store
        .cache_children("p2", None, &[child("b")], 1_000)
        .unwrap();
    store.delete_profile("p1").unwrap();
    assert_eq!(store.cached_children("p1", None, 1_000).unwrap(), None);
    assert_eq!(
        store.cached_children("p2", None, 1_000).unwrap(),
        Some(vec![child("b")])
    );
}

#[test]
fn a_write_deletes_expired_children() {
    let mut store = Storage::in_memory().unwrap();
    store
        .cache_children("p1", None, &[child("old")], 1_000)
        .unwrap();
    let later = 1_000 + METADATA_CACHE_SECONDS + 1;
    store
        .cache_children("p2", None, &[child("new")], later)
        .unwrap();
    // A read at the first time finds no row, so the later write deleted it.
    assert_eq!(store.cached_children("p1", None, 1_000).unwrap(), None);
    assert_eq!(
        store.cached_children("p2", None, later).unwrap(),
        Some(vec![child("new")])
    );
}

fn sqlite_profile(path: &str, name: &str) -> ConnectionProfile {
    ConnectionProfile {
        id: "p1".into(),
        name: name.into(),
        group_id: None,
        configuration: ProfileConfiguration::Sqlite {
            path: path.into(),
            read_only: false,
        },
        credential_ref: None,
        proxy_credential_ref: None,
        ssh_jump_credential_refs: Default::default(),
        ssh_jump_private_key_refs: Default::default(),
        tls_credential_ref: None,
        ssh_credential_ref: None,
        ssh_private_key_ref: None,
    }
}

#[test]
fn a_new_connection_target_clears_the_saved_children() {
    let mut store = Storage::in_memory().unwrap();
    store.save_profile(&sqlite_profile("/a.db", "A")).unwrap();
    store
        .cache_children("p1", None, &[child("a")], 1_000)
        .unwrap();
    // A new name keeps the children of the same database.
    store
        .save_profile(&sqlite_profile("/a.db", "Renamed"))
        .unwrap();
    assert_eq!(
        store.cached_children("p1", None, 1_000).unwrap(),
        Some(vec![child("a")])
    );
    store
        .save_profile(&sqlite_profile("/b.db", "Renamed"))
        .unwrap();
    assert_eq!(store.cached_children("p1", None, 1_000).unwrap(), None);
}
