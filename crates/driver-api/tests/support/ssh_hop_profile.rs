use choscordb_driver_api::*;
pub fn value(name: &str) -> String {
    std::env::var(name).unwrap_or_else(|_| panic!("missing fixture variable {name}"))
}
pub fn settings(
    mode: &str,
) -> (
    SshTunnel,
    Option<Secret>,
    std::collections::BTreeMap<String, Secret>,
) {
    let mut hop = SshJumpHost {
        host: "127.0.0.1".into(),
        port: value("CHOSCORDB_SSH_JUMP_PORT").parse().unwrap(),
        user: "root".into(),
        id: Some("hop_auth".into()),
        authentication: SshJumpAuthentication::Password,
        known_hosts_file: Some(value("CHOSCORDB_SSH_KNOWN_HOSTS")),
        ..Default::default()
    };
    let mut secrets = std::collections::BTreeMap::from([(
        "hop_auth".into(),
        Secret::new(if mode == "wrong-hop" {
            "wrong-hop-password"
        } else {
            "jump-password"
        }),
    )]);
    let mut target = SshTunnel {
        host: if mode == "alias" {
            "target-alias"
        } else {
            "target-ssh"
        }
        .into(),
        port: 22,
        user: "root".into(),
        authentication: SshAuthentication::Password,
        identity_source: Default::default(),
        identity_file: None,
        options: Box::new(SshOptions {
            connect_timeout_seconds: 8,
            known_hosts_file: Some(value("CHOSCORDB_SSH_KNOWN_HOSTS")),
            ..Default::default()
        }),
    };
    let mut secret = Some(Secret::new("target-password"));
    match mode {
        "hop-key" => {
            hop.authentication = SshJumpAuthentication::PublicKey;
            hop.identity_file = Some(value("CHOSCORDB_SSH_HOP_KEY"));
            secrets.insert("hop_auth".into(), Secret::new("hop-key-password"));
        }
        "target-key" => {
            target.authentication = SshAuthentication::PublicKey;
            target.identity_file = Some(value("CHOSCORDB_SSH_TARGET_KEY"));
            secret = Some(Secret::new("target-key-password"));
        }
        "agent" => {
            hop.authentication = SshJumpAuthentication::Agent;
            hop.agent_socket = Some(value("SSH_AUTH_SOCK"));
            secrets.clear();
        }
        "configured" => {
            hop.host = "jump-alias".into();
            hop.authentication = SshJumpAuthentication::Configured;
            secrets.clear();
        }
        "bad-hop-trust" => {
            hop.known_hosts_file = Some(value("CHOSCORDB_SSH_EMPTY_HOSTS"));
        }
        _ => (),
    }
    target.options.jump_hosts = vec![hop];
    if mode.starts_with("two-hops") || mode == "five-hops" {
        let count = if mode == "five-hops" { 4 } else { 1 };
        for index in 0..count {
            let id = format!("additional_{index}");
            let final_user = index % 2 == 1;
            target.options.jump_hosts.push(SshJumpHost {
                host: if index == 0 {
                    "target-ssh"
                } else {
                    "127.0.0.1"
                }
                .into(),
                port: 22,
                user: if final_user { "final" } else { "root" }.into(),
                id: Some(id.clone()),
                authentication: SshJumpAuthentication::Password,
                known_hosts_file: Some(value("CHOSCORDB_SSH_KNOWN_HOSTS")),
                ..Default::default()
            });
            secrets.insert(
                id,
                Secret::new(if mode == "two-hops-wrong-second" {
                    "wrong-second-secret"
                } else if final_user {
                    "final-password"
                } else {
                    "target-password"
                }),
            );
        }
        target.host = "127.0.0.1".into();
        target.user = "final".into();
        secret = Some(Secret::new("final-password"));
        target.options.connect_timeout_seconds = 12;
    }
    if mode.starts_with("inline") {
        target.authentication = SshAuthentication::PublicKey;
        target.identity_source = SshIdentitySource::Inline;
        secret = Some(Secret::new(if mode == "inline-wrong-passphrase" {
            "wrong-passphrase"
        } else {
            "target-key-password"
        }));
        if mode != "inline-target" {
            let hop = &mut target.options.jump_hosts[0];
            hop.authentication = SshJumpAuthentication::PublicKey;
            hop.identity_source = SshIdentitySource::Inline;
            secrets.insert("hop_auth".into(), Secret::new("hop-key-password"));
        }
    }
    if mode == "inline-unencrypted" {
        secret = None;
        secrets.clear();
    }
    if mode == "inline-agent" {
        let hop = &mut target.options.jump_hosts[0];
        hop.authentication = SshJumpAuthentication::Agent;
        hop.identity_source = SshIdentitySource::File;
        hop.agent_socket = Some(value("SSH_AUTH_SOCK"));
        secrets.clear();
    }
    if mode == "inline-hop-file-target" {
        target.identity_source = SshIdentitySource::File;
        target.identity_file = Some(value("CHOSCORDB_SSH_TARGET_KEY"));
    }
    if mode == "inline-bad-trust" {
        target.options.known_hosts_file = Some(value("CHOSCORDB_SSH_EMPTY_HOSTS"));
    }
    (target, secret, secrets)
}
pub async fn verify(driver: &dyn DatabaseDriver, options: ConnectionOptions, mode: &str) {
    let result = driver.connect(options).await;
    if matches!(
        mode,
        "wrong-hop"
            | "two-hops-wrong-second"
            | "bad-hop-trust"
            | "wrong-tls"
            | "inline-wrong-passphrase"
            | "inline-wrong-key"
            | "inline-bad-trust"
    ) {
        let error = result
            .err()
            .expect("untrusted or incorrectly authenticated hop must never connect");
        if mode == "wrong-tls" {
            assert_eq!(
                error.kind,
                ErrorKind::Tls,
                "database hostname must remain the TLS verification identity"
            );
        }
        return;
    }
    let mut connection = result.unwrap();
    let mut cursor = connection
        .execute("SELECT 913", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(913)]]
    );
    cursor.close().await.unwrap();
    if mode == "password" || mode == "sharing" {
        if driver.id() == "mysql" {
            for sql in [
                "CREATE TABLE IF NOT EXISTS choscordb_test.ssh_hop_objects(value INTEGER)",
                "DELETE FROM choscordb_test.ssh_hop_objects",
                "INSERT INTO choscordb_test.ssh_hop_objects VALUES(914)",
            ] {
                let mut cursor = connection
                    .execute(sql, QueryOptions::default())
                    .await
                    .unwrap();
                cursor.fetch_page(PageSize::default()).await.unwrap();
                cursor.close().await.unwrap();
            }
            let mut cursor = connection
                .open_object(
                    &ObjectId(r#"["choscordb_test","ssh_hop_objects"]"#.into()),
                    1024 * 1024,
                )
                .await
                .unwrap();
            assert_eq!(
                cursor.fetch_page(PageSize::default()).await.unwrap().rows,
                vec![vec![Value::Integer(914)]]
            );
            cursor.close().await.unwrap();
        }
        let cancel = connection.cancellation_handle();
        let sql = if driver.id() == "mysql" {
            "SELECT SLEEP(30)"
        } else {
            "SELECT pg_sleep(30)"
        };
        let executing = async {
            let mut cursor = connection
                .execute(
                    sql,
                    QueryOptions {
                        timeout: Some(std::time::Duration::from_secs(7)),
                        ..Default::default()
                    },
                )
                .await?;
            cursor.fetch_page(PageSize::default()).await
        };
        let stopping = async {
            tokio::time::sleep(std::time::Duration::from_millis(500)).await;
            cancel.cancel().await.unwrap();
        };
        let (result, ()) = tokio::join!(executing, stopping);
        assert_eq!(result.unwrap_err().kind, ErrorKind::Cancelled);
        let mut cursor = connection
            .execute("SELECT 915", QueryOptions::default())
            .await
            .unwrap();
        assert_eq!(
            cursor.fetch_page(PageSize::default()).await.unwrap().rows,
            vec![vec![Value::Integer(915)]]
        );
        cursor.close().await.unwrap();
    }
    connection.close().await.unwrap();
}

