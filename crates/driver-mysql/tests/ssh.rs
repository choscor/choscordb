//! Run via scripts/integration/mysql_ssh_fixture.py with real OpenSSH forwarding.
use choscordb_driver_api::*;
use choscordb_driver_mysql::MysqlDriver;
use std::time::Duration;

fn options(trusted: bool) -> ConnectionOptions {
    ConnectionOptions::Mysql {
        ssh_jump_secrets: Default::default(),
        ssh_private_key: None,
        ssh_jump_private_keys: Default::default(),
        proxy: None,
        proxy_secret: None,
        // This name resolves only inside the SSH fixture's Docker container.
        host: "mysql-ssh-target".into(),
        port: 3306,
        database: "choscordb_test".into(),
        user: "root".into(),
        password: Some(Secret::new("choscordb-test-password")),
        ssh_secret: None,
        tls: TlsMode::Disable,
        tls_identity: None,
        root_certificate: None,
        ssh: Some(SshTunnel {
            options: Default::default(),
            host: if trusted { "127.0.0.1" } else { "localhost" }.into(),
            port: std::env::var("CHOSCORDB_SSH_PORT")
                .expect("run mysql_ssh_fixture.py")
                .parse()
                .unwrap(),
            user: "root".into(),
            authentication: SshAuthentication::PublicKey,
            identity_source: Default::default(),
            identity_file: Some(std::env::var("CHOSCORDB_SSH_IDENTITY").expect("fixture identity")),
        }),
    }
}

#[tokio::test]
#[ignore = "requires scripts/integration/mysql_ssh_fixture.py"]
async fn ssh_queries_cancellation_and_independent_object_reads() {
    let mut conn = MysqlDriver.connect(options(true)).await.unwrap();
    let mut cursor = conn
        .execute("SELECT 731", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(731)]]
    );
    cursor.close().await.unwrap();
    let token = conn.cancellation_handle();
    let stop = async {
        tokio::time::sleep(Duration::from_millis(100)).await;
        token.cancel().await.unwrap();
    };
    let (result, ()) = tokio::time::timeout(Duration::from_secs(8), async {
        tokio::join!(
            async {
                let mut cursor = conn
                    .execute("SELECT SLEEP(30)", QueryOptions::default())
                    .await?;
                cursor.fetch_page(PageSize::default()).await
            },
            stop
        )
    })
    .await
    .unwrap();
    assert_eq!(result.err().unwrap().kind, ErrorKind::Cancelled);
    let mut cursor = conn
        .execute("SELECT 732", QueryOptions::default())
        .await
        .unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(732)]]
    );
    cursor.close().await.unwrap();
    conn.execute(
        "CREATE OR REPLACE VIEW mysql_ssh_object AS SELECT 733 AS value",
        QueryOptions::default(),
    )
    .await
    .unwrap()
    .close()
    .await
    .unwrap();
    let object = ObjectId(r#"["choscordb_test","mysql_ssh_object"]"#.into());
    let mut cursor = conn.open_object(&object, 1024 * 1024).await.unwrap();
    assert_eq!(
        cursor.fetch_page(PageSize::default()).await.unwrap().rows,
        vec![vec![Value::Integer(733)]]
    );
    cursor.close().await.unwrap();
    conn.execute("DROP VIEW mysql_ssh_object", QueryOptions::default())
        .await
        .unwrap()
        .close()
        .await
        .unwrap();
    conn.close().await.unwrap();
}

#[tokio::test]
#[ignore = "requires scripts/integration/mysql_ssh_fixture.py"]
async fn unknown_ssh_host_key_is_rejected() {
    let result = tokio::time::timeout(Duration::from_secs(20), MysqlDriver.connect(options(false)))
        .await
        .unwrap();
    assert!(result.is_err(), "untrusted SSH host must not connect");
}
