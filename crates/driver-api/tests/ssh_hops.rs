use choscordb_driver_api::SshOptions;
use serde_json::{Value, json};
fn settings(jumps: Value) -> Result<SshOptions, serde_json::Error> {
    serde_json::from_value(json!({"jump_hosts":jumps}))
}
#[test]
fn hop_authentication_is_independent_and_legacy_json_remains_unchanged() {
    let legacy = json!([{"host":"jump.example","port":2222,"user":"jump"}]);
    let parsed = settings(legacy.clone()).unwrap();
    parsed.validate().unwrap();
    assert_eq!(serde_json::to_value(parsed).unwrap()["jump_hosts"], legacy);
    let configured = json!([
        {"host":"jump.example","port":2222,"user":"jump","id":"hop_a","authentication":"password"},
        {"host":"second.example","port":22,"user":"other","id":"hop_b","authentication":"public_key","identity_file":"/keys/private","known_hosts_file":"/keys/known_hosts"}
    ]);
    let parsed = settings(configured.clone())
        .expect("independent native hop authentication must deserialize");
    parsed.validate().unwrap();
    assert_eq!(
        serde_json::to_value(parsed).unwrap()["jump_hosts"],
        configured
    );
}
#[test]
fn credential_hop_identifiers_and_authentication_fields_are_validated() {
    for jumps in [
        json!([{"host":"jump","port":22,"user":"user","authentication":"password"}]),
        json!([{"host":"jump","port":22,"user":"user","id":"bad/id","authentication":"password"}]),
        json!([{"host":"jump","port":22,"user":"user","id":"same"},{"host":"second","port":22,"user":"user","id":"same"}]),
        json!([{"host":"jump","port":22,"user":"user","id":"key","authentication":"public_key"}]),
        json!([{"host":"jump","port":22,"user":"user","authentication":"agent","identity_file":"/unexpected"}]),
        json!([{"host":"jump","port":22,"user":"user","agent_socket":"bad\npath"}]),
    ] {
        assert!(settings(jumps).map_or(true, |value| value.validate().is_err()));
    }
}

#[test]
fn secrets_are_validated_per_destination_before_transport_creation() {
    use choscordb_driver_api::*;
    let mut target:SshTunnel=serde_json::from_value(json!({"host":"target","port":22,"user":"target","authentication":"password","options":{"jump_hosts":[{"host":"jump","port":2222,"user":"jump","id":"hop","authentication":"password"}]}})).unwrap();
    let target_secret = Secret::new("target-only");
    let hops = std::collections::BTreeMap::from([("hop".into(), Secret::new("hop-only"))]);
    validate_ssh_chain_authentication(&target, Some(&target_secret), &hops).unwrap();
    assert_eq!(
        validate_ssh_chain_authentication(&target, Some(&target_secret), &Default::default())
            .unwrap_err()
            .kind,
        ErrorKind::Authentication
    );
    let unknown =
        std::collections::BTreeMap::from([("unknown".into(), Secret::new("never-exposed"))]);
    assert_eq!(
        validate_ssh_chain_authentication(&target, Some(&target_secret), &unknown)
            .unwrap_err()
            .kind,
        ErrorKind::InvalidInput
    );
    target.options.jump_hosts[0].authentication = SshJumpAuthentication::PublicKey;
    target.options.jump_hosts[0].identity_file = Some("/unencrypted/key".into());
    let cleared = std::collections::BTreeMap::from([("hop".into(), Secret::new(""))]);
    validate_ssh_chain_authentication(&target, Some(&target_secret), &cleared).unwrap();
}

#[test]
fn raw_proxyjump_configuration_cannot_silently_ignore_per_hop_authentication() {
    use choscordb_driver_api::*;
    let target:SshTunnel=serde_json::from_value(json!({"host":"target","port":22,"user":"target","authentication":"agent","options":{"jump_hosts":[{"host":"jump","port":2222,"user":"jump","id":"hop","authentication":"password"}]}})).unwrap();
    let mut command = tokio::process::Command::new("ssh");
    assert_eq!(
        configure_ssh_authentication(&mut command, &target, None)
            .err()
            .unwrap()
            .kind,
        ErrorKind::InvalidInput
    );
}
