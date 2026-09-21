use choscordb_driver_api::{
    ErrorKind, Secret, SshAskpass, SshAuthentication, SshTunnel, configure_ssh_authentication,
    request_ssh_secret,
};

#[tokio::test]
async fn askpass_broker_serves_only_authenticated_local_requests() {
    let broker = SshAskpass::new(Secret::new("correct horse battery staple")).unwrap();
    assert!(
        request_ssh_secret(broker.address(), "wrong-token")
            .await
            .is_err()
    );
    for _ in 0..2 {
        let secret = request_ssh_secret(broker.address(), broker.token())
            .await
            .unwrap();
        assert_eq!(secret.expose(), "correct horse battery staple");
    }
}

#[tokio::test]
async fn authentication_methods_select_only_the_requested_openssh_mechanism() {
    let settings = |authentication, identity_file| SshTunnel {
        host: "bastion.example".into(),
        port: 22,
        user: "operator".into(),
        authentication,
        identity_file,
    };
    let mut password = tokio::process::Command::new("ssh");
    let _broker = configure_ssh_authentication(
        &mut password,
        &settings(SshAuthentication::Password, None),
        Some(&Secret::new("password")),
    )
    .unwrap();
    let password_args = password
        .as_std()
        .get_args()
        .map(|value| value.to_string_lossy())
        .collect::<Vec<_>>()
        .join(" ");
    assert!(password_args.contains("PreferredAuthentications=password"));
    assert!(password_args.contains("PubkeyAuthentication=no"));
    assert!(password_args.contains("BatchMode=no"));

    let mut key = tokio::process::Command::new("ssh");
    let _broker = configure_ssh_authentication(
        &mut key,
        &settings(SshAuthentication::PublicKey, Some("/keys/id".into())),
        Some(&Secret::new("passphrase")),
    )
    .unwrap();
    let key_args = key
        .as_std()
        .get_args()
        .map(|value| value.to_string_lossy())
        .collect::<Vec<_>>()
        .join(" ");
    assert!(key_args.contains("PreferredAuthentications=publickey"));
    assert!(key_args.contains("IdentitiesOnly=yes"));

    let mut agent = tokio::process::Command::new("ssh");
    let broker = configure_ssh_authentication(
        &mut agent,
        &settings(SshAuthentication::Agent, None),
        Some(&Secret::new("stale-secret-must-be-ignored")),
    )
    .unwrap();
    assert!(broker.is_none());
    let agent_args = agent
        .as_std()
        .get_args()
        .map(|value| value.to_string_lossy())
        .collect::<Vec<_>>()
        .join(" ");
    assert!(agent_args.contains("IdentityFile=none"));

    let mut missing = tokio::process::Command::new("ssh");
    let result = configure_ssh_authentication(
        &mut missing,
        &settings(SshAuthentication::Password, None),
        None,
    );
    let Err(error) = result else {
        panic!("missing SSH password was accepted")
    };
    assert_eq!(error.kind, ErrorKind::Authentication);
}