pub async fn deadline(driver: &dyn DatabaseDriver, mut options: ConnectionOptions) {
    use tokio::io::AsyncReadExt;
    if let ConnectionOptions::Mysql {
        ssh: Some(settings),
        ..
    }
    | ConnectionOptions::Postgres {
        ssh: Some(settings),
        ..
    } = &mut options
    {
        settings.options.connect_timeout_seconds = 1;
    }
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    std::fs::write(
        value("CHOSCORDB_SSH_STALL_ADDRESS"),
        listener.local_addr().unwrap().to_string(),
    )
    .unwrap();
    std::fs::write(value("CHOSCORDB_SSH_STALL_FLAG"), "owned-fixture").unwrap();
    let started = std::time::Instant::now();
    let (result, accepted) = tokio::join!(
        driver.connect(options),
        tokio::time::timeout(std::time::Duration::from_secs(3), listener.accept())
    );
    assert_eq!(
        result
            .err()
            .expect("stalled SSH preflight cannot connect")
            .kind,
        ErrorKind::Timeout
    );
    assert!(started.elapsed() < std::time::Duration::from_secs(3));
    let mut socket = accepted.unwrap().unwrap().0;
    let mut bytes = Vec::new();
    tokio::time::timeout(
        std::time::Duration::from_secs(1),
        socket.read_to_end(&mut bytes),
    )
    .await
    .expect("connection deadline retained a preflight descendant")
    .unwrap();
    std::fs::remove_file(value("CHOSCORDB_SSH_STALL_FLAG")).unwrap();
}

