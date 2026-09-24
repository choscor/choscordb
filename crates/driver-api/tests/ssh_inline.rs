use choscordb_driver_api::*;
#[test]
fn inline_key_source_is_metadata_not_key_material() {
    let settings: SshTunnel = serde_json::from_value(serde_json::json!({
        "host":"localhost","port":22,"user":"alice","authentication":"public_key",
        "identity_source":"inline","identity_file":null
    }))
    .expect("inline identity source");
    settings.validate().unwrap();
    let serialized = serde_json::to_value(&settings).unwrap();
    assert_eq!(serialized["identity_source"], "inline");
    let mut conflicting = serialized;
    conflicting["identity_file"] = "/private/key".into();
    assert!(
        serde_json::from_value::<SshTunnel>(conflicting)
            .unwrap()
            .validate()
            .is_err()
    );
}
#[test]
fn inline_hop_requires_stable_id_and_public_key_authentication() {
    for authentication in ["public_key", "agent", "configured", "password"] {
        let options: SshOptions = serde_json::from_value(serde_json::json!({"jump_hosts":[{
            "id":"jump-one","host":"localhost","port":22,"user":"alice",
            "authentication":authentication,"identity_source":"inline"
        }]}))
        .expect("inline jump source");
        assert_eq!(options.validate().is_ok(), authentication == "public_key");
    }
}
#[test]
fn inline_material_is_required_bounded_and_destination_scoped() {
    let settings: SshTunnel = serde_json::from_value(serde_json::json!({
        "host":"localhost","port":22,"user":"alice","authentication":"public_key",
        "identity_source":"inline","options":{"jump_hosts":[{
            "id":"hop","host":"localhost","port":22,"user":"bob",
            "authentication":"public_key","identity_source":"inline"
        }]}
    }))
    .unwrap();
    let key = Secret::new("PRIVATE FIXTURE MATERIAL");
    let no_keys = Default::default();
    let hops =
        std::collections::BTreeMap::from([("hop".into(), Secret::new("PRIVATE FIXTURE MATERIAL"))]);
    let valid =
        validate_ssh_chain_authentication_with_keys(&settings, None, &no_keys, Some(&key), &hops);
    if cfg!(unix) {
        assert!(valid.is_ok());
    } else {
        assert_eq!(valid.unwrap_err().kind, ErrorKind::Unsupported);
    }
    for keys in [
        &no_keys,
        &std::collections::BTreeMap::from([(
            "other".into(),
            Secret::new("PRIVATE FIXTURE MATERIAL"),
        )]),
    ] {
        assert!(
            validate_ssh_chain_authentication_with_keys(
                &settings,
                None,
                &no_keys,
                Some(&key),
                keys
            )
            .is_err()
        );
    }
    for key in [
        None,
        Some(Secret::new("")),
        Some(Secret::new("a\0b")),
        Some(Secret::new("x".repeat(65537))),
    ] {
        let error = validate_ssh_chain_authentication_with_keys(
            &settings,
            None,
            &no_keys,
            key.as_ref(),
            &hops,
        )
        .unwrap_err();
        assert!(!format!("{error:?}").contains("PRIVATE FIXTURE MATERIAL"));
    }
    let mut command = tokio::process::Command::new("ssh");
    assert!(
        configure_ssh_authentication(&mut command, &settings, None).is_err(),
        "raw helper must not fall back to configured identities"
    );
}
