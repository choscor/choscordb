use choscordb_driver_api::{
    ErrorKind, Secret, SshAskpass, SshAuthentication, SshTunnel, configure_ssh_authentication,
    request_ssh_secret, validate_ssh_secret,
};
use std::time::Duration;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[test]
fn ssh_secret_validation_ignores_unused_agent_secrets_and_allows_unencrypted_keys() {
    let mut settings = SshTunnel {
        options: Default::default(),
        host: "bastion.example".into(),
        port: 22,
        user: "operator".into(),
        authentication: SshAuthentication::Agent,
        identity_source: Default::default(),
        identity_file: None,
    };
    for secret in [
        None,
        Some(Secret::new("")),
        Some(Secret::new("unused\0\r\n")),
    ] {
        validate_ssh_secret(&settings, secret.as_ref()).unwrap();
    }
    settings.authentication = SshAuthentication::PublicKey;
    settings.identity_file = Some("/keys/id".into());
    validate_ssh_secret(&settings, None).unwrap();
    validate_ssh_secret(&settings, Some(&Secret::new(""))).unwrap();
}

#[tokio::test]
async fn broker_expires_even_with_a_pending_request() {
    let broker = SshAskpass::new(Secret::new("short lived")).unwrap();
    assert_eq!(
        request_ssh_secret(broker.address(), broker.token())
            .await
            .unwrap()
            .expose(),
        "short lived"
    );
    tokio::time::sleep(Duration::from_secs(29)).await;
    let mut stalled = tokio::net::TcpStream::connect(broker.address())
        .await
        .unwrap();
    stalled.write_all(&[0]).await.unwrap();
    let mut byte = [0];
    assert_eq!(
        tokio::time::timeout(Duration::from_millis(1750), stalled.read(&mut byte))
            .await
            .expect("broker expiry must close requests before their own deadline")
            .unwrap(),
        0
    );
    assert!(
        request_ssh_secret(broker.address(), broker.token())
            .await
            .is_err()
    );
}

#[tokio::test]
async fn secret_request_times_out_when_peer_never_responds() {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let (result, _peer) = tokio::join!(
        tokio::time::timeout(Duration::from_secs(4), request_ssh_secret(address, "token")),
        async {
            let (socket, _) = listener.accept().await.unwrap();
            tokio::time::sleep(Duration::from_secs(4)).await;
            socket
        }
    );
    assert_eq!(
        result
            .expect("the client must enforce its own deadline")
            .unwrap_err()
            .kind(),
        std::io::ErrorKind::TimedOut
    );
}

#[tokio::test]
async fn dropping_broker_closes_pending_requests() {
    let broker = SshAskpass::new(Secret::new("must be released")).unwrap();
    let mut stalled = tokio::net::TcpStream::connect(broker.address())
        .await
        .unwrap();
    stalled.write_all(&[0]).await.unwrap();
    // A completed request proves the worker has run and accepted the earlier socket.
    request_ssh_secret(broker.address(), broker.token())
        .await
        .unwrap();
    drop(broker);
    let mut byte = [0];
    assert_eq!(
        tokio::time::timeout(Duration::from_millis(500), stalled.read(&mut byte))
            .await
            .unwrap()
            .unwrap(),
        0
    );
}

#[tokio::test]
async fn incomplete_request_is_closed_and_broker_remains_usable() {
    let broker = SshAskpass::new(Secret::new("after timeout")).unwrap();
    let mut stalled = tokio::net::TcpStream::connect(broker.address())
        .await
        .unwrap();
    stalled.write_all(&[0]).await.unwrap();
    let mut byte = [0];
    let count = tokio::time::timeout(Duration::from_secs(4), stalled.read(&mut byte))
        .await
        .expect("incomplete request must have a deadline")
        .unwrap();
    assert_eq!(count, 0);
    assert_eq!(
        request_ssh_secret(broker.address(), broker.token())
            .await
            .unwrap()
            .expose(),
        "after timeout"
    );
}

#[tokio::test]
async fn stalled_request_does_not_block_authenticated_requests() {
    let broker = SshAskpass::new(Secret::new("available secret")).unwrap();
    let mut stalled = tokio::net::TcpStream::connect(broker.address())
        .await
        .unwrap();
    stalled.write_all(&[0]).await.unwrap();
    let secret = tokio::time::timeout(
        Duration::from_millis(500),
        request_ssh_secret(broker.address(), broker.token()),
    )
    .await
    .expect("an incomplete token must not block other requests")
    .unwrap();
    assert_eq!(secret.expose(), "available secret");
}

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
        options: Default::default(),
        host: "bastion.example".into(),
        port: 22,
        user: "operator".into(),
        authentication,
        identity_file,
        identity_source: Default::default(),
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
        Some(&Secret::new("stale\0secret\rmust\nbe-ignored")),
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