pub fn inline_keys(mode: &str) -> (Option<Secret>, std::collections::BTreeMap<String, Secret>) {
    if !mode.starts_with("inline") {
        return (None, Default::default());
    }
    let read =
        |name| Secret::new(std::fs::read_to_string(value(name)).expect("owned fixture identity"));
    let target = if mode == "inline-hop-file-target" {
        None
    } else {
        Some(read(if mode == "inline-unencrypted" {
            "CHOSCORDB_SSH_AGENT_KEY"
        } else if mode == "inline-wrong-key" {
            "CHOSCORDB_SSH_HOP_KEY"
        } else {
            "CHOSCORDB_SSH_TARGET_KEY"
        }))
    };
    let keys = if matches!(mode, "inline-target" | "inline-agent") {
        Default::default()
    } else {
        std::collections::BTreeMap::from([(
            "hop_auth".into(),
            read(if mode == "inline-unencrypted" {
                "CHOSCORDB_SSH_AGENT_KEY"
            } else {
                "CHOSCORDB_SSH_HOP_KEY"
            }),
        )])
    };
    (target, keys)
}

pub async fn abort_preflight(driver: &dyn DatabaseDriver, options: ConnectionOptions) {
    use tokio::io::AsyncReadExt;
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    std::fs::write(
        value("CHOSCORDB_SSH_STALL_ADDRESS"),
        listener.local_addr().unwrap().to_string(),
    )
    .unwrap();
    std::fs::write(value("CHOSCORDB_SSH_STALL_FLAG"), "owned-fixture").unwrap();
    let mut socket = {
        let attempt = driver.connect(options);
        tokio::pin!(attempt);
        tokio::select! {
            accepted = listener.accept() => accepted.unwrap().0,
            result = &mut attempt => panic!("stalled connect completed: {:?}", result.err()),
            _ = tokio::time::sleep(std::time::Duration::from_secs(3)) => panic!("preflight not observed"),
        }
    };
    let mut bytes = Vec::new();
    tokio::time::timeout(
        std::time::Duration::from_secs(2),
        socket.read_to_end(&mut bytes),
    )
    .await
    .expect("aborted connect retained child")
    .unwrap();
    std::fs::remove_file(value("CHOSCORDB_SSH_STALL_FLAG")).unwrap();
}

pub async fn trust(driver: &dyn DatabaseDriver, make_options: impl Fn() -> ConnectionOptions) {
    let path = std::path::PathBuf::from(value("CHOSCORDB_SSH_TRUST_PATH"));
    let _ = std::fs::remove_file(&path);
    std::fs::write(&path, "").unwrap();
    let options = || {
        let mut options = make_options();
        match &mut options {
            ConnectionOptions::Mysql {
                ssh: Some(settings),
                ..
            }
            | ConnectionOptions::Postgres {
                ssh: Some(settings),
                ..
            } => {
                settings.host = "target-alias".into();
                settings.options.known_hosts_file = Some(path.to_str().unwrap().into());
                for hop in &mut settings.options.jump_hosts {
                    hop.known_hosts_file = Some(path.to_str().unwrap().into());
                }
            }
            _ => unreachable!(),
        }
        options
    };
    assert!(
        driver.connect(options()).await.is_err(),
        "unknown localhost hop was implicitly trusted"
    );
    fn transport(
        options: ConnectionOptions,
    ) -> (
        SshTunnel,
        std::collections::BTreeMap<String, Secret>,
        std::collections::BTreeMap<String, Secret>,
    ) {
        match options {
            ConnectionOptions::Mysql {
                ssh: Some(settings),
                ssh_jump_secrets,
                ssh_jump_private_keys,
                ..
            }
            | ConnectionOptions::Postgres {
                ssh: Some(settings),
                ssh_jump_secrets,
                ssh_jump_private_keys,
                ..
            } => (settings, ssh_jump_secrets, ssh_jump_private_keys),
            _ => unreachable!(),
        }
    }
    let (settings, secrets, keys) = transport(options());
    let keys = inspect_ssh_host_keys(
        &settings,
        SshHostKeyTarget::Jump("hop_auth".into()),
        secrets,
        keys,
    )
    .await
    .unwrap();
    assert!(!keys.is_empty());
    approve_ssh_host_key(&keys[0], &keys[0].sha256, &path)
        .await
        .unwrap();
    assert!(
        driver.connect(options()).await.is_err(),
        "unknown target was implicitly trusted"
    );
    let (settings, secrets, keys) = transport(options());
    let keys = inspect_ssh_host_keys(&settings, SshHostKeyTarget::Target, secrets, keys)
        .await
        .unwrap();
    assert!(!keys.is_empty());
    assert_eq!(keys[0].hostname, "target-ssh");
    assert_eq!(keys[0].host_key_alias.as_deref(), Some("fixture-target"));
    approve_ssh_host_key(&keys[0], &keys[0].sha256, &path)
        .await
        .unwrap();
    verify(driver, options(), "trust").await;
}

