use choscordb_pins::{
    PinError, PinRecord, PinStore, identity_key, remove_pin, remove_profile_pins, toggle_pin, valid,
};
use std::fs;

fn pin(profile: &str, id: &str) -> PinRecord {
    PinRecord {
        profile_id: profile.into(),
        profile_name: "Saved SQLite".into(),
        object_id: id.into(),
        name: "orders".into(),
        qualified_name: "\"main\".\"orders\"".into(),
        kind: "table".into(),
        parent_object_id: "[\"main\"]".into(),
        relation_subtype: "foreign_table".into(),
        ancestry_ids: vec!["[\"main\"]".into()],
        ancestry_names: vec!["main".into()],
        unavailable: true,
    }
}

#[test]
fn ordered_round_trip_preserves_context_and_exact_identity_format() {
    let directory = tempfile::tempdir().unwrap();
    let profile_path = directory.path().join("profiles.sqlite");
    let store = PinStore::for_profile_storage(&profile_path);
    assert_eq!(store.load().pins, Vec::<PinRecord>::new());

    let first = pin("profile-b", "[\"main\",\"orders\"]");
    let second = pin("profile-a", "[\"main\",\"orders\"]");
    assert!(valid(&first));
    assert_eq!(
        identity_key(&first),
        "[\"profile-b\",\"table\",\"[\\\"main\\\",\\\"orders\\\"]\",\"\\\"main\\\".\\\"orders\\\"\",\"foreign_table\"]"
    );
    store.save(&[first.clone(), second.clone()]).unwrap();
    assert_eq!(store.load().pins, vec![first, second]);
    assert!(directory.path().join("profiles.sqlite.pins.json").is_file());
    assert!(!profile_path.exists());
}

#[test]
fn load_skips_malformed_and_duplicate_entries_but_keeps_first_valid_pin() {
    let directory = tempfile::tempdir().unwrap();
    let profile_path = directory.path().join("metadata.sqlite");
    let file = directory.path().join("metadata.sqlite.pins.json");
    let bytes = concat!(
        "{\"version\":1,\"pins\":[",
        "{\"profileId\":\"saved\",\"profileName\":\"Saved\",\"objectId\":\"pg:relation:42\",",
        "\"name\":\"orders\",\"qualifiedName\":\"orders\",\"kind\":\"table\",",
        "\"parentObjectId\":\"pg:group:3:table\",\"ancestryIds\":[],",
        "\"ancestryNames\":[],\"relationSubtype\":\"\",\"unavailable\":false},",
        "{\"profileId\":\"saved\",\"profileName\":\"Saved\",\"objectId\":\"pg:relation:42\",",
        "\"name\":\"orders\",\"qualifiedName\":\"orders\",\"kind\":\"table\",",
        "\"parentObjectId\":\"pg:relation:99\",\"ancestryIds\":[],",
        "\"ancestryNames\":[],\"relationSubtype\":\"\",\"unavailable\":false},",
        "{\"profileId\":\"saved\",\"profileName\":\"Saved\",\"objectId\":\"unsafe\",",
        "\"name\":\"group\",\"qualifiedName\":\"group\",\"kind\":\"group\",",
        "\"parentObjectId\":\"x\",\"ancestryIds\":[],\"ancestryNames\":[],",
        "\"relationSubtype\":\"\",\"unavailable\":false}]}"
    );
    fs::write(&file, bytes).unwrap();
    let store = PinStore::for_profile_storage(&profile_path);
    let loaded = store.load();
    assert_eq!(loaded.pins.len(), 1);
    assert_eq!(loaded.pins[0].object_id, "pg:relation:42");
    assert_eq!(loaded.pins[0].parent_object_id, "pg:group:3:table");
    assert_eq!(
        loaded.error,
        "Skipped 2 invalid or duplicate pin record(s)."
    );

    fs::write(&file, b"{\"version\":99,\"pins\":[]}").unwrap();
    assert_eq!(
        store.load().error,
        "The pin file version or structure is unsupported."
    );
    fs::write(&file, b"not JSON").unwrap();
    assert_eq!(store.load().error, "The pin file is malformed.");
    let handle = fs::OpenOptions::new().write(true).open(&file).unwrap();
    handle.set_len(64 * 1024 * 1024 + 1).unwrap();
    assert_eq!(store.load().error, "The pin file is too large to read.");
}

