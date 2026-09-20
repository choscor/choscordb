use choscordb_credentials::{CredentialError, CredentialStore, NativeCredentialStore, Secret};

#[test]
#[ignore = "Requires an unlocked OS credential store; writes only a fresh isolated test entry"]
fn isolated_native_round_trip() {
    let store =
        NativeCredentialStore::for_test_namespace("com.choscor.ChoscorDB.tests.credentials")
            .unwrap();
    let reference = uuid::Uuid::new_v4().to_string();
    struct Cleanup<'a>(&'a NativeCredentialStore, &'a str);
    impl Drop for Cleanup<'_> {
        fn drop(&mut self) {
            let _ = self.0.delete(self.1);
        }
    }
    let _cleanup = Cleanup(&store, &reference);
    assert!(matches!(
        store.get(&reference),
        Err(CredentialError::Missing)
    ));
    store
        .put(&reference, &Secret::new("isolated-test-α-password"))
        .unwrap();
    assert_eq!(
        store.get(&reference).unwrap().expose(),
        "isolated-test-α-password"
    );
    store.put(&reference, &Secret::new("replacement")).unwrap();
    assert_eq!(store.get(&reference).unwrap().expose(), "replacement");
    store.delete(&reference).unwrap();
    store.delete(&reference).unwrap();
    assert!(matches!(
        store.get(&reference),
        Err(CredentialError::Missing)
    ));
}
