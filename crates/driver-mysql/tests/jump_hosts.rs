//! Run only with scripts/integration/mysql_jump_fixture.py.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;

#[tokio::test]
#[ignore = "requires isolated Docker SSH jump fixture"]
async fn agent_jump_chain_reaches_database_from_target_ssh_host() {
    verify_connection(false).await;
}

#[tokio::test]
#[ignore = "requires isolated Docker SSH jump fixture"]
async fn remote_override_reaches_database_with_an_unresolvable_original_hostname() {
    verify_connection(true).await;
}

async fn verify_connection(remote_override: bool) {
    let mut connection = MysqlDriver
        .connect(ConnectionOptions::Mysql {
            ssh_jump_secrets: Default::default(),
            ssh_private_key: None,
            ssh_jump_private_keys: Default::default(),
            proxy: None,
            proxy_secret: None,
            host: if remote_override {
                "tls-name.example.invalid"
            } else {
                "mysql-ssh-target"
            }
            .into(),
            port: if remote_override { 1 } else { 3306 },
            user: "root".into(),
            database: "choscordb_test".into(),
            password: Some(Secret::new("choscordb-test-password")),
            ssh_secret: None,
            tls: TlsMode::Disable,
            root_certificate: None,
            tls_identity: None,
            ssh: Some(SshTunnel {
                host: "target-ssh".into(),
                port: 22,
                user: "root".into(),
                authentication: SshAuthentication::Agent,
                identity_source: Default::default(),
                identity_file: None,
                options: Box::new(SshOptions {
                    local_host: None,
                    local_port: None,
                    share_tunnels: false,
                    remote_host: remote_override.then(|| "mysql-ssh-target".into()),
                    remote_port: remote_override.then_some(3306),
                    connect_timeout_seconds: 10,
                    server_alive_interval_seconds: 1,
                    server_alive_count_max: 2,
                    agent_socket: Some(std::env::var("SSH_AUTH_SOCK").unwrap()),
                    known_hosts_file: Some(std::env::var("CHOSCORDB_SSH_KNOWN_HOSTS").unwrap()),
                    jump_hosts: vec![SshJumpHost {
                        id: None,
                        authentication: Default::default(),
                        identity_source: Default::default(),
                        identity_file: None,
                        agent_socket: None,
                        known_hosts_file: None,
                        host: "127.0.0.1".into(),
                        port: std::env::var("CHOSCORDB_SSH_JUMP_PORT")
                            .unwrap()
                            .parse()
                            .unwrap(),
                        user: "root".into(),
                    }],
                }),
            }),
        })
        .await
        .unwrap();
    for expected in [741, 742] {
        let mut cursor = connection
            .execute(&format!("SELECT {expected}"), QueryOptions::default())
            .await
            .unwrap();
        assert_eq!(
            cursor.fetch_page(PageSize::default()).await.unwrap().rows,
            vec![vec![Value::Integer(expected)]]
        );
        cursor.close().await.unwrap();
    }
    connection.close().await.unwrap();
}