pub async fn binding(driver: &dyn DatabaseDriver, make_options: impl Fn() -> ConnectionOptions) {
    let reserved = tokio::net::TcpListener::bind("[::1]:0").await.unwrap();
    let address = reserved.local_addr().unwrap();
    drop(reserved);
    let mut options = make_options();
    match &mut options {
        ConnectionOptions::Mysql {
            ssh: Some(settings),
            ..
        }
        | ConnectionOptions::Postgres {
            ssh: Some(settings),
            ..
        } => {
            settings.options.local_host = Some("::1".into());
            settings.options.local_port = Some(address.port());
        }
        _ => unreachable!(),
    }
    let mut owner = driver.connect(options).await.unwrap();
    let mut direct = make_options();
    match &mut direct {
        ConnectionOptions::Mysql {
            ssh,
            ssh_secret,
            ssh_private_key,
            ssh_jump_secrets,
            ssh_jump_private_keys,
            port,
            host,
            ..
        } => {
            *ssh = None;
            *ssh_secret = None;
            *ssh_private_key = None;
            ssh_jump_secrets.clear();
            ssh_jump_private_keys.clear();
            *port = address.port();
            *host = "::1".into();
        }
        ConnectionOptions::Postgres {
            ssh,
            ssh_secret,
            ssh_private_key,
            ssh_jump_secrets,
            ssh_jump_private_keys,
            port,
            host,
            ..
        } => {
            *ssh = None;
            *ssh_secret = None;
            *ssh_private_key = None;
            ssh_jump_secrets.clear();
            ssh_jump_private_keys.clear();
            *port = address.port();
            *host = "::1".into();
        }
        _ => unreachable!(),
    }
    verify(driver, direct, "binding-external").await;
    let mut cursor = owner
        .execute("SELECT 916", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(916)]]
    );
    cursor.close().await.unwrap();
    owner.close().await.unwrap();
    drop(owner);
    tokio::time::timeout(std::time::Duration::from_secs(2), async {
        loop {
            if tokio::net::TcpListener::bind(address).await.is_ok() {
                break;
            }
            tokio::task::yield_now().await;
        }
    })
    .await
    .expect("local binding survived owner close");
}
fn authentication_count(name: &str) -> usize {
    let output = std::process::Command::new("docker")
        .args(["exec", &value(name), "cat", "/tmp/fixture-auth.log"])
        .output()
        .unwrap();
    assert!(output.status.success());
    [output.stdout, output.stderr]
        .iter()
        .flat_map(|bytes| {
            String::from_utf8_lossy(bytes)
                .lines()
                .map(str::to_owned)
                .collect::<Vec<_>>()
        })
        .filter(|line| line.contains("Accepted "))
        .count()
}
pub async fn sharing(driver: &dyn DatabaseDriver, make_options: impl Fn() -> ConnectionOptions) {
    let options = || {
        let mut options = make_options();
        match &mut options {
            ConnectionOptions::Mysql {
                ssh: Some(settings),
                ..
            }
            | ConnectionOptions::Postgres {
                ssh: Some(settings),
                ..
            } => settings.options.share_tunnels = true,
            _ => unreachable!(),
        }
        options
    };
    let depth = match options() {
        ConnectionOptions::Mysql {
            ssh: Some(settings),
            ..
        }
        | ConnectionOptions::Postgres {
            ssh: Some(settings),
            ..
        } => settings.options.jump_hosts.len(),
        _ => unreachable!(),
    };
    let before = authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER");
    let mut first = driver.connect(options()).await.unwrap();
    // The configured empty global trust store must remain shareable even on
    // macOS, where writing this device changes its modification timestamps.
    std::fs::write("/dev/null", b"unrelated process output").unwrap();
    let mut second = driver.connect(options()).await.unwrap();
    verify(driver, options(), "sharing").await;
    assert_eq!(
        authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER") - before,
        depth,
        "identical connections reauthenticated instead of sharing the SSH chain"
    );
    let mut wrong = options();
    match &mut wrong {
        ConnectionOptions::Mysql { ssh_secret, .. }
        | ConnectionOptions::Postgres { ssh_secret, .. } => {
            *ssh_secret = Some(Secret::new("wrong-sharing-secret"))
        }
        _ => unreachable!(),
    }
    assert!(
        driver.connect(wrong).await.is_err(),
        "changed target secret reused an authenticated session"
    );
    let path = value("CHOSCORDB_SSH_KNOWN_HOSTS");
    let original = std::fs::read(&path).unwrap();
    std::fs::write(&path, "").unwrap();
    let rejected = driver.connect(options()).await.is_err();
    std::fs::write(&path, original).unwrap();
    assert!(
        rejected,
        "changed trust file reused an authenticated session"
    );
    first.close().await.unwrap();
    drop(first);
    let mut cursor = second
        .execute("SELECT 918", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(918)]]
    );
    cursor.close().await.unwrap();
    second.close().await.unwrap();
    drop(second);
    tokio::time::timeout(std::time::Duration::from_secs(3), async {
        loop {
            let paths =
                std::fs::read_to_string(value("CHOSCORDB_SSH_CONTROL_AUDIT")).unwrap_or_default();
            if paths
                .lines()
                .all(|path| !std::path::Path::new(path).exists())
            {
                break;
            }
            tokio::time::sleep(std::time::Duration::from_millis(20)).await;
        }
    })
    .await
    .expect("last owner retained a shared SSH master/control socket");
}