#[test]
fn save_rejects_invalid_or_duplicate_pins_without_replacing_saved_state() {
    let directory = tempfile::tempdir().unwrap();
    let store = PinStore::for_profile_storage(&directory.path().join("profiles.sqlite"));
    let original = pin("saved", "pg:index:42");
    store.save(std::slice::from_ref(&original)).unwrap();
    let saved = fs::read(directory.path().join("profiles.sqlite.pins.json")).unwrap();

    let mut invalid = original.clone();
    invalid.kind = "loading".into();
    assert!(matches!(store.save(&[invalid]), Err(PinError::InvalidPin)));
    let mut duplicate = original.clone();
    duplicate.parent_object_id = "pg:relation:24".into();
    duplicate.ancestry_ids = vec!["pg:database:1".into(), "pg:schema:2".into()];
    duplicate.ancestry_names = vec!["database".into(), "public".into()];
    assert_eq!(identity_key(&original), identity_key(&duplicate));
    assert!(matches!(
        store.save(&[original.clone(), duplicate]),
        Err(PinError::DuplicatePin)
    ));
    assert_eq!(
        fs::read(directory.path().join("profiles.sqlite.pins.json")).unwrap(),
        saved
    );

    let blocked_path = directory.path().join("blocked.sqlite.pins.json");
    fs::create_dir(&blocked_path).unwrap();
    let blocked = PinStore::for_profile_storage(&directory.path().join("blocked.sqlite"));
    assert!(blocked.save(&[original]).is_err());
}

#[test]
fn validates_root_schema_and_rejects_unsafe_text() {
    let mut schema = pin("mysql-profile", "mysql:schema:analytics");
    schema.kind = "schema".into();
    schema.qualified_name = "`analytics`".into();
    schema.parent_object_id.clear();
    schema.ancestry_ids.clear();
    schema.ancestry_names.clear();
    assert!(valid(&schema));
    let directory = tempfile::tempdir().unwrap();
    let store = PinStore::for_application_data(directory.path());
    store.save(std::slice::from_ref(&schema)).unwrap();
    assert_eq!(store.load().pins, vec![schema.clone()]);
    assert!(directory.path().join("pins.json").exists());
    schema.object_id = "bad\0id".into();
    assert!(!valid(&schema));
    schema.object_id = "mysql:schema:analytics".into();
    schema.ancestry_ids.push("child".into());
    assert!(!valid(&schema));
    schema.ancestry_ids.clear();
    schema.name = "🙂".repeat(524_289);
    assert!(!valid(&schema));
}

#[test]
fn save_rejects_file_over_64_mib() {
    let directory = tempfile::tempdir().unwrap();
    let store = PinStore::for_profile_storage(&directory.path().join("profiles.sqlite"));
    let long_name = "x".repeat(1024 * 1024);
    let pins: Vec<_> = (0..65)
        .map(|index| {
            let mut record = pin("saved", &format!("object-{index}"));
            record.profile_name = long_name.clone();
            record
        })
        .collect();
    assert!(matches!(store.save(&pins), Err(PinError::TooLargeToSave)));
    assert!(!directory.path().join("profiles.sqlite.pins.json").exists());
}

#[cfg(unix)]
#[test]
fn symlink_storage_path_is_rejected_without_reading_or_replacing_target() {
    use std::os::unix::fs::symlink;

    let directory = tempfile::tempdir().unwrap();
    let target_store = PinStore::for_profile_storage(&directory.path().join("target.sqlite"));
    let original = pin("saved", "table-1");
    target_store.save(std::slice::from_ref(&original)).unwrap();
    let target_path = directory.path().join("target.sqlite.pins.json");
    let before = fs::read(&target_path).unwrap();
    let link_path = directory.path().join("link.sqlite.pins.json");
    symlink(&target_path, &link_path).unwrap();

    let linked_store = PinStore::for_profile_storage(&directory.path().join("link.sqlite"));
    let loaded = linked_store.load();
    assert!(loaded.pins.is_empty());
    assert!(!loaded.error.is_empty());
    assert!(linked_store.save(&[original]).is_err());
    assert_eq!(fs::read(&target_path).unwrap(), before);
    assert!(
        fs::symlink_metadata(link_path)
            .unwrap()
            .file_type()
            .is_symlink()
    );
}

