use choscordb_update::UpdatePreferenceStore;
use std::fs;

#[test]
fn native_ini_locations_follow_qt_fallback_order() {
    let directory = tempfile::tempdir().unwrap();
    let user = directory.path().join("user");
    let system = directory.path().join("system");
    fs::create_dir_all(user.join("com.choscor.ChoscorDB")).unwrap();
    fs::create_dir_all(system.join("com.choscor.ChoscorDB")).unwrap();
    let sources = [
        user.join("com.choscor.ChoscorDB/ChoscorDB.conf"),
        user.join("com.choscor.ChoscorDB.conf"),
        system.join("com.choscor.ChoscorDB/ChoscorDB.conf"),
        system.join("com.choscor.ChoscorDB.conf"),
    ];
    for (index, source) in sources.iter().enumerate() {
        fs::write(
            source,
            format!("[updates]\nbackgroundConsent={}\n", index % 2 == 0),
        )
        .unwrap();
    }
    for (index, source) in sources.iter().enumerate() {
        let data = directory.path().join(format!("data-{index}"));
        let store = UpdatePreferenceStore::new(&data);
        assert_eq!(
            store
                .load_with_legacy_config(&user, std::slice::from_ref(&system))
                .unwrap(),
            Some(index % 2 == 0)
        );
        fs::remove_file(source).unwrap();
    }
}

#[test]
fn system_application_settings_precede_organization_settings_across_config_roots() {
    let directory = tempfile::tempdir().unwrap();
    let first = directory.path().join("first");
    let second = directory.path().join("second");
    fs::create_dir_all(&first).unwrap();
    fs::create_dir_all(second.join("com.choscor.ChoscorDB")).unwrap();
    fs::write(
        first.join("com.choscor.ChoscorDB.conf"),
        b"[updates]\nbackgroundConsent=true\n",
    )
    .unwrap();
    fs::write(
        second.join("com.choscor.ChoscorDB/ChoscorDB.conf"),
        b"[updates]\nbackgroundConsent=false\n",
    )
    .unwrap();
    let store = UpdatePreferenceStore::new(&directory.path().join("new"));
    assert_eq!(
        store
            .load_with_legacy_config(&directory.path().join("user"), &[first, second])
            .unwrap(),
        Some(false)
    );
}

#[cfg(target_os = "linux")]
#[test]
fn linux_native_legacy_locations_follow_qt_fallback_order() {
    const CHILD: &str = "CHOSCORDB_LEGACY_TEST_CHILD";
    if let Some(data) = std::env::var_os(CHILD) {
        let store = UpdatePreferenceStore::new(std::path::Path::new(&data));
        let expected = std::env::var("CHOSCORDB_LEGACY_TEST_EXPECTED").unwrap() == "true";
        assert_eq!(store.load_with_native_legacy().unwrap(), Some(expected));
        assert_eq!(store.load().unwrap(), Some(expected));
        return;
    }
    let directory = tempfile::tempdir().unwrap();
    let user = directory.path().join("user");
    let system = directory.path().join("system");
    fs::create_dir_all(user.join("com.choscor.ChoscorDB")).unwrap();
    fs::create_dir_all(system.join("com.choscor.ChoscorDB")).unwrap();
    let sources = [
        user.join("com.choscor.ChoscorDB/ChoscorDB.conf"),
        user.join("com.choscor.ChoscorDB.conf"),
        system.join("com.choscor.ChoscorDB/ChoscorDB.conf"),
        system.join("com.choscor.ChoscorDB.conf"),
    ];
    for (index, source) in sources.iter().enumerate() {
        fs::write(
            source,
            format!("[updates]\nbackgroundConsent={}\n", index % 2 == 0),
        )
        .unwrap();
    }
    for (index, source) in sources.iter().enumerate() {
        let data = directory.path().join(format!("data-{index}"));
        let status = std::process::Command::new(std::env::current_exe().unwrap())
            .args([
                "--exact",
                "linux_native_legacy_locations_follow_qt_fallback_order",
                "--nocapture",
            ])
            .env(CHILD, &data)
            .env(
                "CHOSCORDB_LEGACY_TEST_EXPECTED",
                (index % 2 == 0).to_string(),
            )
            .env("XDG_CONFIG_HOME", &user)
            .env("XDG_CONFIG_DIRS", &system)
            .status()
            .unwrap();
        assert!(status.success());
        fs::remove_file(source).unwrap();
    }
}