pub async fn certificate_context(
    driver: &dyn DatabaseDriver,
    make_options: impl Fn() -> ConnectionOptions,
) {
    let options = || {
        let mut options = make_options();
        match &mut options {
            ConnectionOptions::Mysql {
                ssh: Some(settings),
                ..
            }
            | ConnectionOptions::Postgres {
                ssh: Some(settings),
                ..
            } => settings.options.share_tunnels = true,
            _ => unreachable!(),
        }
        options
    };
    let identity = std::path::PathBuf::from(value("CHOSCORDB_SSH_TARGET_KEY"));
    let certificate = identity.with_file_name(format!(
        "{}-cert.pub",
        identity.file_name().unwrap().to_str().unwrap()
    ));
    let public = identity.with_file_name(format!(
        "{}.pub",
        identity.file_name().unwrap().to_str().unwrap()
    ));
    assert!(!certificate.exists());
    let before = authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER");
    let mut first = driver.connect(options()).await.unwrap();
    let mut second = driver.connect(options()).await.unwrap();
    assert_eq!(
        authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER") - before,
        1
    );
    let signed = std::process::Command::new("ssh-keygen")
        .args(["-s"])
        .arg(identity.parent().unwrap().join("host_ca"))
        .args([
            "-I",
            "owned-context-regression",
            "-n",
            "root",
            "-V",
            "-1m:+1h",
        ])
        .arg(&public)
        .output()
        .unwrap();
    assert!(signed.status.success());
    let mut third = driver.connect(options()).await.unwrap();
    assert_eq!(
        authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER") - before,
        2,
        "new implicit user certificate reused stale SSH authentication"
    );
    let original = std::fs::read(&public).unwrap();
    let mut changed = original.clone();
    changed.push(b'\n');
    std::fs::write(&public, changed).unwrap();
    let fourth = driver.connect(options()).await;
    std::fs::write(&public, original).unwrap();
    std::fs::remove_file(certificate).unwrap();
    let mut fourth = fourth.unwrap();
    assert_eq!(
        authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER") - before,
        3,
        "changed public identity sidecar reused stale SSH authentication"
    );
    first.close().await.unwrap();
    second.close().await.unwrap();
    third.close().await.unwrap();
    fourth.close().await.unwrap();
}