#[cfg(unix)]
#[test]
fn fifo_storage_path_is_rejected_without_blocking() {
    use std::os::unix::fs::FileTypeExt;
    let directory = tempfile::tempdir().unwrap();
    let path = directory.path().join("pipe.sqlite.pins.json");
    assert!(
        std::process::Command::new("mkfifo")
            .arg(&path)
            .status()
            .unwrap()
            .success()
    );
    let store = PinStore::for_profile_storage(&directory.path().join("pipe.sqlite"));
    let loaded = store.load();
    assert!(loaded.pins.is_empty());
    assert!(!loaded.error.is_empty());
    assert!(matches!(
        store.save(&[pin("saved", "table-1")]),
        Err(PinError::UnsafePath)
    ));
    assert!(fs::symlink_metadata(path).unwrap().file_type().is_fifo());
}

#[cfg(unix)]
#[test]
fn atomic_save_preserves_existing_permissions_and_creates_private_files() {
    use std::os::unix::fs::PermissionsExt;

    let directory = tempfile::tempdir().unwrap();
    let store = PinStore::for_profile_storage(&directory.path().join("profiles.sqlite"));
    let path = directory.path().join("profiles.sqlite.pins.json");
    store.save(&[pin("saved", "table-1")]).unwrap();
    assert_eq!(
        fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o600
    );
    fs::set_permissions(&path, fs::Permissions::from_mode(0o640)).unwrap();
    store.save(&[pin("saved", "table-2")]).unwrap();
    assert_eq!(
        fs::metadata(&path).unwrap().permissions().mode() & 0o777,
        0o640
    );
    assert_eq!(store.load().pins[0].object_id, "table-2");
}

#[test]
fn identity_preserves_quoted_unicode_and_utf16_field_limit() {
    let mut record = pin("saved", "α\"β");
    record.qualified_name = "\"模式\".\"名\"".into();
    record.relation_subtype = "外部".into();
    assert_eq!(
        identity_key(&record),
        "[\"saved\",\"table\",\"α\\\"β\",\"\\\"模式\\\".\\\"名\\\"\",\"外部\"]"
    );
    record.name = "🙂".repeat(524_288);
    assert!(valid(&record));
    record.name.push('🙂');
    assert!(!valid(&record));
}

#[test]
fn pinning_prepends_new_pins_and_unpinning_removes_them() {
    let orders = pin("profile-a", "[\"main\",\"orders\"]");
    let items = pin("profile-a", "[\"main\",\"items\"]");
    let pinned = toggle_pin(std::slice::from_ref(&orders), items.clone(), false).unwrap();
    assert_eq!(pinned, vec![items.clone(), orders.clone()]);
    assert_eq!(toggle_pin(&pinned, items.clone(), false), None);
    assert_eq!(
        toggle_pin(&pinned, items.clone(), true),
        Some(vec![orders.clone()])
    );
    assert_eq!(
        toggle_pin(std::slice::from_ref(&orders), items.clone(), true),
        None
    );
    let invalid = PinRecord {
        kind: "loading".into(),
        ..items
    };
    assert_eq!(toggle_pin(&[orders], invalid, false), None);
}

#[test]
fn pins_are_removed_by_identity_or_profile() {
    let orders = pin("profile-a", "[\"main\",\"orders\"]");
    let other = pin("profile-b", "[\"main\",\"orders\"]");
    let pins = vec![orders.clone(), other.clone()];
    assert_eq!(
        remove_pin(&pins, &identity_key(&orders)),
        Some(vec![other.clone()])
    );
    assert_eq!(remove_pin(&pins, "missing"), None);
    assert_eq!(remove_profile_pins(&pins, "profile-b"), Some(vec![orders]));
    assert_eq!(remove_profile_pins(&pins, "profile-c"), None);
}
