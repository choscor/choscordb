use choscordb_driver_api::*;
#[test]
fn local_binding_and_sharing_are_explicit_and_bounded() {
    let settings: SshOptions = serde_json::from_value(
        serde_json::json!({"local_host":"::1","local_port":54321,"share_tunnels":true}),
    )
    .expect("native local binding settings");
    settings.validate().unwrap();
    assert_eq!(
        settings.local_address().unwrap(),
        "[::1]:54321".parse().unwrap()
    );
    let defaults = SshOptions::default();
    assert!(!defaults.share_tunnels);
    assert_eq!(
        defaults.local_address().unwrap(),
        "127.0.0.1:0".parse().unwrap()
    );
    for host in ["localhost; command", "example.com", "127.0.0.1:22", ""] {
        let settings: SshOptions =
            serde_json::from_value(serde_json::json!({"local_host":host})).unwrap();
        assert!(settings.validate().is_err());
    }
    for host in ["127.0.0.2", "0.0.0.0", "::"] {
        let settings: SshOptions =
            serde_json::from_value(serde_json::json!({"local_host":host,"local_port":0})).unwrap();
        settings.validate().unwrap();
    }
}
#[tokio::test]
async fn exact_local_binding_refuses_conflicts_and_releases_with_last_owner() {
    let mut settings:SshTunnel=serde_json::from_value(serde_json::json!({"host":"unused.example","port":22,"user":"user","authentication":"agent","options":{"local_host":"127.0.0.1","share_tunnels":true}})).unwrap();
    let occupied = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    settings.options.local_port = Some(occupied.local_addr().unwrap().port());
    assert!(
        SshLocalForward::open(
            &settings,
            None,
            &Default::default(),
            None,
            &Default::default(),
            "database.example",
            5432
        )
        .await
        .is_err()
    );
    drop(occupied);
    let first = SshLocalForward::open(
        &settings,
        None,
        &Default::default(),
        None,
        &Default::default(),
        "database.example",
        5432,
    )
    .await
    .unwrap();
    let address = first.address();
    assert_eq!(address, settings.options.local_address().unwrap());
    let second = SshLocalForward::open(
        &settings,
        None,
        &Default::default(),
        None,
        &Default::default(),
        "database.example",
        5432,
    )
    .await
    .unwrap();
    assert!(
        std::sync::Arc::ptr_eq(&first, &second),
        "identical opt-in listeners must share ownership"
    );
    let mut changed = settings.clone();
    changed.user = "other-user".into();
    assert!(
        SshLocalForward::open(
            &changed,
            None,
            &Default::default(),
            None,
            &Default::default(),
            "database.example",
            5432
        )
        .await
        .is_err(),
        "different SSH context reused occupied listener"
    );
    drop(first);
    assert!(tokio::net::TcpListener::bind(address).await.is_err());
    drop(second);
    tokio::time::timeout(std::time::Duration::from_secs(2), async {
        loop {
            if tokio::net::TcpListener::bind(address).await.is_ok() {
                break;
            }
            tokio::task::yield_now().await;
        }
    })
    .await
    .expect("last owner retained the local port");
}
#[tokio::test]
async fn ipv6_binding_and_password_context_are_isolated() {
    let mut settings:SshTunnel=serde_json::from_value(serde_json::json!({"host":"unused.example","port":22,"user":"user","authentication":"password","options":{"local_host":"::1","share_tunnels":true}})).unwrap();
    let reservation = tokio::net::TcpListener::bind("[::1]:0").await.unwrap();
    settings.options.local_port = Some(reservation.local_addr().unwrap().port());
    drop(reservation);
    let first_password = Secret::new("first-password");
    let second_password = Secret::new("second-password");
    let first = SshLocalForward::open(
        &settings,
        Some(&first_password),
        &Default::default(),
        None,
        &Default::default(),
        "database.example",
        5432,
    )
    .await
    .unwrap();
    assert!(first.address().is_ipv6());
    assert!(
        SshLocalForward::open(
            &settings,
            Some(&second_password),
            &Default::default(),
            None,
            &Default::default(),
            "database.example",
            5432
        )
        .await
        .is_err()
    );
    let address = first.address();
    drop(first);
    tokio::time::timeout(std::time::Duration::from_secs(2), async {
        loop {
            if tokio::net::TcpListener::bind(address).await.is_ok() {
                break;
            }
            tokio::task::yield_now().await;
        }
    })
    .await
    .unwrap();
}
#[test]
fn different_runtimes_never_share_listener_ownership() {
    let first_runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();
    let second_runtime = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();
    let reserved = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let port = reserved.local_addr().unwrap().port();
    drop(reserved);
    let settings:SshTunnel=serde_json::from_value(serde_json::json!({"host":"unused.example","port":22,"user":"user","authentication":"agent","options":{"local_port":port,"share_tunnels":true}})).unwrap();
    let first = first_runtime
        .block_on(SshLocalForward::open(
            &settings,
            None,
            &Default::default(),
            None,
            &Default::default(),
            "database.example",
            5432,
        ))
        .unwrap();
    assert!(
        second_runtime
            .block_on(SshLocalForward::open(
                &settings,
                None,
                &Default::default(),
                None,
                &Default::default(),
                "database.example",
                5432
            ))
            .is_err()
    );
    first_runtime.block_on(async {
        drop(first);
        tokio::task::yield_now().await;
    });
}

#[cfg(unix)]
#[tokio::test]
async fn failed_shared_master_startup_releases_binding_and_can_retry() {
    let peer = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let reservation = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let local = reservation.local_addr().unwrap();
    drop(reservation);
    let settings: SshTunnel = serde_json::from_value(serde_json::json!({
        "host":"127.0.0.1", "port":peer.local_addr().unwrap().port(),
        "user":"unused", "authentication":"agent", "options":{
            "local_host":"127.0.0.1", "local_port":local.port(),
            "share_tunnels":true, "connect_timeout_seconds":2
        }
    }))
    .unwrap();
    let server = tokio::spawn(async move {
        for _ in 0..2 {
            let (socket, _) = peer.accept().await.unwrap();
            drop(socket);
        }
    });
    for _ in 0..2 {
        let error = match SshForward::open(
            &settings,
            None,
            &Default::default(),
            "database.example",
            5432,
        )
        .await
        {
            Ok(_) => panic!("failed SSH handshake unexpectedly connected"),
            Err(error) => error,
        };
        assert_eq!(error.kind, ErrorKind::Connection);
        tokio::time::timeout(std::time::Duration::from_secs(2), async {
            loop {
                if tokio::net::TcpListener::bind(local).await.is_ok() {
                    break;
                }
                tokio::task::yield_now().await;
            }
        })
        .await
        .expect("failed master retained local binding");
    }
    tokio::time::timeout(std::time::Duration::from_secs(2), server)
        .await
        .unwrap()
        .unwrap();
}