#[test]
fn qt_ini_legacy_consent_is_migrated_without_rewriting_legacy() {
    let directory = tempfile::tempdir().unwrap();
    let legacy = directory.path().join("legacy.conf");
    let content = b"[updates]\nbackgroundConsent=true\n";
    fs::write(&legacy, content).unwrap();
    let store = UpdatePreferenceStore::new(&directory.path().join("new"));
    assert_eq!(
        store
            .load_with_legacy_ini(std::slice::from_ref(&legacy))
            .unwrap(),
        Some(true)
    );
    assert_eq!(store.load().unwrap(), Some(true));
    assert_eq!(fs::read(legacy).unwrap(), content);
}

#[test]
fn legacy_ini_reads_false_and_fallbacks_in_order_but_keeps_saved_consent() {
    let directory = tempfile::tempdir().unwrap();
    let missing = directory.path().join("missing.conf");
    let unrelated = directory.path().join("user.conf");
    fs::write(&unrelated, b"[editor]\nbackgroundConsent=true\n").unwrap();
    let organization = directory.path().join("organization.conf");
    fs::write(&organization, b"[updates]\nbackgroundConsent=false\n").unwrap();
    let system = directory.path().join("system.conf");
    fs::write(&system, b"[updates]\nbackgroundConsent=true\n").unwrap();
    let store = UpdatePreferenceStore::new(&directory.path().join("new"));
    assert_eq!(
        store
            .load_with_legacy_ini(&[missing.clone(), unrelated.clone()])
            .unwrap(),
        None
    );
    assert_eq!(
        store
            .load_with_legacy_ini(&[missing, unrelated, organization, system.clone()])
            .unwrap(),
        Some(false)
    );
    assert_eq!(store.load_with_legacy_ini(&[system]).unwrap(), Some(false));
}

#[test]
fn malformed_and_oversized_legacy_ini_cannot_grant_consent() {
    let directory = tempfile::tempdir().unwrap();
    let legacy = directory.path().join("legacy.conf");
    let store = UpdatePreferenceStore::new(&directory.path().join("new"));
    fs::write(&legacy, b"[updates]\nbackgroundConsent=anything\n").unwrap();
    assert!(
        store
            .load_with_legacy_ini(std::slice::from_ref(&legacy))
            .is_err()
    );
    assert_eq!(store.load().unwrap(), None);
    let mut oversized = b"[updates]\nbackgroundConsent=true\n".to_vec();
    oversized.resize(1024 * 1024 + 1, b' ');
    fs::write(&legacy, oversized).unwrap();
    assert!(store.load_with_legacy_ini(&[legacy]).is_err());
    assert_eq!(store.load().unwrap(), None);
}

#[cfg(unix)]
#[test]
fn legacy_ini_symlinks_and_relative_paths_cannot_grant_consent() {
    use std::os::unix::fs::symlink;
    let directory = tempfile::tempdir().unwrap();
    let target = directory.path().join("target.conf");
    let content = b"[updates]\nbackgroundConsent=true\n";
    fs::write(&target, content).unwrap();
    let legacy = directory.path().join("legacy.conf");
    symlink(&target, &legacy).unwrap();
    let store = UpdatePreferenceStore::new(&directory.path().join("new"));
    assert!(store.load_with_legacy_ini(&[legacy]).is_err());
    assert!(
        store
            .load_with_legacy_ini(&["relative.conf".into()])
            .is_err()
    );
    assert_eq!(store.load().unwrap(), None);
    assert_eq!(fs::read(target).unwrap(), content);
}

