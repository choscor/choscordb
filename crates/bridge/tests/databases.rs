use choscordb_bridge::{ffi, profile_with_database};

fn profile(driver: &str) -> ffi::ProfileDto {
    ffi::ProfileDto {
        driver: driver.into(),
        path: "/data/main.sqlite".into(),
        database: "app".into(),
        ..Default::default()
    }
}

#[test]
fn switching_sets_the_field_that_each_driver_connects_with() {
    let sqlite = profile_with_database(profile("sqlite"), "/data/archive.db");
    assert!(sqlite.valid, "{}", sqlite.error);
    assert_eq!(sqlite.profile.path, "/data/archive.db");
    assert_eq!(sqlite.profile.database, "app");
    for driver in ["postgres", "mysql", "mariadb"] {
        let switched = profile_with_database(profile(driver), "reports");
        assert!(switched.valid, "{driver}");
        assert_eq!(switched.profile.database, "reports");
        assert_eq!(switched.profile.path, "/data/main.sqlite");
    }
}

#[test]
fn switching_rejects_empty_targets_and_unknown_drivers() {
    assert_eq!(
        profile_with_database(profile("postgres"), "").error,
        "Choose a database."
    );
    let unknown = profile_with_database(profile("oracle"), "reports");
    assert!(!unknown.valid);
    assert_eq!(unknown.error, "This connection cannot switch databases.");
}