pub async fn reconnect_shared(
    driver: &dyn DatabaseDriver,
    make_options: impl Fn() -> ConnectionOptions,
    fixed: bool,
) {
    let reserved = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = reserved.local_addr().unwrap();
    drop(reserved);
    let options = || {
        let mut options = make_options();
        match &mut options {
            ConnectionOptions::Mysql {
                ssh: Some(settings),
                ..
            }
            | ConnectionOptions::Postgres {
                ssh: Some(settings),
                ..
            } => {
                settings.options.share_tunnels = true;
                settings.options.local_host = Some("127.0.0.1".into());
                settings.options.local_port = Some(if fixed { address.port() } else { 0 });
            }
            _ => unreachable!(),
        }
        options
    };
    let mut healthy = driver.connect(options()).await.unwrap();
    let mut failed = driver.connect(options()).await.unwrap();
    failed.close().await.unwrap();
    drop(failed);
    let before = authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER");
    if fixed {
        let error = driver
            .reconnect(options())
            .await
            .err()
            .expect("fresh fixed-port recovery must not reuse a healthy listener");
        assert_eq!(error.kind, ErrorKind::Connection);
        assert!(error.message.contains("bind selected SSH local address"));
        let mut cursor = healthy
            .execute("SELECT 941", QueryOptions::default())
            .await
            .unwrap();
        assert_eq!(
            cursor.fetch_page(PageSize::default()).await.unwrap().rows,
            vec![vec![Value::Integer(941)]]
        );
        cursor.close().await.unwrap();
        healthy.close().await.unwrap();
        drop(healthy);
        tokio::time::timeout(std::time::Duration::from_secs(3), async {
            loop {
                if let Ok(listener) = tokio::net::TcpListener::bind(address).await {
                    drop(listener);
                    break;
                }
                tokio::time::sleep(std::time::Duration::from_millis(20)).await;
            }
        })
        .await
        .expect("last lease must release selected fixed port");
        let mut recovered = driver.reconnect(options()).await.unwrap();
        assert!(tokio::net::TcpListener::bind(address).await.is_err());
        let mut cursor = recovered
            .execute("SELECT 942", QueryOptions::default())
            .await
            .unwrap();
        assert_eq!(
            cursor.fetch_page(PageSize::default()).await.unwrap().rows,
            vec![vec![Value::Integer(942)]]
        );
        cursor.close().await.unwrap();
        recovered.close().await.unwrap();
        drop(recovered);
    } else {
        let mut recovered = driver.reconnect(options()).await.unwrap();
        assert!(
            authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER") > before,
            "reconnect reused the old authenticated master"
        );
        for connection in [&mut healthy, &mut recovered] {
            let mut cursor = connection
                .execute("SELECT 943", QueryOptions::default())
                .await
                .unwrap();
            assert_eq!(
                cursor.fetch_page(PageSize::default()).await.unwrap().rows,
                vec![vec![Value::Integer(943)]]
            );
            cursor.close().await.unwrap();
        }
        let authenticated = authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER");
        let mut wrong = options();
        match &mut wrong {
            ConnectionOptions::Mysql { ssh_secret, .. }
            | ConnectionOptions::Postgres { ssh_secret, .. } => {
                *ssh_secret = Some(Secret::new("wrong-recovery-context"))
            }
            _ => unreachable!(),
        }
        assert!(driver.reconnect(wrong).await.is_err());
        let mut additional = driver.connect(options()).await.unwrap();
        assert_eq!(
            authentication_count("CHOSCORDB_SSH_TARGET_CONTAINER"),
            authenticated,
            "unrelated failed credentials invalidated the healthy sharing context"
        );
        additional.close().await.unwrap();
        drop(additional);
        healthy.close().await.unwrap();
        drop(healthy);
        recovered.close().await.unwrap();
        drop(recovered);
    }
    tokio::time::timeout(std::time::Duration::from_secs(3), async {
        loop {
            let paths =
                std::fs::read_to_string(value("CHOSCORDB_SSH_CONTROL_AUDIT")).unwrap_or_default();
            if paths
                .lines()
                .all(|path| !std::path::Path::new(path).exists())
            {
                break;
            }
            tokio::time::sleep(std::time::Duration::from_millis(20)).await;
        }
    })
    .await
    .expect("recovery leaked an owned SSH master");
}