#[cfg(windows)]
#[test]
fn native_registry_fallbacks_migrate_real_values_without_rewriting_them() {
    use winreg::{
        RegKey,
        enums::{HKEY_CURRENT_USER, KEY_READ, KEY_WRITE},
    };
    struct RegistryFixture(String);
    impl Drop for RegistryFixture {
        fn drop(&mut self) {
            let _ = RegKey::predef(HKEY_CURRENT_USER).delete_subkey_all(&self.0);
        }
    }
    let directory = tempfile::tempdir().unwrap();
    let registry_root = format!(
        "Software\\ChoscorDBPreferenceTests\\{}-{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos()
    );
    let fixture = RegistryFixture(registry_root);
    let hive = RegKey::predef(HKEY_CURRENT_USER);
    let (root, _) = hive.create_subkey(&fixture.0).unwrap();
    let (user, _) = root.create_subkey("user").unwrap();
    let (system, _) = root.create_subkey("system").unwrap();
    let applications = ["ChoscorDB", "OrganizationDefaults"];
    for (index, location) in [&user, &system]
        .iter()
        .flat_map(|hive| applications.map(|app| (*hive, app)))
        .enumerate()
    {
        let (key, _) = location
            .0
            .create_subkey(format!(
                "Software\\com.choscor.ChoscorDB\\{}\\updates",
                location.1
            ))
            .unwrap();
        key.set_value("backgroundConsent", &(index % 2 == 0).to_string())
            .unwrap();
    }
    for (index, location) in [&user, &system]
        .iter()
        .flat_map(|hive| applications.map(|app| (*hive, app)))
        .enumerate()
    {
        let store = UpdatePreferenceStore::new(&directory.path().join(format!("data-{index}")));
        assert_eq!(
            store.load_with_legacy_registry(&user, &system).unwrap(),
            Some(index % 2 == 0)
        );
        let key = location
            .0
            .open_subkey_with_flags(
                format!("Software\\com.choscor.ChoscorDB\\{}\\updates", location.1),
                KEY_READ | KEY_WRITE,
            )
            .unwrap();
        assert_eq!(
            key.get_value::<String, _>("backgroundConsent").unwrap(),
            (index % 2 == 0).to_string()
        );
        key.set_value("backgroundConsent", &"unexpected").unwrap();
        assert_eq!(
            store.load_with_legacy_registry(&user, &system).unwrap(),
            Some(index % 2 == 0)
        );
        let fresh =
            UpdatePreferenceStore::new(&directory.path().join(format!("malformed-{index}")));
        assert!(fresh.load_with_legacy_registry(&user, &system).is_err());
        key.delete_value("backgroundConsent").unwrap();
    }
    let store = UpdatePreferenceStore::new(&directory.path().join("absent"));
    assert_eq!(
        store.load_with_legacy_registry(&user, &system).unwrap(),
        None
    );
}

#[test]
fn missing_consent_requires_decision_and_legacy_choice_migrates_once() {
    let directory = tempfile::tempdir().unwrap();
    let store = UpdatePreferenceStore::new(directory.path());
    assert_eq!(store.load().unwrap(), None);
    assert_eq!(store.load_or_migrate(Some(true)).unwrap(), Some(true));
    assert_eq!(store.load().unwrap(), Some(true));
    assert_eq!(store.load_or_migrate(Some(false)).unwrap(), Some(true));
    store.save(false).unwrap();
    assert_eq!(store.load().unwrap(), Some(false));
}

#[test]
fn invalid_or_oversized_preference_cannot_grant_background_consent() {
    let directory = tempfile::tempdir().unwrap();
    let store = UpdatePreferenceStore::new(directory.path());
    let path = directory.path().join("update-preferences.json");
    fs::write(&path, b"{\"version\":1,\"background_consent\":\"true\"}").unwrap();
    assert!(store.load().is_err());
    fs::write(&path, vec![b'x'; 4097]).unwrap();
    assert!(store.load().is_err());
}

#[test]
fn failed_save_leaves_previous_consent_intact() {
    let directory = tempfile::tempdir().unwrap();
    let store = UpdatePreferenceStore::new(directory.path());
    store.save(true).unwrap();
    let path = directory.path().join("update-preferences.json");
    assert!(
        fs::read(&path)
            .unwrap()
            .windows(4)
            .any(|part| part == b"true")
    );
    let blocked = UpdatePreferenceStore::new(&path);
    assert!(blocked.save(false).is_err());
    assert_eq!(store.load().unwrap(), Some(true));
}

#[cfg(unix)]
#[test]
fn symlink_preference_cannot_read_or_replace_another_file() {
    use std::os::unix::fs::symlink;
    let directory = tempfile::tempdir().unwrap();
    let target = directory.path().join("other.json");
    fs::write(&target, b"private content").unwrap();
    let path = directory.path().join("update-preferences.json");
    symlink(&target, &path).unwrap();
    let store = UpdatePreferenceStore::new(directory.path());
    assert!(store.load().is_err());
    assert!(store.save(true).is_err());
    assert_eq!(fs::read(target).unwrap(), b"private content");
}
