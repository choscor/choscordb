use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
use std::{sync::mpsc, time::Duration};

#[test]
fn connection_deadline_includes_queued_tls_preparation() {
    let runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .max_blocking_threads(1)
        .build()
        .unwrap();
    let (release, held) = mpsc::channel::<()>();
    let (started, ready) = mpsc::channel();
    runtime.spawn_blocking(move || {
        started.send(()).unwrap();
        let _ = held.recv();
    });
    ready.recv_timeout(Duration::from_secs(2)).unwrap();
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    listener.set_nonblocking(true).unwrap();
    let port = listener.local_addr().unwrap().port();
    let result = runtime.block_on(async {
        tokio::time::timeout(
            Duration::from_secs(3),
            MysqlDriver.connect(ConnectionOptions::Mysql {
                ssh_jump_secrets: Default::default(),
                ssh_private_key: None,
                ssh_jump_private_keys: Default::default(),
                proxy: None,
                proxy_secret: None,
                host: "127.0.0.1".into(),
                port,
                database: "test".into(),
                user: "tester".into(),
                password: None,
                ssh_secret: None,
                tls: TlsMode::Require,
                root_certificate: None,
                tls_identity: None,
                ssh: Some(SshTunnel {
                    host: "127.0.0.1".into(),
                    port,
                    user: "tester".into(),
                    authentication: SshAuthentication::Agent,
                    identity_source: Default::default(),
                    identity_file: None,
                    options: Box::new(SshOptions {
                        connect_timeout_seconds: 1,
                        ..Default::default()
                    }),
                }),
            }),
        )
        .await
    });
    // Also release on a red-state panic, so the runtime never hangs while dropping.
    drop(release);
    runtime.shutdown_timeout(Duration::from_secs(2));
    let error = result
        .expect("connection must enforce its own deadline while TLS preparation is queued")
        .err()
        .expect("occupied TLS worker cannot establish a connection");
    assert_eq!(error.kind, ErrorKind::Timeout);
    assert_eq!(
        listener.accept().unwrap_err().kind(),
        std::io::ErrorKind::WouldBlock,
        "timed-out preparation must not initiate database or SSH transport later"
    );
}
